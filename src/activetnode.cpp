// Copyright (c) 2014-2019 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activetnode.h"
#include "evo/deterministicmns.h"
#include "init.h"
#include "tnode-sync.h"
#include "netbase.h"
#include "protocol.h"
#include "validation.h"
#include "warnings.h"

// Keep track of the active Tnode
CActiveTnodeInfo activeTnodeInfo;
CActiveTnodeManager* activeTnodeManager;

std::string CActiveTnodeManager::GetStateString() const
{
    switch (state) {
    case TNODE_WAITING_FOR_PROTX:
        return "WAITING_FOR_PROTX";
    case TNODE_POSE_BANNED:
        return "POSE_BANNED";
    case TNODE_REMOVED:
        return "REMOVED";
    case TNODE_OPERATOR_KEY_CHANGED:
        return "OPERATOR_KEY_CHANGED";
    case TNODE_PROTX_IP_CHANGED:
        return "PROTX_IP_CHANGED";
    case TNODE_READY:
        return "READY";
    case TNODE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

std::string CActiveTnodeManager::GetStatus() const
{
    switch (state) {
    case TNODE_WAITING_FOR_PROTX:
        return "Waiting for ProTx to appear on-chain";
    case TNODE_POSE_BANNED:
        return "Tnode was PoSe banned";
    case TNODE_REMOVED:
        return "Tnode removed from list";
    case TNODE_OPERATOR_KEY_CHANGED:
        return "Operator key changed or revoked";
    case TNODE_PROTX_IP_CHANGED:
        return "IP address specified in ProTx changed";
    case TNODE_READY:
        return "Ready";
    case TNODE_ERROR:
        return "Error. " + strError;
    default:
        return "Unknown";
    }
}

void CActiveTnodeManager::Init()
{
    LOCK(cs_main);

    if (!fTnodeMode) return;

    if (!deterministicMNManager->IsDIP3Enforced()) return;

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        state = TNODE_ERROR;
        strError = "Tnode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveDeterministicTnodeManager::Init -- ERROR: %s\n", strError);
        return;
    }

    if (!GetLocalAddress(activeTnodeInfo.service)) {
        state = TNODE_ERROR;
        return;
    }

    CDeterministicMNList mnList = deterministicMNManager->GetListAtChainTip();

    CDeterministicMNCPtr dmn = mnList.GetMNByOperatorKey(*activeTnodeInfo.blsPubKeyOperator);
    if (!dmn) {
        // MN not appeared on the chain yet
        return;
    }

    if (!mnList.IsMNValid(dmn->proTxHash)) {
        if (mnList.IsMNPoSeBanned(dmn->proTxHash)) {
            state = TNODE_POSE_BANNED;
        } else {
            state = TNODE_REMOVED;
        }
        return;
    }

    LogPrintf("CActiveTnodeManager::Init -- proTxHash=%s, proTx=%s\n", dmn->proTxHash.ToString(), dmn->ToString());

    if (activeTnodeInfo.service != dmn->pdmnState->addr) {
        state = TNODE_ERROR;
        strError = "Local address does not match the address from ProTx";
        LogPrintf("CActiveTnodeManager::Init -- ERROR: %s", strError);
        return;
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST) {
        // Check socket connectivity
        LogPrintf("CActiveDeterministicTnodeManager::Init -- Checking inbound connection to '%s'\n", activeTnodeInfo.service.ToString());
        SOCKET hSocket;
        bool fConnected = ConnectSocket(activeTnodeInfo.service, hSocket, nConnectTimeout) && IsSelectableSocket(hSocket);
        CloseSocket(hSocket);

        if (!fConnected) {
            state = TNODE_ERROR;
            strError = "Could not connect to " + activeTnodeInfo.service.ToString();
            LogPrintf("CActiveDeterministicTnodeManager::Init -- ERROR: %s\n", strError);
            return;
        }
    }

    activeTnodeInfo.proTxHash = dmn->proTxHash;
    activeTnodeInfo.outpoint = dmn->collateralOutpoint;
    state = TNODE_READY;
}

void CActiveTnodeManager::UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload)
{
    LOCK(cs_main);

    if (!fTnodeMode) return;

    if (!deterministicMNManager->IsDIP3Enforced(pindexNew->nHeight)) return;

    if (state == TNODE_READY) {
        auto oldMNList = deterministicMNManager->GetListForBlock(pindexNew->pprev);
        auto newMNList = deterministicMNManager->GetListForBlock(pindexNew);
        if (!newMNList.IsMNValid(activeTnodeInfo.proTxHash)) {
            // MN disappeared from MN list
            state = TNODE_REMOVED;
            activeTnodeInfo.proTxHash = uint256();
            activeTnodeInfo.outpoint.SetNull();
            // MN might have reappeared in same block with a new ProTx
            Init();
            return;
        }

        auto oldDmn = oldMNList.GetMN(activeTnodeInfo.proTxHash);
        auto newDmn = newMNList.GetMN(activeTnodeInfo.proTxHash);
        if (newDmn->pdmnState->pubKeyOperator != oldDmn->pdmnState->pubKeyOperator) {
            // MN operator key changed or revoked
            state = TNODE_OPERATOR_KEY_CHANGED;
            activeTnodeInfo.proTxHash = uint256();
            activeTnodeInfo.outpoint.SetNull();
            // MN might have reappeared in same block with a new ProTx
            Init();
            return;
        }

        if (newDmn->pdmnState->addr != oldDmn->pdmnState->addr) {
            // MN IP changed
            state = TNODE_PROTX_IP_CHANGED;
            activeTnodeInfo.proTxHash = uint256();
            activeTnodeInfo.outpoint.SetNull();
            Init();
            return;
        }
    } else {
        // MN might have (re)appeared with a new ProTx or we've found some peers
        // and figured out our local address
        Init();
    }
}

bool CActiveTnodeManager::GetLocalAddress(CService& addrRet)
{
    // First try to find whatever local address is specified by externalip option
    bool fFoundLocal = GetLocal(addrRet) && IsValidNetAddr(addrRet);
    if (!fFoundLocal && Params().NetworkIDString() == CBaseChainParams::REGTEST) {
        if (Lookup("127.0.0.1", addrRet, GetListenPort(), false)) {
            fFoundLocal = true;
        }
    }
    if (!fFoundLocal) {
        bool empty = true;
        // If we have some peers, let's try to find our local address from one of them
        g_connman->ForEachNodeContinueIf(CConnman::AllNodes, [&fFoundLocal, &empty](CNode* pnode) {
            empty = false;
            if (pnode->addr.IsIPv4())
                fFoundLocal = GetLocal(activeTnodeInfo.service, &pnode->addr) && IsValidNetAddr(activeTnodeInfo.service);
            return !fFoundLocal;
        });
        // nothing and no live connections, can't do anything for now
        if (empty) {
            strError = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
            LogPrintf("CActiveTnodeManager::GetLocalAddress -- ERROR: %s\n", strError);
            return false;
        }
    }
    return true;
}

bool CActiveTnodeManager::IsValidNetAddr(CService addrIn)
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkIDString() == CBaseChainParams::REGTEST ||
           (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}

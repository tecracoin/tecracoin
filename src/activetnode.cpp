// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activetnode.h"
#include "consensus/consensus.h"
#include "tnode.h"
#include "tnode-sync.h"
#include "tnode-payments.h"
#include "tnodeman.h"
#include "protocol.h"
#include "netbase.h"

// TODO: upgrade to new dash, remove this hack
#define vNodes (g_connman->vNodes)
#define cs_vNodes (g_connman->cs_vNodes)

extern CWallet *pwalletMain;

// Keep track of the active Tnode
CActiveTnode activeTnode;

void CActiveTnode::ManageState() {
    LogPrint("tnode", "CActiveTnode::ManageState -- Start\n");
    if (!fMasternodeMode) {
        LogPrint("tnode", "CActiveTnode::ManageState -- Not a tnode, returning\n");
        return;
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !tnodeSync.IsBlockchainSynced()) {
        nState = ACTIVE_TNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveTnode::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if (nState == ACTIVE_TNODE_SYNC_IN_PROCESS) {
        nState = ACTIVE_TNODE_INITIAL;
    }

    LogPrint("tnode", "CActiveTnode::ManageState -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);

    if (eType == TNODE_UNKNOWN) {
        ManageStateInitial();
    }

    if (eType == TNODE_REMOTE) {
        ManageStateRemote();
    } else if (eType == TNODE_LOCAL) {
        // Try Remote Start first so the started local tnode can be restarted without recreate tnode broadcast.
        ManageStateRemote();
        if (nState != ACTIVE_TNODE_STARTED)
            ManageStateLocal();
    }

    SendTnodePing();
}

std::string CActiveTnode::GetStateString() const {
    switch (nState) {
        case ACTIVE_TNODE_INITIAL:
            return "INITIAL";
        case ACTIVE_TNODE_SYNC_IN_PROCESS:
            return "SYNC_IN_PROCESS";
        case ACTIVE_TNODE_INPUT_TOO_NEW:
            return "INPUT_TOO_NEW";
        case ACTIVE_TNODE_NOT_CAPABLE:
            return "NOT_CAPABLE";
        case ACTIVE_TNODE_STARTED:
            return "STARTED";
        default:
            return "UNKNOWN";
    }
}

std::string CActiveTnode::GetStatus() const {
    switch (nState) {
        case ACTIVE_TNODE_INITIAL:
            return "Node just started, not yet activated";
        case ACTIVE_TNODE_SYNC_IN_PROCESS:
            return "Sync in progress. Must wait until sync is complete to start Tnode";
        case ACTIVE_TNODE_INPUT_TOO_NEW:
            return strprintf("Tnode input must have at least %d confirmations",
                             Params().GetConsensus().nTnodeMinimumConfirmations);
        case ACTIVE_TNODE_NOT_CAPABLE:
            return "Not capable tnode: " + strNotCapableReason;
        case ACTIVE_TNODE_STARTED:
            return "Tnode successfully started";
        default:
            return "Unknown";
    }
}

std::string CActiveTnode::GetTypeString() const {
    std::string strType;
    switch (eType) {
        case TNODE_UNKNOWN:
            strType = "UNKNOWN";
            break;
        case TNODE_REMOTE:
            strType = "REMOTE";
            break;
        case TNODE_LOCAL:
            strType = "LOCAL";
            break;
        default:
            strType = "UNKNOWN";
            break;
    }
    return strType;
}

bool CActiveTnode::SendTnodePing() {
    if (!fPingerEnabled) {
        LogPrint("tnode",
                 "CActiveTnode::SendTnodePing -- %s: tnode ping service is disabled, skipping...\n",
                 GetStateString());
        return false;
    }

    if (!mnodeman.Has(vin)) {
        strNotCapableReason = "Tnode not in tnode list";
        nState = ACTIVE_TNODE_NOT_CAPABLE;
        LogPrintf("CActiveTnode::SendTnodePing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CTnodePing mnp(vin);
    if (!mnp.Sign(keyTnode, pubKeyTnode)) {
        LogPrintf("CActiveTnode::SendTnodePing -- ERROR: Couldn't sign Tnode Ping\n");
        return false;
    }

    // Update lastPing for our tnode in Tnode list
    if (mnodeman.IsTnodePingedWithin(vin, TNODE_MIN_MNP_SECONDS, mnp.sigTime)) {
        LogPrintf("CActiveTnode::SendTnodePing -- Too early to send Tnode Ping\n");
        return false;
    }

    mnodeman.SetTnodeLastPing(vin, mnp);

    LogPrintf("CActiveTnode::SendTnodePing -- Relaying ping, collateral=%s\n", vin.ToString());
    mnp.Relay();

    return true;
}

void CActiveTnode::ManageStateInitial() {
    LogPrint("tnode", "CActiveTnode::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ACTIVE_TNODE_NOT_CAPABLE;
        strNotCapableReason = "Tnode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveTnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    bool fFoundLocal = false;
    {
        LOCK(cs_vNodes);

        // First try to find whatever local address is specified by externalip option
        fFoundLocal = GetLocal(service) && CTnode::IsValidNetAddr(service);
        if (!fFoundLocal) {
            // nothing and no live connections, can't do anything for now
            if (vNodes.empty()) {
                nState = ACTIVE_TNODE_NOT_CAPABLE;
                strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
                LogPrintf("CActiveTnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
                return;
            }
            // We have some peers, let's try to find our local address from one of them
            BOOST_FOREACH(CNode * pnode, vNodes)
            {
                if (pnode->fSuccessfullyConnected && pnode->addr.IsIPv4()) {
                    fFoundLocal = GetLocal(service, &pnode->addr) && CTnode::IsValidNetAddr(service);
                    if (fFoundLocal) break;
                }
            }
        }
    }
        
    if(Params().NetworkIDString() == CBaseChainParams::REGTEST) {
        std::string const & serv = GetArg("-externalip", "");
        if(!serv.empty()) {
            if (Lookup(serv.c_str(), service, 0, false))
                fFoundLocal = true;
        }

    }
    if(!fFoundLocal)
    {
        LOCK(cs_vNodes);

        // First try to find whatever local address is specified by externalip option
        fFoundLocal = GetLocal(service) && CTnode::IsValidNetAddr(service);
        if (!fFoundLocal) {
            // nothing and no live connections, can't do anything for now
            if (vNodes.empty()) {
                nState = ACTIVE_TNODE_NOT_CAPABLE;
                strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
                LogPrintf("CActiveTnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
                return;
            }
            // We have some peers, let's try to find our local address from one of them
            BOOST_FOREACH(CNode * pnode, vNodes)
            {
                if (pnode->fSuccessfullyConnected && pnode->addr.IsIPv4()) {
                    fFoundLocal = GetLocal(service, &pnode->addr) && CTnode::IsValidNetAddr(service);
                    if (fFoundLocal) break;
                }
            }
        }
    }

    if (!fFoundLocal) {
        nState = ACTIVE_TNODE_NOT_CAPABLE;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveTnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            nState = ACTIVE_TNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Invalid port: %u - only %d is supported on mainnet.", service.GetPort(),
                                            mainnetDefaultPort);
            LogPrintf("CActiveTnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        nState = ACTIVE_TNODE_NOT_CAPABLE;
        strNotCapableReason = strprintf("Invalid port: %u - %d is only supported on mainnet.", service.GetPort(),
                                        mainnetDefaultPort);
        LogPrintf("CActiveTnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    LogPrintf("CActiveTnode::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());
    //TODO
    if (!g_connman->OpenMasternodeConnection(CAddress(service, NODE_NETWORK))) {
        nState = ACTIVE_TNODE_NOT_CAPABLE;
        strNotCapableReason = "Could not connect to " + service.ToString();
        LogPrintf("CActiveTnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // Default to REMOTE
    eType = TNODE_REMOTE;

    // Check if wallet funds are available
    if (!pwalletMain) {
        LogPrintf("CActiveTnode::ManageStateInitial -- %s: Wallet not available\n", GetStateString());
        return;
    }

    if (pwalletMain->IsLocked()) {
        LogPrintf("CActiveTnode::ManageStateInitial -- %s: Wallet is locked\n", GetStateString());
        return;
    }

    if (pwalletMain->GetBalance() < TNODE_COIN_REQUIRED * COIN) {
        LogPrintf("CActiveTnode::ManageStateInitial -- %s: Wallet balance is < 10000 TCR\n", GetStateString());
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    // If collateral is found switch to LOCAL mode
    if (pwalletMain->GetTnodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        eType = TNODE_LOCAL;
    }

    LogPrint("tnode", "CActiveTnode::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveTnode::ManageStateRemote() {
    LogPrint("tnode",
             "CActiveTnode::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyTnode.GetID() = %s\n",
             GetStatus(), fPingerEnabled, GetTypeString(), pubKeyTnode.GetID().ToString());

    mnodeman.CheckTnode(pubKeyTnode);
    tnode_info_t infoMn = mnodeman.GetTnodeInfo(pubKeyTnode);

    if (infoMn.fInfoValid) {
        if (infoMn.nProtocolVersion < MIN_TNODE_PAYMENT_PROTO_VERSION_1
                || infoMn.nProtocolVersion > MIN_TNODE_PAYMENT_PROTO_VERSION_2) {
            nState = ACTIVE_TNODE_NOT_CAPABLE;
            strNotCapableReason = "Invalid protocol version";
            LogPrintf("CActiveTnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (service != infoMn.addr) {
            nState = ACTIVE_TNODE_NOT_CAPABLE;
            // LogPrintf("service: %s\n", service.ToString());
            // LogPrintf("infoMn.addr: %s\n", infoMn.addr.ToString());
            strNotCapableReason = "Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this tnode changed recently.";
            LogPrintf("CActiveTnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (!CTnode::IsValidStateForAutoStart(infoMn.nActiveState)) {
            nState = ACTIVE_TNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Tnode in %s state", CTnode::StateToString(infoMn.nActiveState));
            LogPrintf("CActiveTnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (nState != ACTIVE_TNODE_STARTED) {
            LogPrintf("CActiveTnode::ManageStateRemote -- STARTED!\n");
            vin = infoMn.vin;
            service = infoMn.addr;
            fPingerEnabled = true;
            nState = ACTIVE_TNODE_STARTED;
        }
    } else {
        nState = ACTIVE_TNODE_NOT_CAPABLE;
        strNotCapableReason = "Tnode not in tnode list";
        LogPrintf("CActiveTnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}

void CActiveTnode::ManageStateLocal() {
    LogPrint("tnode", "CActiveTnode::ManageStateLocal -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);
    if (nState == ACTIVE_TNODE_STARTED) {
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    if (pwalletMain->GetTnodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        int nInputAge = GetInputAge(vin);
        if (nInputAge < Params().GetConsensus().nTnodeMinimumConfirmations) {
            nState = ACTIVE_TNODE_INPUT_TOO_NEW;
            strNotCapableReason = strprintf(_("%s - %d confirmations"), GetStatus(), nInputAge);
            LogPrintf("CActiveTnode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        {
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);
        }

        CTnodeBroadcast mnb;
        std::string strError;
        if (!CTnodeBroadcast::Create(vin, service, keyCollateral, pubKeyCollateral, keyTnode,
                                     pubKeyTnode, strError, mnb)) {
            nState = ACTIVE_TNODE_NOT_CAPABLE;
            strNotCapableReason = "Error creating mastenode broadcast: " + strError;
            LogPrintf("CActiveTnode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        fPingerEnabled = true;
        nState = ACTIVE_TNODE_STARTED;

        //update to tnode list
        LogPrintf("CActiveTnode::ManageStateLocal -- Update Tnode List\n");
        mnodeman.UpdateTnodeList(mnb);
        mnodeman.NotifyTnodeUpdates();

        //send to all peers
        LogPrintf("CActiveTnode::ManageStateLocal -- Relay broadcast, vin=%s\n", vin.ToString());
        mnb.RelayTNode();
    }
}

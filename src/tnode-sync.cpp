// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activetnode.h"
#include "checkpoints.h"
#include "main.h"
#include "tnode.h"
#include "tnode-payments.h"
#include "tnode-sync.h"
#include "tnodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"

class CTnodeSync;

CTnodeSync tnodeSync;

bool CTnodeSync::CheckNodeHeight(CNode *pnode, bool fDisconnectStuckNodes) {
    CNodeStateStats stats;
    if (!GetNodeStateStats(pnode->id, stats) || stats.nCommonHeight == -1 || stats.nSyncHeight == -1) return false; // not enough info about this peer

    // Check blocks and headers, allow a small error margin of 1 block
    if (pCurrentBlockIndex->nHeight - 1 > stats.nCommonHeight) {
        // This peer probably stuck, don't sync any additional data from it
        if (fDisconnectStuckNodes) {
            // Disconnect to free this connection slot for another peer.
            pnode->fDisconnect = true;
            LogPrintf("CTnodeSync::CheckNodeHeight -- disconnecting from stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                      pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->id);
        } else {
            LogPrintf("CTnodeSync::CheckNodeHeight -- skipping stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                      pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->id);
        }
        return false;
    } else if (pCurrentBlockIndex->nHeight < stats.nSyncHeight - 1) {
        // This peer announced more headers than we have blocks currently
        LogPrint("tnode", "CTnodeSync::CheckNodeHeight -- skipping peer, who announced more headers than we have blocks currently, nHeight=%d, nSyncHeight=%d, peer=%d\n",
                  pCurrentBlockIndex->nHeight, stats.nSyncHeight, pnode->id);
        return false;
    }

    return true;
}

bool CTnodeSync::IsBlockchainSynced(bool fBlockAccepted) {
    static bool fBlockchainSynced = false;
    static int64_t nTimeLastProcess = GetTime();
    static int nSkipped = 0;
    static bool fFirstBlockAccepted = false;

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if (GetTime() - nTimeLastProcess > 60 * 60) {
        LogPrintf("CTnodeSync::IsBlockchainSynced time-check fBlockchainSynced=%s\n", fBlockchainSynced);
        Reset();
        fBlockchainSynced = false;
    }

    if (!pCurrentBlockIndex || !pindexBestHeader || fImporting || fReindex) return false;

    if (fBlockAccepted) {
        // this should be only triggered while we are still syncing
        if (!IsSynced()) {
            // we are trying to download smth, reset blockchain sync status
            fFirstBlockAccepted = true;
            fBlockchainSynced = false;
            nTimeLastProcess = GetTime();
            return false;
        }
    } else {
        //Dont skip on REGTEST to make the tests run faster
        if(Params().NetworkIDString() != CBaseChainParams::REGTEST) {
            // skip if we already checked less than 1 tick ago
            if (GetTime() - nTimeLastProcess < TNODE_SYNC_TICK_SECONDS) {
                nSkipped++;
                return fBlockchainSynced;
            }
        }
    }

    LogPrint("tnode-sync", "CTnodeSync::IsBlockchainSynced -- state before check: %ssynced, skipped %d times\n", fBlockchainSynced ? "" : "not ", nSkipped);

    nTimeLastProcess = GetTime();
    nSkipped = 0;

    if (fBlockchainSynced){
        return true;
    }

    if (fCheckpointsEnabled && pCurrentBlockIndex->nHeight < Checkpoints::GetTotalBlocksEstimate(Params().Checkpoints())) {
        return false;
    }

    std::vector < CNode * > vNodesCopy = CopyNodeVector();
    // We have enough peers and assume most of them are synced
    if (vNodesCopy.size() >= TNODE_SYNC_ENOUGH_PEERS) {
        // Check to see how many of our peers are (almost) at the same height as we are
        int nNodesAtSameHeight = 0;
        BOOST_FOREACH(CNode * pnode, vNodesCopy)
        {
            // Make sure this peer is presumably at the same height
            if (!CheckNodeHeight(pnode)) {
                continue;
            }
            nNodesAtSameHeight++;
            // if we have decent number of such peers, most likely we are synced now
            if (nNodesAtSameHeight >= TNODE_SYNC_ENOUGH_PEERS) {
                LogPrintf("CTnodeSync::IsBlockchainSynced -- found enough peers on the same height as we are, done\n");
                fBlockchainSynced = true;
                ReleaseNodeVector(vNodesCopy);
                return true;
            }
        }
    }
    ReleaseNodeVector(vNodesCopy);

    // wait for at least one new block to be accepted
    if (!fFirstBlockAccepted) return false;

    // same as !IsInitialBlockDownload() but no cs_main needed here
    int64_t nMaxBlockTime = std::max(pCurrentBlockIndex->GetBlockTime(), pindexBestHeader->GetBlockTime());
    fBlockchainSynced = pindexBestHeader->nHeight - pCurrentBlockIndex->nHeight < 24 * 6 &&
                        GetTime() - nMaxBlockTime < Params().MaxTipAge();
    return fBlockchainSynced;
}

void CTnodeSync::Fail() {
    nTimeLastFailure = GetTime();
    nRequestedTnodeAssets = TNODE_SYNC_FAILED;
}

void CTnodeSync::Reset() {
    nRequestedTnodeAssets = TNODE_SYNC_INITIAL;
    nRequestedTnodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastTnodeList = GetTime();
    nTimeLastPaymentVote = GetTime();
    nTimeLastGovernanceItem = GetTime();
    nTimeLastFailure = 0;
    nCountFailures = 0;
}

std::string CTnodeSync::GetAssetName() {
    switch (nRequestedTnodeAssets) {
        case (TNODE_SYNC_INITIAL):
            return "TNODE_SYNC_INITIAL";
        case (TNODE_SYNC_SPORKS):
            return "TNODE_SYNC_SPORKS";
        case (TNODE_SYNC_LIST):
            return "TNODE_SYNC_LIST";
        case (TNODE_SYNC_MNW):
            return "TNODE_SYNC_MNW";
        case (TNODE_SYNC_FAILED):
            return "TNODE_SYNC_FAILED";
        case TNODE_SYNC_FINISHED:
            return "TNODE_SYNC_FINISHED";
        default:
            return "UNKNOWN";
    }
}

void CTnodeSync::SwitchToNextAsset() {
    switch (nRequestedTnodeAssets) {
        case (TNODE_SYNC_FAILED):
            throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
            break;
        case (TNODE_SYNC_INITIAL):
            ClearFulfilledRequests();
            nRequestedTnodeAssets = TNODE_SYNC_SPORKS;
            LogPrintf("CTnodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case (TNODE_SYNC_SPORKS):
            nTimeLastTnodeList = GetTime();
            nRequestedTnodeAssets = TNODE_SYNC_LIST;
            LogPrintf("CTnodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case (TNODE_SYNC_LIST):
            nTimeLastPaymentVote = GetTime();
            nRequestedTnodeAssets = TNODE_SYNC_MNW;
            LogPrintf("CTnodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;

        case (TNODE_SYNC_MNW):
            nTimeLastGovernanceItem = GetTime();
            LogPrintf("CTnodeSync::SwitchToNextAsset -- Sync has finished\n");
            nRequestedTnodeAssets = TNODE_SYNC_FINISHED;
            break;
    }
    nRequestedTnodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
}

std::string CTnodeSync::GetSyncStatus() {
    switch (tnodeSync.nRequestedTnodeAssets) {
        case TNODE_SYNC_INITIAL:
            return _("Synchronization pending...");
        case TNODE_SYNC_SPORKS:
            return _("Synchronizing sporks...");
        case TNODE_SYNC_LIST:
            return _("Synchronizing tnodes...");
        case TNODE_SYNC_MNW:
            return _("Synchronizing tnode payments...");
        case TNODE_SYNC_FAILED:
            return _("Synchronization failed");
        case TNODE_SYNC_FINISHED:
            return _("Synchronization finished");
        default:
            return "";
    }
}

void CTnodeSync::ProcessMessage(CNode *pfrom, std::string &strCommand, CDataStream &vRecv) {
    if (strCommand == NetMsgType::SYNCSTATUSCOUNT) { //Sync status count

        //do not care about stats if sync process finished or failed
        if (IsSynced() || IsFailed()) return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        LogPrintf("SYNCSTATUSCOUNT -- got inventory count: nItemID=%d  nCount=%d  peer=%d\n", nItemID, nCount, pfrom->id);
    }
}

void CTnodeSync::ClearFulfilledRequests() {
    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH(CNode * pnode, vNodes)
    {
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "spork-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "tnode-list-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "tnode-payment-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "full-sync");
    }
}

void CTnodeSync::ProcessTick() {
    static int nTick = 0;
    if (nTick++ % TNODE_SYNC_TICK_SECONDS != 0) return;
    if (!pCurrentBlockIndex) return;

    //the actual count of tnodes we have currently
    int nMnCount = mnodeman.CountTnodes();

    LogPrint("ProcessTick", "CTnodeSync::ProcessTick -- nTick %d nMnCount %d\n", nTick, nMnCount);

    // INITIAL SYNC SETUP / LOG REPORTING
    double nSyncProgress = double(nRequestedTnodeAttempt + (nRequestedTnodeAssets - 1) * 8) / (8 * 4);
    LogPrint("ProcessTick", "CTnodeSync::ProcessTick -- nTick %d nRequestedTnodeAssets %d nRequestedTnodeAttempt %d nSyncProgress %f\n", nTick, nRequestedTnodeAssets, nRequestedTnodeAttempt, nSyncProgress);
    uiInterface.NotifyAdditionalDataSyncProgressChanged(pCurrentBlockIndex->nHeight, nSyncProgress);

    // RESET SYNCING INCASE OF FAILURE
    {
        if (IsSynced()) {
            /*
                Resync if we lost all tnodes from sleep/wake or failed to sync originally
            */
            if (nMnCount == 0) {
                LogPrintf("CTnodeSync::ProcessTick -- WARNING: not enough data, restarting sync\n");
                Reset();
            } else {
                std::vector < CNode * > vNodesCopy = CopyNodeVector();
                ReleaseNodeVector(vNodesCopy);
                return;
            }
        }

        //try syncing again
        if (IsFailed()) {
            if (nTimeLastFailure + (1 * 60) < GetTime()) { // 1 minute cooldown after failed sync
                Reset();
            }
            return;
        }
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !IsBlockchainSynced() && nRequestedTnodeAssets > TNODE_SYNC_SPORKS) {
        nTimeLastTnodeList = GetTime();
        nTimeLastPaymentVote = GetTime();
        nTimeLastGovernanceItem = GetTime();
        return;
    }
    if (nRequestedTnodeAssets == TNODE_SYNC_INITIAL || (nRequestedTnodeAssets == TNODE_SYNC_SPORKS && IsBlockchainSynced())) {
        SwitchToNextAsset();
    }

    std::vector < CNode * > vNodesCopy = CopyNodeVector();

    BOOST_FOREACH(CNode * pnode, vNodesCopy)
    {
        // Don't try to sync any data from outbound "tnode" connections -
        // they are temporary and should be considered unreliable for a sync process.
        // Inbound connection this early is most likely a "tnode" connection
        // initialted from another node, so skip it too.
        if (pnode->fTnode || (fTNode && pnode->fInbound)) continue;

        // QUICK MODE (REGTEST ONLY!)
        if (Params().NetworkIDString() == CBaseChainParams::REGTEST) {
            if (nRequestedTnodeAttempt <= 2) {
                pnode->PushMessage(NetMsgType::GETSPORKS); //get current network sporks
            } else if (nRequestedTnodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if (nRequestedTnodeAttempt < 6) {
                int nMnCount = mnodeman.CountTnodes();
                pnode->PushMessage(NetMsgType::TNODEPAYMENTSYNC, nMnCount); //sync payment votes
            } else {
                nRequestedTnodeAssets = TNODE_SYNC_FINISHED;
            }
            nRequestedTnodeAttempt++;
            ReleaseNodeVector(vNodesCopy);
            return;
        }

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if (netfulfilledman.HasFulfilledRequest(pnode->addr, "full-sync")) {
                // We already fully synced from this node recently,
                // disconnect to free this connection slot for another peer.
                pnode->fDisconnect = true;
                LogPrintf("CTnodeSync::ProcessTick -- disconnecting from recently synced peer %d\n", pnode->id);
                continue;
            }

            // SPORK : ALWAYS ASK FOR SPORKS AS WE SYNC (we skip this mode now)

            if (!netfulfilledman.HasFulfilledRequest(pnode->addr, "spork-sync")) {
                // only request once from each peer
                netfulfilledman.AddFulfilledRequest(pnode->addr, "spork-sync");
                // get current network sporks
                pnode->PushMessage(NetMsgType::GETSPORKS);
                LogPrintf("CTnodeSync::ProcessTick -- nTick %d nRequestedTnodeAssets %d -- requesting sporks from peer %d\n", nTick, nRequestedTnodeAssets, pnode->id);
                continue; // always get sporks first, switch to the next node without waiting for the next tick
            }

            // MNLIST : SYNC TNODE LIST FROM OTHER CONNECTED CLIENTS

            if (nRequestedTnodeAssets == TNODE_SYNC_LIST) {
                // check for timeout first
                if (nTimeLastTnodeList < GetTime() - TNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CTnodeSync::ProcessTick -- nTick %d nRequestedTnodeAssets %d -- timeout\n", nTick, nRequestedTnodeAssets);
                    if (nRequestedTnodeAttempt == 0) {
                        LogPrintf("CTnodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // there is no way we can continue without tnode list, fail here and try later
                        Fail();
                        ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if (netfulfilledman.HasFulfilledRequest(pnode->addr, "tnode-list-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "tnode-list-sync");

                if (pnode->nVersion < mnpayments.GetMinTnodePaymentsProto()) continue;
                nRequestedTnodeAttempt++;

                mnodeman.DsegUpdate(pnode);

                ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // MNW : SYNC TNODE PAYMENT VOTES FROM OTHER CONNECTED CLIENTS

            if (nRequestedTnodeAssets == TNODE_SYNC_MNW) {
                LogPrint("mnpayments", "CTnodeSync::ProcessTick -- nTick %d nRequestedTnodeAssets %d nTimeLastPaymentVote %lld GetTime() %lld diff %lld\n", nTick, nRequestedTnodeAssets, nTimeLastPaymentVote, GetTime(), GetTime() - nTimeLastPaymentVote);
                // check for timeout first
                // This might take a lot longer than TNODE_SYNC_TIMEOUT_SECONDS minutes due to new blocks,
                // but that should be OK and it should timeout eventually.
                if (nTimeLastPaymentVote < GetTime() - TNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CTnodeSync::ProcessTick -- nTick %d nRequestedTnodeAssets %d -- timeout\n", nTick, nRequestedTnodeAssets);
                    if (nRequestedTnodeAttempt == 0) {
                        LogPrintf("CTnodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // probably not a good idea to proceed without winner list
                        Fail();
                        ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // check for data
                // if mnpayments already has enough blocks and votes, switch to the next asset
                // try to fetch data from at least two peers though
                if (nRequestedTnodeAttempt > 1 && mnpayments.IsEnoughData()) {
                    LogPrintf("CTnodeSync::ProcessTick -- nTick %d nRequestedTnodeAssets %d -- found enough data\n", nTick, nRequestedTnodeAssets);
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if (netfulfilledman.HasFulfilledRequest(pnode->addr, "tnode-payment-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "tnode-payment-sync");

                if (pnode->nVersion < mnpayments.GetMinTnodePaymentsProto()) continue;
                nRequestedTnodeAttempt++;

                // ask node for all payment votes it has (new nodes will only return votes for future payments)
                pnode->PushMessage(NetMsgType::TNODEPAYMENTSYNC, mnpayments.GetStorageLimit());
                // ask node for missing pieces only (old nodes will not be asked)
                mnpayments.RequestLowDataPaymentBlocks(pnode);

                ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

        }
    }
    // looped through all nodes, release them
    ReleaseNodeVector(vNodesCopy);
}

void CTnodeSync::UpdatedBlockTip(const CBlockIndex *pindex) {
    pCurrentBlockIndex = pindex;
}
bool CTnodeSync::IsSynced() {
    return nRequestedTnodeAssets == TNODE_SYNC_FINISHED;
}

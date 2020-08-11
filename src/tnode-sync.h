// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef TNODE_SYNC_H
#define TNODE_SYNC_H

#include "chain.h"
#include "net.h"

#include <univalue.h>

class CTnodeSync;

static const int TNODE_SYNC_FAILED          = -1;
static const int TNODE_SYNC_INITIAL         = 0;
static const int TNODE_SYNC_SPORKS          = 1;
static const int TNODE_SYNC_LIST            = 2;
static const int TNODE_SYNC_MNW             = 3;
static const int TNODE_SYNC_FINISHED        = 999;

static const int TNODE_SYNC_TICK_SECONDS    = 6;
static const int TNODE_SYNC_TIMEOUT_SECONDS = 30; // our blocks are 2.5 minutes so 30 seconds should be fine

static const int TNODE_SYNC_ENOUGH_PEERS    = 3;

extern CTnodeSync tnodeSync;

//
// CTnodeSync : Sync tnode assets in stages
//

class CTnodeSync
{
private:
    // Keep track of current asset
    int nRequestedTnodeAssets;
    // Count peers we've requested the asset from
    int nRequestedTnodeAttempt;

    // Time when current tnode asset sync started
    int64_t nTimeAssetSyncStarted;

    // Last time when we received some tnode asset ...
    int64_t nTimeLastTnodeList;
    int64_t nTimeLastPaymentVote;
    int64_t nTimeLastGovernanceItem;
    // ... or failed
    int64_t nTimeLastFailure;

    // How many times we failed
    int nCountFailures;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    bool CheckNodeHeight(CNode* pnode, bool fDisconnectStuckNodes = false);
    void Fail();
    void ClearFulfilledRequests();

public:
    CTnodeSync() { Reset(); }

    void AddedTnodeList() { nTimeLastTnodeList = GetTime(); }
    void AddedPaymentVote() { nTimeLastPaymentVote = GetTime(); }
    void AddedGovernanceItem() { nTimeLastGovernanceItem = GetTime(); };

    void SendGovernanceSyncRequest(CNode* pnode);

    bool IsFailed() { return nRequestedTnodeAssets == TNODE_SYNC_FAILED; }
    bool IsBlockchainSynced(bool fBlockAccepted = false);
    bool IsTnodeListSynced() { return nRequestedTnodeAssets > TNODE_SYNC_LIST; }
    bool IsWinnersListSynced() { return nRequestedTnodeAssets > TNODE_SYNC_MNW; }
    bool IsSynced() { return nRequestedTnodeAssets == TNODE_SYNC_FINISHED; }

    int GetAssetID() { return nRequestedTnodeAssets; }
    int GetAttempt() { return nRequestedTnodeAttempt; }
    std::string GetAssetName();
    std::string GetSyncStatus();

    void Reset();
    void SwitchToNextAsset();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void ProcessTick();

    void UpdatedBlockTip(const CBlockIndex *pindex);
};

#endif

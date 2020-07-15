#ifndef TNODESYNC_INTERFACE_H
#define TNODESYNC_INTERFACE_H

#include "masternode-sync.h"

/**
 * Class for getting sync status with either version of tnodes (legacy and evo)
 * This is temporary measure, remove it when transition to evo tnodes is done on mainnet
 */

class CTnodeSyncInterface {
private:
    // is it evo mode?
    bool fEvoTnodes;

public:
    CTnodeSyncInterface() : fEvoTnodes(false) {}

    bool IsFailed() { return GetAssetID() == TNODE_SYNC_FAILED; }
    bool IsBlockchainSynced();
    bool IsSynced();

    int GetAssetID();

    void Reset();
    void SwitchToNextAsset(CConnman& connman);

    std::string GetAssetName();
    std::string GetSyncStatus();

    void UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman);
};

extern CTnodeSyncInterface tnodeSyncInterface;

#endif
#include "tnodesync-interface.h"
#include "tnode-sync.h"

#include "evo/deterministicmns.h"

CTnodeSyncInterface tnodeSyncInterface;

void CTnodeSyncInterface::Reset()
{
    if (!fEvoTnodes)
        tnodeSync.Reset();
    masternodeSync.Reset();
}

int CTnodeSyncInterface::GetAssetID()
{
    return fEvoTnodes ? masternodeSync.GetAssetID() : tnodeSync.GetAssetID();
}

bool CTnodeSyncInterface::IsBlockchainSynced() {
    return fEvoTnodes ? masternodeSync.IsBlockchainSynced() : tnodeSync.IsBlockchainSynced();
}

bool CTnodeSyncInterface::IsSynced() {
    return fEvoTnodes ? masternodeSync.IsSynced() : tnodeSync.IsSynced();
}

void CTnodeSyncInterface::UpdatedBlockTip(const CBlockIndex * /*pindexNew*/, bool /*fInitialDownload*/, CConnman & /*connman*/)
{
    fEvoTnodes = deterministicMNManager->IsDIP3Enforced();
}

void CTnodeSyncInterface::SwitchToNextAsset(CConnman &connman)
{
    fEvoTnodes ? masternodeSync.SwitchToNextAsset(connman) : tnodeSync.SwitchToNextAsset();
}

std::string CTnodeSyncInterface::GetAssetName()
{
    return fEvoTnodes ? masternodeSync.GetAssetName() : tnodeSync.GetAssetName();
}

std::string CTnodeSyncInterface::GetSyncStatus()
{
    return fEvoTnodes ? masternodeSync.GetSyncStatus() : tnodeSync.GetSyncStatus();
}
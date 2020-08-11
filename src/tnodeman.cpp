// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activetnode.h"
#include "addrman.h"
//#include "governance.h"
#include "tnode-payments.h"
#include "tnode-sync.h"
#include "tnodeman.h"
#include "netfulfilledman.h"
#include "darksend.h"
#include "netmessagemaker.h"
#include "net.h"
#include "net_processing.h"
#include "util.h"
#include "txmempool.h"

#define cs_vNodes (g_connman->cs_vNodes)
#define vNodes (g_connman->vNodes)

/**
 * PRNG initialized from secure entropy based RNG
 */
class InsecureRand
{
private:
    uint32_t nRz;
    uint32_t nRw;
    bool fDeterministic;

public:
    InsecureRand(bool _fDeterministic = false);

    /**
     * MWC RNG of George Marsaglia
     * This is intended to be fast. It has a period of 2^59.3, though the
     * least significant 16 bits only have a period of about 2^30.1.
     *
     * @return random value < nMax
     */
    int64_t operator()(int64_t nMax)
    {
        nRz = 36969 * (nRz & 65535) + (nRz >> 16);
        nRw = 18000 * (nRw & 65535) + (nRw >> 16);
        return ((nRw << 16) + nRz) % nMax;
    }
};

InsecureRand::InsecureRand(bool _fDeterministic)
        : nRz(11),
          nRw(11),
          fDeterministic(_fDeterministic)
{
    // The seed values have some unlikely fixed points which we avoid.
    if(fDeterministic) return;
    uint32_t nTmp;
    do {
        GetRandBytes((unsigned char*)&nTmp, 4);
    } while (nTmp == 0 || nTmp == 0x9068ffffU);
    nRz = nTmp;
    do {
        GetRandBytes((unsigned char*)&nTmp, 4);
    } while (nTmp == 0 || nTmp == 0x464fffffU);
    nRw = nTmp;
}

/** Tnode manager */
CTnodeMan mnodeman;

const std::string CTnodeMan::SERIALIZATION_VERSION_STRING = "CTnodeMan-Version-4";

struct CompareLastPaidBlock
{
    bool operator()(const std::pair<int, CTnode*>& t1,
                    const std::pair<int, CTnode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

struct CompareScoreMN
{
    bool operator()(const std::pair<int64_t, CTnode*>& t1,
                    const std::pair<int64_t, CTnode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

CTnodeIndex::CTnodeIndex()
    : nSize(0),
      mapIndex(),
      mapReverseIndex()
{}

bool CTnodeIndex::Get(int nIndex, CTxIn& vinTnode) const
{
    rindex_m_cit it = mapReverseIndex.find(nIndex);
    if(it == mapReverseIndex.end()) {
        return false;
    }
    vinTnode = it->second;
    return true;
}

int CTnodeIndex::GetTnodeIndex(const CTxIn& vinTnode) const
{
    index_m_cit it = mapIndex.find(vinTnode);
    if(it == mapIndex.end()) {
        return -1;
    }
    return it->second;
}

void CTnodeIndex::AddTnodeVIN(const CTxIn& vinTnode)
{
    index_m_it it = mapIndex.find(vinTnode);
    if(it != mapIndex.end()) {
        return;
    }
    int nNextIndex = nSize;
    mapIndex[vinTnode] = nNextIndex;
    mapReverseIndex[nNextIndex] = vinTnode;
    ++nSize;
}

void CTnodeIndex::Clear()
{
    mapIndex.clear();
    mapReverseIndex.clear();
    nSize = 0;
}
struct CompareByAddr

{
    bool operator()(const CTnode* t1,
                    const CTnode* t2) const
    {
        return t1->addr < t2->addr;
    }
};

void CTnodeIndex::RebuildIndex()
{
    nSize = mapIndex.size();
    for(index_m_it it = mapIndex.begin(); it != mapIndex.end(); ++it) {
        mapReverseIndex[it->second] = it->first;
    }
}

CTnodeMan::CTnodeMan() : cs(),
  vTnodes(),
  mAskedUsForTnodeList(),
  mWeAskedForTnodeList(),
  mWeAskedForTnodeListEntry(),
  mWeAskedForVerification(),
  mMnbRecoveryRequests(),
  mMnbRecoveryGoodReplies(),
  listScheduledMnbRequestConnections(),
  nLastIndexRebuildTime(0),
  indexTnodes(),
  indexTnodesOld(),
  fIndexRebuilt(false),
  fTnodesAdded(false),
  fTnodesRemoved(false),
//  vecDirtyGovernanceObjectHashes(),
  nLastWatchdogVoteTime(0),
  mapSeenTnodeBroadcast(),
  mapSeenTnodePing(),
  nDsqCount(0)
{}

bool CTnodeMan::Add(CTnode &mn)
{
    LOCK(cs);

    CTnode *pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("tnode", "CTnodeMan::Add -- Adding new Tnode: addr=%s, %i now\n", mn.addr.ToString(), size() + 1);
        vTnodes.push_back(mn);
        indexTnodes.AddTnodeVIN(mn.vin);
        fTnodesAdded = true;
        return true;
    }

    return false;
}

void CTnodeMan::AskForMN(CNode* pnode, const CTxIn &vin)
{
    if(!pnode) return;

    LOCK(cs);

    std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it1 = mWeAskedForTnodeListEntry.find(vin.prevout);
    if (it1 != mWeAskedForTnodeListEntry.end()) {
        std::map<CNetAddr, int64_t>::iterator it2 = it1->second.find(pnode->addr);
        if (it2 != it1->second.end()) {
            if (GetTime() < it2->second) {
                // we've asked recently, should not repeat too often or we could get banned
                return;
            }
            // we asked this node for this outpoint but it's ok to ask again already
            LogPrintf("CTnodeMan::AskForMN -- Asking same peer %s for missing tnode entry again: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
        } else {
            // we already asked for this outpoint but not this node
            LogPrintf("CTnodeMan::AskForMN -- Asking new peer %s for missing tnode entry: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
        }
    } else {
        // we never asked any node for this outpoint
        LogPrintf("CTnodeMan::AskForMN -- Asking peer %s for missing tnode entry for the first time: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
    }
    mWeAskedForTnodeListEntry[vin.prevout][pnode->addr] = GetTime() + DSEG_UPDATE_SECONDS;

    g_connman->PushMessage(pnode, CNetMsgMaker(LEGACY_TNODES_PROTOCOL_VERSION).Make(NetMsgType::DSEG, vin));
}

void CTnodeMan::Check()
{
    LOCK(cs);

//    LogPrint("tnode", "CTnodeMan::Check -- nLastWatchdogVoteTime=%d, IsWatchdogActive()=%d\n", nLastWatchdogVoteTime, IsWatchdogActive());

    BOOST_FOREACH(CTnode& mn, vTnodes) {
        mn.Check();
    }
}

void CTnodeMan::CheckAndRemove()
{
    if(!tnodeSync.IsTnodeListSynced()) return;

    LogPrintf("CTnodeMan::CheckAndRemove\n");

    {
        // Need LOCK2 here to ensure consistent locking order because code below locks cs_main
        // in CheckMnbAndUpdateTnodeList()
        LOCK2(cs_main, cs);

        Check();

        // Remove spent tnodes, prepare structures and make requests to reasure the state of inactive ones
        std::vector<CTnode>::iterator it = vTnodes.begin();
        std::vector<std::pair<int, CTnode> > vecTnodeRanks;
        // ask for up to MNB_RECOVERY_MAX_ASK_ENTRIES tnode entries at a time
        int nAskForMnbRecovery = MNB_RECOVERY_MAX_ASK_ENTRIES;
        while(it != vTnodes.end()) {
            CTnodeBroadcast mnb = CTnodeBroadcast(*it);
            uint256 hash = mnb.GetHash();
            // If collateral was spent ...
            if ((*it).IsOutpointSpent()) {
                LogPrint("tnode", "CTnodeMan::CheckAndRemove -- Removing Tnode: %s  addr=%s  %i now\n", (*it).GetStateString(), (*it).addr.ToString(), size() - 1);

                // erase all of the broadcasts we've seen from this txin, ...
                mapSeenTnodeBroadcast.erase(hash);
                mWeAskedForTnodeListEntry.erase((*it).vin.prevout);

                // and finally remove it from the list
//                it->FlagGovernanceItemsAsDirty();
                it = vTnodes.erase(it);
                fTnodesRemoved = true;
            } else {
                bool fAsk = pCurrentBlockIndex &&
                            (nAskForMnbRecovery > 0) &&
                            tnodeSync.IsSynced() &&
                            it->IsNewStartRequired() &&
                            !IsMnbRecoveryRequested(hash);
                if(fAsk) {
                    // this mn is in a non-recoverable state and we haven't asked other nodes yet
                    std::set<CNetAddr> setRequested;
                    // calulate only once and only when it's needed
                    if(vecTnodeRanks.empty()) {
                        int nRandomBlockHeight = GetRandInt(pCurrentBlockIndex->nHeight);
                        vecTnodeRanks = GetTnodeRanks(nRandomBlockHeight);
                    }
                    bool fAskedForMnbRecovery = false;
                    // ask first MNB_RECOVERY_QUORUM_TOTAL tnodes we can connect to and we haven't asked recently
                    for(int i = 0; setRequested.size() < MNB_RECOVERY_QUORUM_TOTAL && i < (int)vecTnodeRanks.size(); i++) {
                        // avoid banning
                        if(mWeAskedForTnodeListEntry.count(it->vin.prevout) && mWeAskedForTnodeListEntry[it->vin.prevout].count(vecTnodeRanks[i].second.addr)) continue;
                        // didn't ask recently, ok to ask now
                        CService addr = vecTnodeRanks[i].second.addr;
                        setRequested.insert(addr);
                        listScheduledMnbRequestConnections.push_back(std::make_pair(addr, hash));
                        fAskedForMnbRecovery = true;
                    }
                    if(fAskedForMnbRecovery) {
                        LogPrint("tnode", "CTnodeMan::CheckAndRemove -- Recovery initiated, tnode=%s\n", it->vin.prevout.ToStringShort());
                        nAskForMnbRecovery--;
                    }
                    // wait for mnb recovery replies for MNB_RECOVERY_WAIT_SECONDS seconds
                    mMnbRecoveryRequests[hash] = std::make_pair(GetTime() + MNB_RECOVERY_WAIT_SECONDS, setRequested);
                }
                ++it;
            }
        }

        // proces replies for TNODE_NEW_START_REQUIRED tnodes
        LogPrint("tnode", "CTnodeMan::CheckAndRemove -- mMnbRecoveryGoodReplies size=%d\n", (int)mMnbRecoveryGoodReplies.size());
        std::map<uint256, std::vector<CTnodeBroadcast> >::iterator itMnbReplies = mMnbRecoveryGoodReplies.begin();
        while(itMnbReplies != mMnbRecoveryGoodReplies.end()){
            if(mMnbRecoveryRequests[itMnbReplies->first].first < GetTime()) {
                // all nodes we asked should have replied now
                if(itMnbReplies->second.size() >= MNB_RECOVERY_QUORUM_REQUIRED) {
                    // majority of nodes we asked agrees that this mn doesn't require new mnb, reprocess one of new mnbs
                    LogPrint("tnode", "CTnodeMan::CheckAndRemove -- reprocessing mnb, tnode=%s\n", itMnbReplies->second[0].vin.prevout.ToStringShort());
                    // mapSeenTnodeBroadcast.erase(itMnbReplies->first);
                    int nDos;
                    itMnbReplies->second[0].fRecovery = true;
                    CheckMnbAndUpdateTnodeList(NULL, itMnbReplies->second[0], nDos);
                }
                LogPrint("tnode", "CTnodeMan::CheckAndRemove -- removing mnb recovery reply, tnode=%s, size=%d\n", itMnbReplies->second[0].vin.prevout.ToStringShort(), (int)itMnbReplies->second.size());
                mMnbRecoveryGoodReplies.erase(itMnbReplies++);
            } else {
                ++itMnbReplies;
            }
        }
    }
    {
        // no need for cm_main below
        LOCK2(cs_main, cs);

        std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > >::iterator itMnbRequest = mMnbRecoveryRequests.begin();
        while(itMnbRequest != mMnbRecoveryRequests.end()){
            // Allow this mnb to be re-verified again after MNB_RECOVERY_RETRY_SECONDS seconds
            // if mn is still in TNODE_NEW_START_REQUIRED state.
            if(GetTime() - itMnbRequest->second.first > MNB_RECOVERY_RETRY_SECONDS) {
                mMnbRecoveryRequests.erase(itMnbRequest++);
            } else {
                ++itMnbRequest;
            }
        }

        // check who's asked for the Tnode list
        std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForTnodeList.begin();
        while(it1 != mAskedUsForTnodeList.end()){
            if((*it1).second < GetTime()) {
                mAskedUsForTnodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check who we asked for the Tnode list
        it1 = mWeAskedForTnodeList.begin();
        while(it1 != mWeAskedForTnodeList.end()){
            if((*it1).second < GetTime()){
                mWeAskedForTnodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check which Tnodes we've asked for
        std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it2 = mWeAskedForTnodeListEntry.begin();
        while(it2 != mWeAskedForTnodeListEntry.end()){
            std::map<CNetAddr, int64_t>::iterator it3 = it2->second.begin();
            while(it3 != it2->second.end()){
                if(it3->second < GetTime()){
                    it2->second.erase(it3++);
                } else {
                    ++it3;
                }
            }
            if(it2->second.empty()) {
                mWeAskedForTnodeListEntry.erase(it2++);
            } else {
                ++it2;
            }
        }

        std::map<CNetAddr, CTnodeVerification>::iterator it3 = mWeAskedForVerification.begin();
        while(it3 != mWeAskedForVerification.end()){
            if(it3->second.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS) {
                mWeAskedForVerification.erase(it3++);
            } else {
                ++it3;
            }
        }

        // NOTE: do not expire mapSeenTnodeBroadcast entries here, clean them on mnb updates!

        // remove expired mapSeenTnodePing
        std::map<uint256, CTnodePing>::iterator it4 = mapSeenTnodePing.begin();
        while(it4 != mapSeenTnodePing.end()){
            if((*it4).second.IsExpired()) {
                LogPrint("tnode", "CTnodeMan::CheckAndRemove -- Removing expired Tnode ping: hash=%s\n", (*it4).second.GetHash().ToString());
                mapSeenTnodePing.erase(it4++);
            } else {
                ++it4;
            }
        }

        // remove expired mapSeenTnodeVerification
        std::map<uint256, CTnodeVerification>::iterator itv2 = mapSeenTnodeVerification.begin();
        while(itv2 != mapSeenTnodeVerification.end()){
            if((*itv2).second.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS){
                LogPrint("tnode", "CTnodeMan::CheckAndRemove -- Removing expired Tnode verification: hash=%s\n", (*itv2).first.ToString());
                mapSeenTnodeVerification.erase(itv2++);
            } else {
                ++itv2;
            }
        }

        LogPrintf("CTnodeMan::CheckAndRemove -- %s\n", ToString());

        if(fTnodesRemoved) {
            CheckAndRebuildTnodeIndex();
        }
    }

    if(fTnodesRemoved) {
        NotifyTnodeUpdates();
    }
}

void CTnodeMan::Clear()
{
    LOCK(cs);
    vTnodes.clear();
    mAskedUsForTnodeList.clear();
    mWeAskedForTnodeList.clear();
    mWeAskedForTnodeListEntry.clear();
    mapSeenTnodeBroadcast.clear();
    mapSeenTnodePing.clear();
    nDsqCount = 0;
    nLastWatchdogVoteTime = 0;
    indexTnodes.Clear();
    indexTnodesOld.Clear();
}

int CTnodeMan::CountTnodes(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? tnpayments.GetMinTnodePaymentsProto() : nProtocolVersion;

    BOOST_FOREACH(CTnode& mn, vTnodes) {
        if(mn.nProtocolVersion < nProtocolVersion) continue;
        nCount++;
    }

    return nCount;
}

int CTnodeMan::CountEnabled(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? tnpayments.GetMinTnodePaymentsProto() : nProtocolVersion;

    BOOST_FOREACH(CTnode& mn, vTnodes) {
        if(mn.nProtocolVersion < nProtocolVersion || !mn.IsEnabled()) continue;
        nCount++;
    }

    return nCount;
}

/* Only IPv4 tnodes are allowed in 12.1, saving this for later
int CTnodeMan::CountByIP(int nNetworkType)
{
    LOCK(cs);
    int nNodeCount = 0;

    BOOST_FOREACH(CTnode& mn, vTnodes)
        if ((nNetworkType == NET_IPV4 && mn.addr.IsIPv4()) ||
            (nNetworkType == NET_TOR  && mn.addr.IsTor())  ||
            (nNetworkType == NET_IPV6 && mn.addr.IsIPv6())) {
                nNodeCount++;
        }

    return nNodeCount;
}
*/

void CTnodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForTnodeList.find(pnode->addr);
            if(it != mWeAskedForTnodeList.end() && GetTime() < (*it).second) {
                LogPrintf("CTnodeMan::DsegUpdate -- we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                return;
            }
        }
    }
    
    g_connman->PushMessage(pnode, CNetMsgMaker(LEGACY_TNODES_PROTOCOL_VERSION).Make(NetMsgType::DSEG, CTxIn()));
    int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
    mWeAskedForTnodeList[pnode->addr] = askAgain;

    LogPrint("tnode", "CTnodeMan::DsegUpdate -- asked %s for the list\n", pnode->addr.ToString());
}

CTnode* CTnodeMan::Find(const std::string &txHash, const std::string &outputIndex)
{
    LOCK(cs);

    BOOST_FOREACH(CTnode& mn, vTnodes)
    {
        COutPoint outpoint = mn.vin.prevout;

        if(txHash==outpoint.hash.ToString().substr(0,64) &&
           outputIndex==to_string(outpoint.n))
            return &mn;
    }
    return NULL;
}

CTnode* CTnodeMan::Find(const CScript &payee)
{
    LOCK(cs);

    BOOST_FOREACH(CTnode& mn, vTnodes)
    {
        if(GetScriptForDestination(mn.pubKeyCollateralAddress.GetID()) == payee)
            return &mn;
    }
    return NULL;
}

CTnode* CTnodeMan::Find(const CTxIn &vin)
{
    LOCK(cs);

    BOOST_FOREACH(CTnode& mn, vTnodes)
    {
        if(mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}

CTnode* CTnodeMan::Find(const CPubKey &pubKeyTnode)
{
    LOCK(cs);

    BOOST_FOREACH(CTnode& mn, vTnodes)
    {
        if(mn.pubKeyTnode == pubKeyTnode)
            return &mn;
    }
    return NULL;
}

bool CTnodeMan::Get(const CPubKey& pubKeyTnode, CTnode& tnode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CTnode* pMN = Find(pubKeyTnode);
    if(!pMN)  {
        return false;
    }
    tnode = *pMN;
    return true;
}

bool CTnodeMan::Get(const CTxIn& vin, CTnode& tnode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CTnode* pMN = Find(vin);
    if(!pMN)  {
        return false;
    }
    tnode = *pMN;
    return true;
}

tnode_info_t CTnodeMan::GetTnodeInfo(const CTxIn& vin)
{
    tnode_info_t info;
    LOCK(cs);
    CTnode* pMN = Find(vin);
    if(!pMN)  {
        return info;
    }
    info = pMN->GetInfo();
    return info;
}

tnode_info_t CTnodeMan::GetTnodeInfo(const CPubKey& pubKeyTnode)
{
    tnode_info_t info;
    LOCK(cs);
    CTnode* pMN = Find(pubKeyTnode);
    if(!pMN)  {
        return info;
    }
    info = pMN->GetInfo();
    return info;
}

bool CTnodeMan::Has(const CTxIn& vin)
{
    LOCK(cs);
    CTnode* pMN = Find(vin);
    return (pMN != NULL);
}

char* CTnodeMan::GetNotQualifyReason(CTnode& mn, int nBlockHeight, bool fFilterSigTime, int nMnCount)
{
    if (!mn.IsValidForPayment()) {
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'not valid for payment'");
        return reasonStr;
    }
    // //check protocol version
    if (mn.nProtocolVersion < tnpayments.GetMinTnodePaymentsProto()) {
        // LogPrintf("Invalid nProtocolVersion!\n");
        // LogPrintf("mn.nProtocolVersion=%s!\n", mn.nProtocolVersion);
        // LogPrintf("tnpayments.GetMinTnodePaymentsProto=%s!\n", tnpayments.GetMinTnodePaymentsProto());
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'Invalid nProtocolVersion', nProtocolVersion=%d", mn.nProtocolVersion);
        return reasonStr;
    }
    //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
    if (tnpayments.IsScheduled(mn, nBlockHeight)) {
        // LogPrintf("tnpayments.IsScheduled!\n");
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'is scheduled'");
        return reasonStr;
    }
    //it's too new, wait for a cycle
    if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) {
        // LogPrintf("it's too new, wait for a cycle!\n");
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'too new', sigTime=%s, will be qualifed after=%s",
                DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime + (nMnCount * 2.6 * 60)).c_str());
        return reasonStr;
    }
    //make sure it has at least as many confirmations as there are tnodes
    if (mn.GetCollateralAge() < nMnCount) {
        // LogPrintf("mn.GetCollateralAge()=%s!\n", mn.GetCollateralAge());
        // LogPrintf("nMnCount=%s!\n", nMnCount);
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'collateralAge < znCount', collateralAge=%d, znCount=%d", mn.GetCollateralAge(), nMnCount);
        return reasonStr;
    }
    return NULL;
}

//
// Deterministically select the oldest/best tnode to pay on the network
//
CTnode* CTnodeMan::GetNextTnodeInQueueForPayment(bool fFilterSigTime, int& nCount)
{
    if(!pCurrentBlockIndex) {
        nCount = 0;
        return NULL;
    }
    return GetNextTnodeInQueueForPayment(pCurrentBlockIndex->nHeight, fFilterSigTime, nCount);
}

CTnode* CTnodeMan::GetNextTnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    // Need LOCK2 here to ensure consistent locking order because the GetBlockHash call below locks cs_main
    LOCK2(cs_main, mempool.cs);
    LOCK(cs);

    CTnode *pBestTnode = NULL;
    std::vector<std::pair<int, CTnode*> > vecTnodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */
    int nMnCount = CountEnabled();
    int index = 0;
    BOOST_FOREACH(CTnode &mn, vTnodes)
    {
        index += 1;
        // LogPrintf("index=%s, mn=%s\n", index, mn.ToString());
        /*if (!mn.IsValidForPayment()) {
            LogPrint("tnodeman", "Tnode, %s, addr(%s), not-qualified: 'not valid for payment'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        // //check protocol version
        if (mn.nProtocolVersion < tnpayments.GetMinTnodePaymentsProto()) {
            // LogPrintf("Invalid nProtocolVersion!\n");
            // LogPrintf("mn.nProtocolVersion=%s!\n", mn.nProtocolVersion);
            // LogPrintf("tnpayments.GetMinTnodePaymentsProto=%s!\n", tnpayments.GetMinTnodePaymentsProto());
            LogPrint("tnodeman", "Tnode, %s, addr(%s), not-qualified: 'invalid nProtocolVersion'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (tnpayments.IsScheduled(mn, nBlockHeight)) {
            // LogPrintf("tnpayments.IsScheduled!\n");
            LogPrint("tnodeman", "Tnode, %s, addr(%s), not-qualified: 'IsScheduled'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) {
            // LogPrintf("it's too new, wait for a cycle!\n");
            LogPrint("tnodeman", "Tnode, %s, addr(%s), not-qualified: 'it's too new, wait for a cycle!', sigTime=%s, will be qualifed after=%s\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime + (nMnCount * 2.6 * 60)).c_str());
            continue;
        }
        //make sure it has at least as many confirmations as there are tnodes
        if (mn.GetCollateralAge() < nMnCount) {
            // LogPrintf("mn.GetCollateralAge()=%s!\n", mn.GetCollateralAge());
            // LogPrintf("nMnCount=%s!\n", nMnCount);
            LogPrint("tnodeman", "Tnode, %s, addr(%s), not-qualified: 'mn.GetCollateralAge() < nMnCount', CollateralAge=%d, nMnCount=%d\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), mn.GetCollateralAge(), nMnCount);
            continue;
        }*/
        char* reasonStr = GetNotQualifyReason(mn, nBlockHeight, fFilterSigTime & (Params().NetworkIDString() != CBaseChainParams::REGTEST), nMnCount);
        if (reasonStr != NULL) {
            LogPrint("tnodeman", "Tnode, %s, addr(%s), qualify %s\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), reasonStr);
            delete [] reasonStr;
            continue;
        }
        vecTnodeLastPaid.push_back(std::make_pair(mn.GetLastPaidBlock(), &mn));
    }
    nCount = (int)vecTnodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCount < nMnCount / 3) {
        // LogPrintf("Need Return, nCount=%s, nMnCount/3=%s\n", nCount, nMnCount/3);
        return GetNextTnodeInQueueForPayment(nBlockHeight, false, nCount);
    }

    // Sort them low to high
    sort(vecTnodeLastPaid.begin(), vecTnodeLastPaid.end(), CompareLastPaidBlock());

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight - 101)) {
        LogPrintf("CTnode::GetNextTnodeInQueueForPayment -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight - 101);
        return NULL;
    }
    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = nMnCount/10;
    int nCountTenth = 0;
    arith_uint256 nHighest = 0;
    BOOST_FOREACH (PAIRTYPE(int, CTnode*)& s, vecTnodeLastPaid){
        arith_uint256 nScore = s.second->CalculateScore(blockHash);
        if(nScore > nHighest){
            nHighest = nScore;
            pBestTnode = s.second;
        }
        nCountTenth++;
        if(nCountTenth >= nTenthNetwork) break;
    }
    return pBestTnode;
}

CTnode* CTnodeMan::FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion)
{
    LOCK(cs);

    nProtocolVersion = nProtocolVersion == -1 ? tnpayments.GetMinTnodePaymentsProto() : nProtocolVersion;

    int nCountEnabled = CountEnabled(nProtocolVersion);
    int nCountNotExcluded = nCountEnabled - vecToExclude.size();

    LogPrintf("CTnodeMan::FindRandomNotInVec -- %d enabled tnodes, %d tnodes to choose from\n", nCountEnabled, nCountNotExcluded);
    if(nCountNotExcluded < 1) return NULL;

    // fill a vector of pointers
    std::vector<CTnode*> vpTnodesShuffled;
    BOOST_FOREACH(CTnode &mn, vTnodes) {
        vpTnodesShuffled.push_back(&mn);
    }

    InsecureRand insecureRand;
    // shuffle pointers
    std::random_shuffle(vpTnodesShuffled.begin(), vpTnodesShuffled.end(), insecureRand);
    bool fExclude;

    // loop through
    BOOST_FOREACH(CTnode* pmn, vpTnodesShuffled) {
        if(pmn->nProtocolVersion < nProtocolVersion || !pmn->IsEnabled()) continue;
        fExclude = false;
        BOOST_FOREACH(const CTxIn &txinToExclude, vecToExclude) {
            if(pmn->vin.prevout == txinToExclude.prevout) {
                fExclude = true;
                break;
            }
        }
        if(fExclude) continue;
        // found the one not in vecToExclude
        LogPrint("tnode", "CTnodeMan::FindRandomNotInVec -- found, tnode=%s\n", pmn->vin.prevout.ToStringShort());
        return pmn;
    }

    LogPrint("tnode", "CTnodeMan::FindRandomNotInVec -- failed\n");
    return NULL;
}

int CTnodeMan::GetTnodeRank(const CTxIn& vin, int nBlockHeight, int nMinProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CTnode*> > vecTnodeScores;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return -1;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CTnode& mn, vTnodes) {
        if(mn.nProtocolVersion < nMinProtocol) continue;
        if(fOnlyActive) {
            if(!mn.IsEnabled()) continue;
        }
        else {
            if(!mn.IsValidForPayment()) continue;
        }
        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecTnodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecTnodeScores.rbegin(), vecTnodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTnode*)& scorePair, vecTnodeScores) {
        nRank++;
        if(scorePair.second->vin.prevout == vin.prevout) return nRank;
    }

    return -1;
}

std::vector<std::pair<int, CTnode> > CTnodeMan::GetTnodeRanks(int nBlockHeight, int nMinProtocol)
{
    std::vector<std::pair<int64_t, CTnode*> > vecTnodeScores;
    std::vector<std::pair<int, CTnode> > vecTnodeRanks;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return vecTnodeRanks;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CTnode& mn, vTnodes) {

        if(mn.nProtocolVersion < nMinProtocol || !mn.IsEnabled()) continue;

        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecTnodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecTnodeScores.rbegin(), vecTnodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTnode*)& s, vecTnodeScores) {
        nRank++;
        vecTnodeRanks.push_back(std::make_pair(nRank, *s.second));
    }

    return vecTnodeRanks;
}

CTnode* CTnodeMan::GetTnodeByRank(int nRank, int nBlockHeight, int nMinProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CTnode*> > vecTnodeScores;

    LOCK(cs);

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight)) {
        LogPrintf("CTnode::GetTnodeByRank -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight);
        return NULL;
    }

    // Fill scores
    BOOST_FOREACH(CTnode& mn, vTnodes) {

        if(mn.nProtocolVersion < nMinProtocol) continue;
        if(fOnlyActive && !mn.IsEnabled()) continue;

        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecTnodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecTnodeScores.rbegin(), vecTnodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTnode*)& s, vecTnodeScores){
        rank++;
        if(rank == nRank) {
            return s.second;
        }
    }

    return NULL;
}

void CTnodeMan::ProcessTnodeConnections()
{
    //we don't care about this for regtest
    if(Params().NetworkIDString() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes) {
        if(pnode->fTnode) {
            if(darkSendPool.pSubmittedToTnode != NULL && pnode->addr == darkSendPool.pSubmittedToTnode->addr) continue;
            // LogPrintf("Closing Tnode connection: peer=%d, addr=%s\n", pnode->id, pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    }
}

std::pair<CService, std::set<uint256> > CTnodeMan::PopScheduledMnbRequestConnection()
{
    LOCK(cs);
    if(listScheduledMnbRequestConnections.empty()) {
        return std::make_pair(CService(), std::set<uint256>());
    }

    std::set<uint256> setResult;

    listScheduledMnbRequestConnections.sort();
    std::pair<CService, uint256> pairFront = listScheduledMnbRequestConnections.front();

    // squash hashes from requests with the same CService as the first one into setResult
    std::list< std::pair<CService, uint256> >::iterator it = listScheduledMnbRequestConnections.begin();
    while(it != listScheduledMnbRequestConnections.end()) {
        if(pairFront.first == it->first) {
            setResult.insert(it->second);
            it = listScheduledMnbRequestConnections.erase(it);
        } else {
            // since list is sorted now, we can be sure that there is no more hashes left
            // to ask for from this addr
            break;
        }
    }
    return std::make_pair(pairFront.first, setResult);
}


void CTnodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{

//    LogPrint("tnode", "CTnodeMan::ProcessMessage, strCommand=%s\n", strCommand);
    if(fLiteMode) return; // disable all Zcoin specific functionality
    if(!tnodeSync.IsBlockchainSynced()) return;

    if (strCommand == NetMsgType::MNANNOUNCE) { //Tnode Broadcast
        CTnodeBroadcast mnb;
        vRecv >> mnb;

        pfrom->setAskFor.erase(mnb.GetHash());

        LogPrintf("MNANNOUNCE -- Tnode announce, tnode=%s\n", mnb.vin.prevout.ToStringShort());

        int nDos = 0;

        if (CheckMnbAndUpdateTnodeList(pfrom, mnb, nDos)) {
            // use announced Tnode as a peer
            g_connman->AddNewAddress(CAddress(mnb.addr, NODE_NETWORK), pfrom->addr, 2*60*60);
        } else if(nDos > 0) {
            Misbehaving(pfrom->GetId(), nDos);
        }

        if(fTnodesAdded) {
            NotifyTnodeUpdates();
        }
    } else if (strCommand == NetMsgType::MNPING) { //Tnode Ping

        CTnodePing mnp;
        vRecv >> mnp;

        uint256 nHash = mnp.GetHash();

        pfrom->setAskFor.erase(nHash);

        LogPrint("tnode", "MNPING -- Tnode ping, tnode=%s\n", mnp.vin.prevout.ToStringShort());

        // Need LOCK2 here to ensure consistent locking order because the CheckAndUpdate call below locks cs_main
        LOCK2(cs_main, cs);

        if(mapSeenTnodePing.count(nHash)) return; //seen
        mapSeenTnodePing.insert(std::make_pair(nHash, mnp));

        LogPrint("tnode", "MNPING -- Tnode ping, tnode=%s new\n", mnp.vin.prevout.ToStringShort());

        // see if we have this Tnode
        CTnode* pmn = mnodeman.Find(mnp.vin);

        // too late, new MNANNOUNCE is required
        if(pmn && pmn->IsNewStartRequired()) return;

        int nDos = 0;
        if(mnp.CheckAndUpdate(pmn, false, nDos)) return;

        if(nDos > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDos);
        } else if(pmn != NULL) {
            // nothing significant failed, mn is a known one too
            return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a tnode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == NetMsgType::DSEG) { //Get Tnode list or specific entry
        // Ignore such requests until we are fully synced.
        // We could start processing this after tnode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!tnodeSync.IsSynced()) return;

        CTxIn vin;
        vRecv >> vin;

        LogPrint("tnode", "DSEG -- Tnode list, tnode=%s\n", vin.prevout.ToStringShort());

        LOCK(cs);

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().NetworkIDString() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForTnodeList.find(pfrom->addr);
                if (i != mAskedUsForTnodeList.end()){
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrintf("DSEG -- peer already asked me for the list, peer=%d\n", pfrom->id);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
                mAskedUsForTnodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;

        BOOST_FOREACH(CTnode& mn, vTnodes) {
            if (vin != CTxIn() && vin != mn.vin) continue; // asked for specific vin but we are not there yet
            if (Params().NetworkIDString() != CBaseChainParams::REGTEST)
                if (mn.addr.IsRFC1918() || mn.addr.IsLocal()) continue; // do not send local network tnode
            if (mn.IsUpdateRequired()) continue; // do not send outdated tnodes

            LogPrint("tnode", "DSEG -- Sending Tnode entry: tnode=%s  addr=%s\n", mn.vin.prevout.ToStringShort(), mn.addr.ToString());
            CTnodeBroadcast mnb = CTnodeBroadcast(mn);
            uint256 hash = mnb.GetHash();
            pfrom->PushInventory(CInv(MSG_TNODE_ANNOUNCE, hash));
            pfrom->PushInventory(CInv(MSG_TNODE_PING, mn.lastPing.GetHash()));
            nInvCount++;

            if (!mapSeenTnodeBroadcast.count(hash)) {
                mapSeenTnodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));
            }

            if (vin == mn.vin) {
                LogPrintf("DSEG -- Sent 1 Tnode inv to peer %d\n", pfrom->id);
                return;
            }
        }

        if(vin == CTxIn()) {
            g_connman->PushMessage(pfrom, CNetMsgMaker(LEGACY_TNODES_PROTOCOL_VERSION).Make(NetMsgType::SYNCSTATUSCOUNT, TNODE_SYNC_LIST, nInvCount));
            LogPrintf("DSEG -- Sent %d Tnode invs to peer %d\n", nInvCount, pfrom->id);
            return;
        }
        // smth weird happen - someone asked us for vin we have no idea about?
        LogPrint("tnode", "DSEG -- No invs sent to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::MNVERIFY) { // Tnode Verify

        // Need LOCK2 here to ensure consistent locking order because the all functions below call GetBlockHash which locks cs_main
        LOCK2(cs_main, cs);

        CTnodeVerification mnv;
        vRecv >> mnv;

        if(mnv.vchSig1.empty()) {
            // CASE 1: someone asked me to verify myself /IP we are using/
            SendVerifyReply(pfrom, mnv);
        } else if (mnv.vchSig2.empty()) {
            // CASE 2: we _probably_ got verification we requested from some tnode
            ProcessVerifyReply(pfrom, mnv);
        } else {
            // CASE 3: we _probably_ got verification broadcast signed by some tnode which verified another one
            ProcessVerifyBroadcast(pfrom, mnv);
        }
    }
}

// Verification of tnodes via unique direct requests.

void CTnodeMan::DoFullVerificationStep()
{
    if(activeTnode.vin == CTxIn()) return;
    if(!tnodeSync.IsSynced()) return;

    std::vector<std::pair<int, CTnode> > vecTnodeRanks = GetTnodeRanks(pCurrentBlockIndex->nHeight - 1, MIN_POSE_PROTO_VERSION);

    std::vector<CAddress> vAddr;
    int nCount = 0;

    {
    LOCK2(cs_main, cs);

    int nMyRank = -1;
    int nRanksTotal = (int)vecTnodeRanks.size();

    // send verify requests only if we are in top MAX_POSE_RANK
    std::vector<std::pair<int, CTnode> >::iterator it = vecTnodeRanks.begin();
    while(it != vecTnodeRanks.end()) {
        if(it->first > MAX_POSE_RANK) {
            LogPrint("tnode", "CTnodeMan::DoFullVerificationStep -- Must be in top %d to send verify request\n",
                        (int)MAX_POSE_RANK);
            return;
        }
        if(it->second.vin == activeTnode.vin) {
            nMyRank = it->first;
            LogPrint("tnode", "CTnodeMan::DoFullVerificationStep -- Found self at rank %d/%d, verifying up to %d tnodes\n",
                        nMyRank, nRanksTotal, (int)MAX_POSE_CONNECTIONS);
            break;
        }
        ++it;
    }

    // edge case: list is too short and this tnode is not enabled
    if(nMyRank == -1) return;

    // send verify requests to up to MAX_POSE_CONNECTIONS tnodes
    // starting from MAX_POSE_RANK + nMyRank and using MAX_POSE_CONNECTIONS as a step
    int nOffset = MAX_POSE_RANK + nMyRank - 1;
    if(nOffset >= (int)vecTnodeRanks.size()) return;

    std::vector<CTnode*> vSortedByAddr;
    BOOST_FOREACH(CTnode& mn, vTnodes) {
        vSortedByAddr.push_back(&mn);
    }

    sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

    it = vecTnodeRanks.begin() + nOffset;
    while(it != vecTnodeRanks.end()) {
        if(it->second.IsPoSeVerified() || it->second.IsPoSeBanned()) {
            LogPrint("tnode", "CTnodeMan::DoFullVerificationStep -- Already %s%s%s tnode %s address %s, skipping...\n",
                        it->second.IsPoSeVerified() ? "verified" : "",
                        it->second.IsPoSeVerified() && it->second.IsPoSeBanned() ? " and " : "",
                        it->second.IsPoSeBanned() ? "banned" : "",
                        it->second.vin.prevout.ToStringShort(), it->second.addr.ToString());
            nOffset += MAX_POSE_CONNECTIONS;
            if(nOffset >= (int)vecTnodeRanks.size()) break;
            it += MAX_POSE_CONNECTIONS;
            continue;
        }
        LogPrint("tnode", "CTnodeMan::DoFullVerificationStep -- Verifying tnode %s rank %d/%d address %s\n",
                    it->second.vin.prevout.ToStringShort(), it->first, nRanksTotal, it->second.addr.ToString());
        CAddress addr = CAddress(it->second.addr, NODE_NETWORK);
        if(CheckVerifyRequestAddr(addr, *g_connman)) {
            vAddr.push_back(addr);
            if((int)vAddr.size() >= MAX_POSE_CONNECTIONS) break;
        }
        nOffset += MAX_POSE_CONNECTIONS;
        if(nOffset >= (int)vecTnodeRanks.size()) break;
        it += MAX_POSE_CONNECTIONS;
    }

    } // LOCK2(cs_main, cs)

    for (const auto& addr : vAddr) {
        PrepareVerifyRequest(addr, *g_connman);
    }

    LogPrint("tnode", "CTnodeMan::DoFullVerificationStep -- Sent verification requests to %d tnodes\n", nCount);
}

// This function tries to find tnodes with the same addr,
// find a verified one and ban all the other. If there are many nodes
// with the same addr but none of them is verified yet, then none of them are banned.
// It could take many times to run this before most of the duplicate nodes are banned.

void CTnodeMan::CheckSameAddr()
{
    if(!tnodeSync.IsSynced() || vTnodes.empty()) return;

    std::vector<CTnode*> vBan;
    std::vector<CTnode*> vSortedByAddr;

    {
        LOCK(cs);

        CTnode* pprevTnode = NULL;
        CTnode* pverifiedTnode = NULL;

        BOOST_FOREACH(CTnode& mn, vTnodes) {
            vSortedByAddr.push_back(&mn);
        }

        sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

        BOOST_FOREACH(CTnode* pmn, vSortedByAddr) {
            // check only (pre)enabled tnodes
            if(!pmn->IsEnabled() && !pmn->IsPreEnabled()) continue;
            // initial step
            if(!pprevTnode) {
                pprevTnode = pmn;
                pverifiedTnode = pmn->IsPoSeVerified() ? pmn : NULL;
                continue;
            }
            // second+ step
            if(pmn->addr == pprevTnode->addr) {
                if(pverifiedTnode) {
                    // another tnode with the same ip is verified, ban this one
                    vBan.push_back(pmn);
                } else if(pmn->IsPoSeVerified()) {
                    // this tnode with the same ip is verified, ban previous one
                    vBan.push_back(pprevTnode);
                    // and keep a reference to be able to ban following tnodes with the same ip
                    pverifiedTnode = pmn;
                }
            } else {
                pverifiedTnode = pmn->IsPoSeVerified() ? pmn : NULL;
            }
            pprevTnode = pmn;
        }
    }

    // ban duplicates
    BOOST_FOREACH(CTnode* pmn, vBan) {
        LogPrintf("CTnodeMan::CheckSameAddr -- increasing PoSe ban score for tnode %s\n", pmn->vin.prevout.ToStringShort());
        pmn->IncreasePoSeBanScore();
    }
}

bool CTnodeMan::CheckVerifyRequestAddr(const CAddress& addr, CConnman& connman)
{
    if(netfulfilledman.HasFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        // we already asked for verification, not a good idea to do this too often, skip it
        LogPrint("tnode", "CTnodeMan::%s -- too many requests, skipping... addr=%s\n", __func__, addr.ToString());
        return false;
    }

    return !connman.IsMasternodeOrDisconnectRequested(addr);
}

void CTnodeMan::PrepareVerifyRequest(const CAddress& addr, CConnman& connman)
{
    int nHeight;
    {
        LOCK(cs_main);
        nHeight = chainActive.Height();
    }

    connman.AddPendingMasternode(addr);
    // use random nonce, store it and require node to reply with correct one later
    CTnodeVerification mnv(addr, GetRandInt(999999), nHeight - 1);
    LOCK(cs_mapPendingMNV);
    mapPendingMNV.insert(std::make_pair(addr, std::make_pair(GetTime(), mnv)));
    LogPrintf("CTnodeMan::%s -- verifying node using nonce %d addr=%s\n", __func__, mnv.nonce, addr.ToString());
}

void CTnodeMan::ProcessPendingMnvRequests(CConnman& connman)
{
    LOCK(cs_mapPendingMNV);

    std::map<CService, std::pair<int64_t, CTnodeVerification> >::iterator itPendingMNV = mapPendingMNV.begin();

    while (itPendingMNV != mapPendingMNV.end()) {
        bool fDone = connman.ForNode(itPendingMNV->first, [&](CNode* pnode) {
            netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request");
            // use random nonce, store it and require node to reply with correct one later
            mWeAskedForVerification[pnode->addr] = itPendingMNV->second.second;
            LogPrint("tnode", "-- verifying node using nonce %d addr=%s\n", itPendingMNV->second.second.nonce, pnode->addr.ToString());
            CNetMsgMaker msgMaker(LEGACY_TNODES_PROTOCOL_VERSION);
            connman.PushMessage(pnode, msgMaker.Make(NetMsgType::MNVERIFY, itPendingMNV->second.second));
            return true;
        });

        int64_t nTimeAdded = itPendingMNV->second.first;
        if (fDone || (GetTime() - nTimeAdded > 15)) {
            if (!fDone) {
                LogPrint("tnode", "CTnodeMan::%s -- failed to connect to %s\n", __func__, itPendingMNV->first.ToString());
            }
            mapPendingMNV.erase(itPendingMNV++);
        } else {
            ++itPendingMNV;
        }
    }
}

void CTnodeMan::SendVerifyReply(CNode* pnode, CTnodeVerification& mnv)
{
    // only tnodes can sign this, why would someone ask regular node?
    if(!fTnodeMode) {
        // do not ban, malicious node might be using my IP
        // and trying to confuse the node which tries to verify it
        return;
    }

    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply")) {
//        // peer should not ask us that often
        LogPrintf("TnodeMan::SendVerifyReply -- ERROR: peer already asked me recently, peer=%d\n", pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        LogPrintf("TnodeMan::SendVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    std::string strMessage = strprintf("%s%d%s", activeTnode.service.ToString(), mnv.nonce, blockHash.ToString());

    if(!darkSendSigner.SignMessage(strMessage, mnv.vchSig1, activeTnode.keyTnode)) {
        LogPrintf("TnodeMan::SendVerifyReply -- SignMessage() failed\n");
        return;
    }

    std::string strError;

    if(!darkSendSigner.VerifyMessage(activeTnode.pubKeyTnode, mnv.vchSig1, strMessage, strError)) {
        LogPrintf("TnodeMan::SendVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
        return;
    }

    g_connman->PushMessage(pnode, CNetMsgMaker(LEGACY_TNODES_PROTOCOL_VERSION).Make(NetMsgType::MNVERIFY, mnv));
    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply");
}

void CTnodeMan::ProcessVerifyReply(CNode* pnode, CTnodeVerification& mnv)
{
    std::string strError;

    // did we even ask for it? if that's the case we should have matching fulfilled request
    if(!netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        LogPrintf("CTnodeMan::ProcessVerifyReply -- ERROR: we didn't ask for verification of %s, peer=%d\n", pnode->addr.ToString(), pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nonce for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nonce != mnv.nonce) {
        LogPrintf("CTnodeMan::ProcessVerifyReply -- ERROR: wrong nounce: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nonce, mnv.nonce, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nBlockHeight for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nBlockHeight != mnv.nBlockHeight) {
        LogPrintf("CTnodeMan::ProcessVerifyReply -- ERROR: wrong nBlockHeight: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nBlockHeight, mnv.nBlockHeight, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("TnodeMan::ProcessVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

//    // we already verified this address, why node is spamming?
    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done")) {
        LogPrintf("CTnodeMan::ProcessVerifyReply -- ERROR: already verified %s recently\n", pnode->addr.ToString());
        Misbehaving(pnode->id, 20);
        return;
    }

    {
        LOCK(cs);

        CTnode* prealTnode = NULL;
        std::vector<CTnode*> vpTnodesToBan;
        std::vector<CTnode>::iterator it = vTnodes.begin();
        std::string strMessage1 = strprintf("%s%d%s", pnode->addr.ToString(), mnv.nonce, blockHash.ToString());
        while(it != vTnodes.end()) {
            if(CAddress(it->addr, NODE_NETWORK) == pnode->addr) {
                if(darkSendSigner.VerifyMessage(it->pubKeyTnode, mnv.vchSig1, strMessage1, strError)) {
                    // found it!
                    prealTnode = &(*it);
                    if(!it->IsPoSeVerified()) {
                        it->DecreasePoSeBanScore();
                    }
                    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done");

                    // we can only broadcast it if we are an activated tnode
                    if(activeTnode.vin == CTxIn()) continue;
                    // update ...
                    mnv.addr = it->addr;
                    mnv.vin1 = it->vin;
                    mnv.vin2 = activeTnode.vin;
                    std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString(),
                                            mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());
                    // ... and sign it
                    if(!darkSendSigner.SignMessage(strMessage2, mnv.vchSig2, activeTnode.keyTnode)) {
                        LogPrintf("TnodeMan::ProcessVerifyReply -- SignMessage() failed\n");
                        return;
                    }

                    std::string strError;

                    if(!darkSendSigner.VerifyMessage(activeTnode.pubKeyTnode, mnv.vchSig2, strMessage2, strError)) {
                        LogPrintf("TnodeMan::ProcessVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
                        return;
                    }

                    mWeAskedForVerification[pnode->addr] = mnv;
                    mnv.Relay();

                } else {
                    vpTnodesToBan.push_back(&(*it));
                }
            }
            ++it;
        }
        // no real tnode found?...
        if(!prealTnode) {
            // this should never be the case normally,
            // only if someone is trying to game the system in some way or smth like that
            LogPrintf("CTnodeMan::ProcessVerifyReply -- ERROR: no real tnode found for addr %s\n", pnode->addr.ToString());
            Misbehaving(pnode->id, 20);
            return;
        }
        LogPrintf("CTnodeMan::ProcessVerifyReply -- verified real tnode %s for addr %s\n",
                    prealTnode->vin.prevout.ToStringShort(), pnode->addr.ToString());
        // increase ban score for everyone else
        BOOST_FOREACH(CTnode* pmn, vpTnodesToBan) {
            pmn->IncreasePoSeBanScore();
            LogPrint("tnode", "CTnodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        prealTnode->vin.prevout.ToStringShort(), pnode->addr.ToString(), pmn->nPoSeBanScore);
        }
        LogPrintf("CTnodeMan::ProcessVerifyBroadcast -- PoSe score increased for %d fake tnodes, addr %s\n",
                    (int)vpTnodesToBan.size(), pnode->addr.ToString());
    }
}

void CTnodeMan::ProcessVerifyBroadcast(CNode* pnode, const CTnodeVerification& mnv)
{
    std::string strError;

    if(mapSeenTnodeVerification.find(mnv.GetHash()) != mapSeenTnodeVerification.end()) {
        // we already have one
        return;
    }
    mapSeenTnodeVerification[mnv.GetHash()] = mnv;

    // we don't care about history
    if(mnv.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS) {
        LogPrint("tnode", "TnodeMan::ProcessVerifyBroadcast -- Outdated: current block %d, verification block %d, peer=%d\n",
                    pCurrentBlockIndex->nHeight, mnv.nBlockHeight, pnode->id);
        return;
    }

    if(mnv.vin1.prevout == mnv.vin2.prevout) {
        LogPrint("tnode", "TnodeMan::ProcessVerifyBroadcast -- ERROR: same vins %s, peer=%d\n",
                    mnv.vin1.prevout.ToStringShort(), pnode->id);
        // that was NOT a good idea to cheat and verify itself,
        // ban the node we received such message from
        Misbehaving(pnode->id, 100);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("TnodeMan::ProcessVerifyBroadcast -- Can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    int nRank = GetTnodeRank(mnv.vin2, mnv.nBlockHeight, MIN_POSE_PROTO_VERSION);

    if (nRank == -1) {
        LogPrint("tnode", "CTnodeMan::ProcessVerifyBroadcast -- Can't calculate rank for tnode %s\n",
                    mnv.vin2.prevout.ToStringShort());
        return;
    }

    if(nRank > MAX_POSE_RANK) {
        LogPrint("tnode", "CTnodeMan::ProcessVerifyBroadcast -- Mastrernode %s is not in top %d, current rank %d, peer=%d\n",
                    mnv.vin2.prevout.ToStringShort(), (int)MAX_POSE_RANK, nRank, pnode->id);
        return;
    }

    {
        LOCK(cs);

        std::string strMessage1 = strprintf("%s%d%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString());
        std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString(),
                                mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());

        CTnode* pmn1 = Find(mnv.vin1);
        if(!pmn1) {
            LogPrintf("CTnodeMan::ProcessVerifyBroadcast -- can't find tnode1 %s\n", mnv.vin1.prevout.ToStringShort());
            return;
        }

        CTnode* pmn2 = Find(mnv.vin2);
        if(!pmn2) {
            LogPrintf("CTnodeMan::ProcessVerifyBroadcast -- can't find tnode2 %s\n", mnv.vin2.prevout.ToStringShort());
            return;
        }

        if(pmn1->addr != mnv.addr) {
            LogPrintf("CTnodeMan::ProcessVerifyBroadcast -- addr %s do not match %s\n", mnv.addr.ToString(), pnode->addr.ToString());
            return;
        }

        if(darkSendSigner.VerifyMessage(pmn1->pubKeyTnode, mnv.vchSig1, strMessage1, strError)) {
            LogPrintf("TnodeMan::ProcessVerifyBroadcast -- VerifyMessage() for tnode1 failed, error: %s\n", strError);
            return;
        }

        if(darkSendSigner.VerifyMessage(pmn2->pubKeyTnode, mnv.vchSig2, strMessage2, strError)) {
            LogPrintf("TnodeMan::ProcessVerifyBroadcast -- VerifyMessage() for tnode2 failed, error: %s\n", strError);
            return;
        }

        if(!pmn1->IsPoSeVerified()) {
            pmn1->DecreasePoSeBanScore();
        }
        mnv.Relay();

        LogPrintf("CTnodeMan::ProcessVerifyBroadcast -- verified tnode %s for addr %s\n",
                    pmn1->vin.prevout.ToStringShort(), pnode->addr.ToString());

        // increase ban score for everyone else with the same addr
        int nCount = 0;
        BOOST_FOREACH(CTnode& mn, vTnodes) {
            if(mn.addr != mnv.addr || mn.vin.prevout == mnv.vin1.prevout) continue;
            mn.IncreasePoSeBanScore();
            nCount++;
            LogPrint("tnode", "CTnodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        mn.vin.prevout.ToStringShort(), mn.addr.ToString(), mn.nPoSeBanScore);
        }
        LogPrintf("CTnodeMan::ProcessVerifyBroadcast -- PoSe score incresed for %d fake tnodes, addr %s\n",
                    nCount, pnode->addr.ToString());
    }
}

std::string CTnodeMan::ToString() const
{
    std::ostringstream info;

    info << "Tnodes: " << (int)vTnodes.size() <<
            ", peers who asked us for Tnode list: " << (int)mAskedUsForTnodeList.size() <<
            ", peers we asked for Tnode list: " << (int)mWeAskedForTnodeList.size() <<
            ", entries in Tnode list we asked for: " << (int)mWeAskedForTnodeListEntry.size() <<
            ", tnode index size: " << indexTnodes.GetSize() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

void CTnodeMan::UpdateTnodeList(CTnodeBroadcast mnb)
{
    try {
        LogPrintf("CTnodeMan::UpdateTnodeList\n");
        LOCK2(cs_main, cs);
        mapSeenTnodePing.insert(std::make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
        mapSeenTnodeBroadcast.insert(std::make_pair(mnb.GetHash(), std::make_pair(GetTime(), mnb)));

        LogPrintf("CTnodeMan::UpdateTnodeList -- tnode=%s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());

        CTnode *pmn = Find(mnb.vin);
        if (pmn == NULL) {
            CTnode mn(mnb);
            if (Add(mn)) {
                tnodeSync.AddedTnodeList();
            }
        } else {
            CTnodeBroadcast mnbOld = mapSeenTnodeBroadcast[CTnodeBroadcast(*pmn).GetHash()].second;
            if (pmn->UpdateFromNewBroadcast(mnb)) {
                tnodeSync.AddedTnodeList();
                mapSeenTnodeBroadcast.erase(mnbOld.GetHash());
            }
        }
    } catch (const std::exception &e) {
        PrintExceptionContinue(&e, "UpdateTnodeList");
    }
}

bool CTnodeMan::CheckMnbAndUpdateTnodeList(CNode* pfrom, CTnodeBroadcast mnb, int& nDos)
{
    // Need LOCK2 here to ensure consistent locking order because the SimpleCheck call below locks cs_main
    LOCK(cs_main);

    {
        LOCK(cs);
        nDos = 0;
        LogPrint("tnode", "CTnodeMan::CheckMnbAndUpdateTnodeList -- tnode=%s\n", mnb.vin.prevout.ToStringShort());

        uint256 hash = mnb.GetHash();
        if (mapSeenTnodeBroadcast.count(hash) && !mnb.fRecovery) { //seen
            LogPrint("tnode", "CTnodeMan::CheckMnbAndUpdateTnodeList -- tnode=%s seen\n", mnb.vin.prevout.ToStringShort());
            // less then 2 pings left before this MN goes into non-recoverable state, bump sync timeout
            if (GetTime() - mapSeenTnodeBroadcast[hash].first > TNODE_NEW_START_REQUIRED_SECONDS - TNODE_MIN_MNP_SECONDS * 2) {
                LogPrint("tnode", "CTnodeMan::CheckMnbAndUpdateTnodeList -- tnode=%s seen update\n", mnb.vin.prevout.ToStringShort());
                mapSeenTnodeBroadcast[hash].first = GetTime();
                tnodeSync.AddedTnodeList();
            }
            // did we ask this node for it?
            if (pfrom && IsMnbRecoveryRequested(hash) && GetTime() < mMnbRecoveryRequests[hash].first) {
                LogPrint("tnode", "CTnodeMan::CheckMnbAndUpdateTnodeList -- mnb=%s seen request\n", hash.ToString());
                if (mMnbRecoveryRequests[hash].second.count(pfrom->addr)) {
                    LogPrint("tnode", "CTnodeMan::CheckMnbAndUpdateTnodeList -- mnb=%s seen request, addr=%s\n", hash.ToString(), pfrom->addr.ToString());
                    // do not allow node to send same mnb multiple times in recovery mode
                    mMnbRecoveryRequests[hash].second.erase(pfrom->addr);
                    // does it have newer lastPing?
                    if (mnb.lastPing.sigTime > mapSeenTnodeBroadcast[hash].second.lastPing.sigTime) {
                        // simulate Check
                        CTnode mnTemp = CTnode(mnb);
                        mnTemp.Check();
                        LogPrint("tnode", "CTnodeMan::CheckMnbAndUpdateTnodeList -- mnb=%s seen request, addr=%s, better lastPing: %d min ago, projected mn state: %s\n", hash.ToString(), pfrom->addr.ToString(), (GetTime() - mnb.lastPing.sigTime) / 60, mnTemp.GetStateString());
                        if (mnTemp.IsValidStateForAutoStart(mnTemp.nActiveState)) {
                            // this node thinks it's a good one
                            LogPrint("tnode", "CTnodeMan::CheckMnbAndUpdateTnodeList -- tnode=%s seen good\n", mnb.vin.prevout.ToStringShort());
                            mMnbRecoveryGoodReplies[hash].push_back(mnb);
                        }
                    }
                }
            }
            return true;
        }
        mapSeenTnodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));

        LogPrint("tnode", "CTnodeMan::CheckMnbAndUpdateTnodeList -- tnode=%s new\n", mnb.vin.prevout.ToStringShort());

        if (!mnb.SimpleCheck(nDos)) {
            LogPrint("tnode", "CTnodeMan::CheckMnbAndUpdateTnodeList -- SimpleCheck() failed, tnode=%s\n", mnb.vin.prevout.ToStringShort());
            return false;
        }

        // search Tnode list
        CTnode *pmn = Find(mnb.vin);
        if (pmn) {
            CTnodeBroadcast mnbOld = mapSeenTnodeBroadcast[CTnodeBroadcast(*pmn).GetHash()].second;
            if (!mnb.Update(pmn, nDos)) {
                LogPrint("tnode", "CTnodeMan::CheckMnbAndUpdateTnodeList -- Update() failed, tnode=%s\n", mnb.vin.prevout.ToStringShort());
                return false;
            }
            if (hash != mnbOld.GetHash()) {
                mapSeenTnodeBroadcast.erase(mnbOld.GetHash());
            }
        }
    } // end of LOCK(cs);

    if(mnb.CheckOutpoint(nDos)) {
        Add(mnb);
        tnodeSync.AddedTnodeList();
        // if it matches our Tnode privkey...
        if(fTnodeMode && mnb.pubKeyTnode == activeTnode.pubKeyTnode) {
            mnb.nPoSeBanScore = -TNODE_POSE_BAN_MAX_SCORE;
            if(mnb.nProtocolVersion == LEGACY_TNODES_PROTOCOL_VERSION) {
                // ... and PROTOCOL_VERSION, then we've been remotely activated ...
                LogPrintf("CTnodeMan::CheckMnbAndUpdateTnodeList -- Got NEW Tnode entry: tnode=%s  sigTime=%lld  addr=%s\n",
                            mnb.vin.prevout.ToStringShort(), mnb.sigTime, mnb.addr.ToString());
                activeTnode.ManageState();
            } else {
                // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
                // but also do not ban the node we get this message from
                LogPrintf("CTnodeMan::CheckMnbAndUpdateTnodeList -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", mnb.nProtocolVersion, LEGACY_TNODES_PROTOCOL_VERSION);
                return false;
            }
        }
        mnb.RelayTNode();
    } else {
        LogPrintf("CTnodeMan::CheckMnbAndUpdateTnodeList -- Rejected Tnode entry: %s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());
        return false;
    }

    return true;
}

void CTnodeMan::UpdateLastPaid()
{
    LOCK(cs);
    if(fLiteMode) return;
    if(!pCurrentBlockIndex) {
        // LogPrintf("CTnodeMan::UpdateLastPaid, pCurrentBlockIndex=NULL\n");
        return;
    }

    static bool IsFirstRun = true;
    // Do full scan on first run or if we are not a tnode
    // (MNs should update this info on every block, so limited scan should be enough for them)
    int nMaxBlocksToScanBack = (IsFirstRun || !fTnodeMode) ? tnpayments.GetStorageLimit() : LAST_PAID_SCAN_BLOCKS;

    LogPrint("tnpayments", "CTnodeMan::UpdateLastPaid -- nHeight=%d, nMaxBlocksToScanBack=%d, IsFirstRun=%s\n",
                             pCurrentBlockIndex->nHeight, nMaxBlocksToScanBack, IsFirstRun ? "true" : "false");

    BOOST_FOREACH(CTnode& mn, vTnodes) {
        mn.UpdateLastPaid(pCurrentBlockIndex, nMaxBlocksToScanBack);
    }

    // every time is like the first time if winners list is not synced
    IsFirstRun = !tnodeSync.IsWinnersListSynced();
}

void CTnodeMan::CheckAndRebuildTnodeIndex()
{
    LOCK(cs);

    if(GetTime() - nLastIndexRebuildTime < MIN_INDEX_REBUILD_TIME) {
        return;
    }

    if(indexTnodes.GetSize() <= MAX_EXPECTED_INDEX_SIZE) {
        return;
    }

    if(indexTnodes.GetSize() <= int(vTnodes.size())) {
        return;
    }

    indexTnodesOld = indexTnodes;
    indexTnodes.Clear();
    for(size_t i = 0; i < vTnodes.size(); ++i) {
        indexTnodes.AddTnodeVIN(vTnodes[i].vin);
    }

    fIndexRebuilt = true;
    nLastIndexRebuildTime = GetTime();
}

void CTnodeMan::UpdateWatchdogVoteTime(const CTxIn& vin)
{
    LOCK(cs);
    CTnode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->UpdateWatchdogVoteTime();
    nLastWatchdogVoteTime = GetTime();
}

bool CTnodeMan::IsWatchdogActive()
{
    LOCK(cs);
    // Check if any tnodes have voted recently, otherwise return false
    return (GetTime() - nLastWatchdogVoteTime) <= TNODE_WATCHDOG_MAX_SECONDS;
}

void CTnodeMan::CheckTnode(const CTxIn& vin, bool fForce)
{
    LOCK(cs);
    CTnode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->Check(fForce);
}

void CTnodeMan::CheckTnode(const CPubKey& pubKeyTnode, bool fForce)
{
    LOCK(cs);
    CTnode* pMN = Find(pubKeyTnode);
    if(!pMN)  {
        return;
    }
    pMN->Check(fForce);
}

int CTnodeMan::GetTnodeState(const CTxIn& vin)
{
    LOCK(cs);
    CTnode* pMN = Find(vin);
    if(!pMN)  {
        return CTnode::TNODE_NEW_START_REQUIRED;
    }
    return pMN->nActiveState;
}

int CTnodeMan::GetTnodeState(const CPubKey& pubKeyTnode)
{
    LOCK(cs);
    CTnode* pMN = Find(pubKeyTnode);
    if(!pMN)  {
        return CTnode::TNODE_NEW_START_REQUIRED;
    }
    return pMN->nActiveState;
}

bool CTnodeMan::IsTnodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CTnode* pMN = Find(vin);
    if(!pMN) {
        return false;
    }
    return pMN->IsPingedWithin(nSeconds, nTimeToCheckAt);
}

void CTnodeMan::SetTnodeLastPing(const CTxIn& vin, const CTnodePing& mnp)
{
    LOCK2(cs_main, cs);
    CTnode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->lastPing = mnp;
    mapSeenTnodePing.insert(std::make_pair(mnp.GetHash(), mnp));

    CTnodeBroadcast mnb(*pMN);
    uint256 hash = mnb.GetHash();
    if(mapSeenTnodeBroadcast.count(hash)) {
        mapSeenTnodeBroadcast[hash].second.lastPing = mnp;
    }
}

void CTnodeMan::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
    LogPrint("tnode", "CTnodeMan::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);

    CheckSameAddr();

    if(fTnodeMode) {
        // normal wallet does not need to update this every block, doing update on rpc call should be enough
        UpdateLastPaid();
    }
}

void CTnodeMan::NotifyTnodeUpdates()
{
    // Avoid double locking
    bool fTnodesAddedLocal = false;
    bool fTnodesRemovedLocal = false;
    {
        LOCK(cs);
        fTnodesAddedLocal = fTnodesAdded;
        fTnodesRemovedLocal = fTnodesRemoved;
    }

    if(fTnodesAddedLocal) {
//        governance.CheckTnodeOrphanObjects();
//        governance.CheckTnodeOrphanVotes();
    }
    if(fTnodesRemovedLocal) {
//        governance.UpdateCachesAndClean();
    }

    LOCK(cs);
    fTnodesAdded = false;
    fTnodesRemoved = false;
}

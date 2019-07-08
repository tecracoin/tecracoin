// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TNODEMAN_H
#define TNODEMAN_H

#include "tnode.h"
#include "sync.h"

using namespace std;

class CTnodeMan;

extern CTnodeMan mnodeman;

/**
 * Provides a forward and reverse index between MN vin's and integers.
 *
 * This mapping is normally add-only and is expected to be permanent
 * It is only rebuilt if the size of the index exceeds the expected maximum number
 * of MN's and the current number of known MN's.
 *
 * The external interface to this index is provided via delegation by CTnodeMan
 */
class CTnodeIndex
{
public: // Types
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

    typedef std::map<int,CTxIn> rindex_m_t;

    typedef rindex_m_t::iterator rindex_m_it;

    typedef rindex_m_t::const_iterator rindex_m_cit;

private:
    int                  nSize;

    index_m_t            mapIndex;

    rindex_m_t           mapReverseIndex;

public:
    CTnodeIndex();

    int GetSize() const {
        return nSize;
    }

    /// Retrieve tnode vin by index
    bool Get(int nIndex, CTxIn& vinTnode) const;

    /// Get index of a tnode vin
    int GetTnodeIndex(const CTxIn& vinTnode) const;

    void AddTnodeVIN(const CTxIn& vinTnode);

    void Clear();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapIndex);
        if(ser_action.ForRead()) {
            RebuildIndex();
        }
    }

private:
    void RebuildIndex();

};

class CTnodeMan
{
public:
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

private:
    static const int MAX_EXPECTED_INDEX_SIZE = 30000;

    /// Only allow 1 index rebuild per hour
    static const int64_t MIN_INDEX_REBUILD_TIME = 3600;

    static const std::string SERIALIZATION_VERSION_STRING;

    static const int DSEG_UPDATE_SECONDS        = 3 * 60 * 60;

    static const int LAST_PAID_SCAN_BLOCKS      = 100;

    static const int MIN_POSE_PROTO_VERSION     = 70203;
    static const int MAX_POSE_CONNECTIONS       = 10;
    static const int MAX_POSE_RANK              = 10;
    static const int MAX_POSE_BLOCKS            = 10;

    static const int MNB_RECOVERY_QUORUM_TOTAL      = 10;
    static const int MNB_RECOVERY_QUORUM_REQUIRED   = 6;
    static const int MNB_RECOVERY_MAX_ASK_ENTRIES   = 10;
    static const int MNB_RECOVERY_WAIT_SECONDS      = 60;
    static const int MNB_RECOVERY_RETRY_SECONDS     = 3 * 60 * 60;


    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    // map to hold all MNs
    std::vector<CTnode> vTnodes;
    // who's asked for the Tnode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForTnodeList;
    // who we asked for the Tnode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForTnodeList;
    // which Tnodes we've asked for
    std::map<COutPoint, std::map<CNetAddr, int64_t> > mWeAskedForTnodeListEntry;
    // who we asked for the tnode verification
    std::map<CNetAddr, CTnodeVerification> mWeAskedForVerification;

    // these maps are used for tnode recovery from TNODE_NEW_START_REQUIRED state
    std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > > mMnbRecoveryRequests;
    std::map<uint256, std::vector<CTnodeBroadcast> > mMnbRecoveryGoodReplies;
    std::list< std::pair<CService, uint256> > listScheduledMnbRequestConnections;

    int64_t nLastIndexRebuildTime;

    CTnodeIndex indexTnodes;

    CTnodeIndex indexTnodesOld;

    /// Set when index has been rebuilt, clear when read
    bool fIndexRebuilt;

    /// Set when tnodes are added, cleared when CGovernanceManager is notified
    bool fTnodesAdded;

    /// Set when tnodes are removed, cleared when CGovernanceManager is notified
    bool fTnodesRemoved;

    std::vector<uint256> vecDirtyGovernanceObjectHashes;

    int64_t nLastWatchdogVoteTime;

    friend class CTnodeSync;

public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, std::pair<int64_t, CTnodeBroadcast> > mapSeenTnodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CTnodePing> mapSeenTnodePing;
    // Keep track of all verifications I've seen
    std::map<uint256, CTnodeVerification> mapSeenTnodeVerification;
    // keep track of dsq count to prevent tnodes from gaming darksend queue
    int64_t nDsqCount;


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        std::string strVersion;
        if(ser_action.ForRead()) {
            READWRITE(strVersion);
        }
        else {
            strVersion = SERIALIZATION_VERSION_STRING; 
            READWRITE(strVersion);
        }

        READWRITE(vTnodes);
        READWRITE(mAskedUsForTnodeList);
        READWRITE(mWeAskedForTnodeList);
        READWRITE(mWeAskedForTnodeListEntry);
        READWRITE(mMnbRecoveryRequests);
        READWRITE(mMnbRecoveryGoodReplies);
        READWRITE(nLastWatchdogVoteTime);
        READWRITE(nDsqCount);

        READWRITE(mapSeenTnodeBroadcast);
        READWRITE(mapSeenTnodePing);
        READWRITE(indexTnodes);
        if(ser_action.ForRead() && (strVersion != SERIALIZATION_VERSION_STRING)) {
            Clear();
        }
    }

    CTnodeMan();

    /// Add an entry
    bool Add(CTnode &mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode *pnode, const CTxIn &vin);
    void AskForMnb(CNode *pnode, const uint256 &hash);

    /// Check all Tnodes
    void Check();

    /// Check all Tnodes and remove inactive
    void CheckAndRemove();

    /// Clear Tnode vector
    void Clear();

    /// Count Tnodes filtered by nProtocolVersion.
    /// Tnode nProtocolVersion should match or be above the one specified in param here.
    int CountTnodes(int nProtocolVersion = -1);
    /// Count enabled Tnodes filtered by nProtocolVersion.
    /// Tnode nProtocolVersion should match or be above the one specified in param here.
    int CountEnabled(int nProtocolVersion = -1);

    /// Count Tnodes by network type - NET_IPV4, NET_IPV6, NET_TOR
    // int CountByIP(int nNetworkType);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CTnode* Find(const CScript &payee);
    CTnode* Find(const CTxIn& vin);
    CTnode* Find(const CPubKey& pubKeyTnode);

    /// Versions of Find that are safe to use from outside the class
    bool Get(const CPubKey& pubKeyTnode, CTnode& tnode);
    bool Get(const CTxIn& vin, CTnode& tnode);

    /// Retrieve tnode vin by index
    bool Get(int nIndex, CTxIn& vinTnode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexTnodes.Get(nIndex, vinTnode);
    }

    bool GetIndexRebuiltFlag() {
        LOCK(cs);
        return fIndexRebuilt;
    }

    /// Get index of a tnode vin
    int GetTnodeIndex(const CTxIn& vinTnode) {
        LOCK(cs);
        return indexTnodes.GetTnodeIndex(vinTnode);
    }

    /// Get old index of a tnode vin
    int GetTnodeIndexOld(const CTxIn& vinTnode) {
        LOCK(cs);
        return indexTnodesOld.GetTnodeIndex(vinTnode);
    }

    /// Get tnode VIN for an old index value
    bool GetTnodeVinForIndexOld(int nTnodeIndex, CTxIn& vinTnodeOut) {
        LOCK(cs);
        return indexTnodesOld.Get(nTnodeIndex, vinTnodeOut);
    }

    /// Get index of a tnode vin, returning rebuild flag
    int GetTnodeIndex(const CTxIn& vinTnode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexTnodes.GetTnodeIndex(vinTnode);
    }

    void ClearOldTnodeIndex() {
        LOCK(cs);
        indexTnodesOld.Clear();
        fIndexRebuilt = false;
    }

    bool Has(const CTxIn& vin);

    tnode_info_t GetTnodeInfo(const CTxIn& vin);

    tnode_info_t GetTnodeInfo(const CPubKey& pubKeyTnode);

    char* GetNotQualifyReason(CTnode& mn, int nBlockHeight, bool fFilterSigTime, int nMnCount);

    /// Find an entry in the tnode list that is next to be paid
    CTnode* GetNextTnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);
    /// Same as above but use current block height
    CTnode* GetNextTnodeInQueueForPayment(bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CTnode* FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion = -1);

    std::vector<CTnode> GetFullTnodeVector() { return vTnodes; }

    std::vector<std::pair<int, CTnode> > GetTnodeRanks(int nBlockHeight = -1, int nMinProtocol=0);
    int GetTnodeRank(const CTxIn &vin, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);
    CTnode* GetTnodeByRank(int nRank, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);

    void ProcessTnodeConnections();
    std::pair<CService, std::set<uint256> > PopScheduledMnbRequestConnection();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    void DoFullVerificationStep();
    void CheckSameAddr();
    bool SendVerifyRequest(const CAddress& addr, const std::vector<CTnode*>& vSortedByAddr);
    void SendVerifyReply(CNode* pnode, CTnodeVerification& mnv);
    void ProcessVerifyReply(CNode* pnode, CTnodeVerification& mnv);
    void ProcessVerifyBroadcast(CNode* pnode, const CTnodeVerification& mnv);

    /// Return the number of (unique) Tnodes
    int size() { return vTnodes.size(); }

    std::string ToString() const;

    /// Update tnode list and maps using provided CTnodeBroadcast
    void UpdateTnodeList(CTnodeBroadcast mnb);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateTnodeList(CNode* pfrom, CTnodeBroadcast mnb, int& nDos);
    bool IsMnbRecoveryRequested(const uint256& hash) { return mMnbRecoveryRequests.count(hash); }

    void UpdateLastPaid();

    void CheckAndRebuildTnodeIndex();

    void AddDirtyGovernanceObjectHash(const uint256& nHash)
    {
        LOCK(cs);
        vecDirtyGovernanceObjectHashes.push_back(nHash);
    }

    std::vector<uint256> GetAndClearDirtyGovernanceObjectHashes()
    {
        LOCK(cs);
        std::vector<uint256> vecTmp = vecDirtyGovernanceObjectHashes;
        vecDirtyGovernanceObjectHashes.clear();
        return vecTmp;;
    }

    bool IsWatchdogActive();
    void UpdateWatchdogVoteTime(const CTxIn& vin);
    bool AddGovernanceVote(const CTxIn& vin, uint256 nGovernanceObjectHash);
    void RemoveGovernanceObject(uint256 nGovernanceObjectHash);

    void CheckTnode(const CTxIn& vin, bool fForce = false);
    void CheckTnode(const CPubKey& pubKeyTnode, bool fForce = false);

    int GetTnodeState(const CTxIn& vin);
    int GetTnodeState(const CPubKey& pubKeyTnode);

    bool IsTnodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt = -1);
    void SetTnodeLastPing(const CTxIn& vin, const CTnodePing& mnp);

    void UpdatedBlockTip(const CBlockIndex *pindex);

    /**
     * Called to notify CGovernanceManager that the tnode index has been updated.
     * Must be called while not holding the CTnodeMan::cs mutex
     */
    void NotifyTnodeUpdates();

};

#endif

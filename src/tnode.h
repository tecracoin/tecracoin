// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TNODE_H
#define TNODE_H

#include "key.h"
#include "validation.h"
#include "net.h"
#include "spork.h"
#include "timedata.h"
#include "utiltime.h"

class CTnode;
class CTnodeBroadcast;
class CTnodePing;

static const int TNODE_CHECK_SECONDS               =   5;
static const int TNODE_MIN_MNB_SECONDS             =   5 * 60; //BROADCAST_TIME
static const int TNODE_EXPIRATION_SECONDS          =  65 * 60;
static const int TNODE_WATCHDOG_MAX_SECONDS        = 120 * 60;
static const int TNODE_COIN_REQUIRED  = 1000;

static const int TNODE_POSE_BAN_MAX_SCORE          = 5;

class CTnodeTimings {
    struct Mainnet {
        static const int TnodeMinMnpSeconds                =  10 * 60; //PRE_ENABLE_TIME
        static const int TnodeNewStartRequiredSeconds      = 180 * 60;
    };
    struct Regtest {
        static const int TnodeMinMnpSeconds                = 30;
        static const int TnodeNewStartRequiredSeconds      = 60;
    };
public:
    static int MinMnpSeconds();
    static int NewStartRequiredSeconds();
private:
    static CTnodeTimings & Inst();
    CTnodeTimings();
    CTnodeTimings(CTnodeTimings const &)=delete;
    void operator=(CTnodeTimings const &)=delete;
    int minMnp, newStartRequired;
};

#define TNODE_MIN_MNP_SECONDS CTnodeTimings::MinMnpSeconds()
#define TNODE_NEW_START_REQUIRED_SECONDS CTnodeTimings::NewStartRequiredSeconds()

//
// The Tnode Ping Class : Contains a different serialize method for sending pings from tnodes through out the network
//

class CTnodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times
    std::vector<unsigned char> vchSig;
    //removed stop

    CTnodePing() :
        vin(),
        blockHash(),
        sigTime(0),
        vchSig()
        {}

    CTnodePing(CTxIn& vinNew);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    bool IsExpired() { return GetTime() - sigTime > TNODE_NEW_START_REQUIRED_SECONDS; }

    bool Sign(CKey& keyTnode, CPubKey& pubKeyTnode);
    bool CheckSignature(CPubKey& pubKeyTnode, int &nDos);
    bool SimpleCheck(int& nDos);
    bool CheckAndUpdate(CTnode* pmn, bool fFromNewBroadcast, int& nDos);
    void Relay();

    CTnodePing& operator=(const CTnodePing &from)
    {
        vin = from.vin;
        blockHash = from.blockHash;
        sigTime = from.sigTime;
        vchSig = from.vchSig;
        return *this;
    }
    friend bool operator==(const CTnodePing& a, const CTnodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CTnodePing& a, const CTnodePing& b)
    {
        return !(a == b);
    }

};

struct tnode_info_t
{
    tnode_info_t()
        : vin(),
          addr(),
          pubKeyCollateralAddress(),
          pubKeyTnode(),
          sigTime(0),
          nLastDsq(0),
          nTimeLastChecked(0),
          nTimeLastPaid(0),
          nTimeLastWatchdogVote(0),
          nTimeLastPing(0),
          nActiveState(0),
          nProtocolVersion(0),
          fInfoValid(false)
        {}

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyTnode;
    int64_t sigTime; //mnb message time
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int64_t nTimeLastChecked;
    int64_t nTimeLastPaid;
    int64_t nTimeLastWatchdogVote;
    int64_t nTimeLastPing;
    int nActiveState;
    int nProtocolVersion;
    bool fInfoValid;
};

//
// The Tnode Class. For managing the Darksend process. It contains the input of the 1000DRK, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CTnode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

public:
    enum state {
        TNODE_PRE_ENABLED,
        TNODE_ENABLED,
        TNODE_EXPIRED,
        TNODE_OUTPOINT_SPENT,
        TNODE_UPDATE_REQUIRED,
        TNODE_WATCHDOG_EXPIRED,
        TNODE_NEW_START_REQUIRED,
        TNODE_POSE_BAN
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyTnode;
    CTnodePing lastPing;
    std::vector<unsigned char> vchSig;
    int64_t sigTime; //mnb message time
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int64_t nTimeLastChecked;
    int64_t nTimeLastPaid;
    int64_t nTimeLastWatchdogVote;
    int nActiveState;
    int nCacheCollateralBlock;
    int nBlockLastPaid;
    int nProtocolVersion;
    int nPoSeBanScore;
    int nPoSeBanHeight;
    bool fAllowMixingTx;
    bool fUnitTest;

    // KEEP TRACK OF GOVERNANCE ITEMS EACH TNODE HAS VOTE UPON FOR RECALCULATION
    std::map<uint256, int> mapGovernanceObjectsVotedOn;

    CTnode();
    CTnode(const CTnode& other);
    CTnode(const CTnodeBroadcast& mnb);
    CTnode(CService addrNew, CTxIn vinNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyTnodeNew, int nProtocolVersionIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        LOCK(cs);
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyTnode);
        READWRITE(lastPing);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nLastDsq);
        READWRITE(nTimeLastChecked);
        READWRITE(nTimeLastPaid);
        READWRITE(nTimeLastWatchdogVote);
        READWRITE(nActiveState);
        READWRITE(nCacheCollateralBlock);
        READWRITE(nBlockLastPaid);
        READWRITE(nProtocolVersion);
        READWRITE(nPoSeBanScore);
        READWRITE(nPoSeBanHeight);
        READWRITE(fAllowMixingTx);
        READWRITE(fUnitTest);
        READWRITE(mapGovernanceObjectsVotedOn);
    }

    void swap(CTnode& first, CTnode& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubKeyCollateralAddress, second.pubKeyCollateralAddress);
        swap(first.pubKeyTnode, second.pubKeyTnode);
        swap(first.lastPing, second.lastPing);
        swap(first.vchSig, second.vchSig);
        swap(first.sigTime, second.sigTime);
        swap(first.nLastDsq, second.nLastDsq);
        swap(first.nTimeLastChecked, second.nTimeLastChecked);
        swap(first.nTimeLastPaid, second.nTimeLastPaid);
        swap(first.nTimeLastWatchdogVote, second.nTimeLastWatchdogVote);
        swap(first.nActiveState, second.nActiveState);
        swap(first.nCacheCollateralBlock, second.nCacheCollateralBlock);
        swap(first.nBlockLastPaid, second.nBlockLastPaid);
        swap(first.nProtocolVersion, second.nProtocolVersion);
        swap(first.nPoSeBanScore, second.nPoSeBanScore);
        swap(first.nPoSeBanHeight, second.nPoSeBanHeight);
        swap(first.fAllowMixingTx, second.fAllowMixingTx);
        swap(first.fUnitTest, second.fUnitTest);
        swap(first.mapGovernanceObjectsVotedOn, second.mapGovernanceObjectsVotedOn);
    }

    // CALCULATE A RANK AGAINST OF GIVEN BLOCK
    arith_uint256 CalculateScore(const uint256& blockHash);

    bool UpdateFromNewBroadcast(CTnodeBroadcast& mnb);

    void Check(bool fForce = false);

    bool IsBroadcastedWithin(int nSeconds) { return GetAdjustedTime() - sigTime < nSeconds; }

    bool IsPingedWithin(int nSeconds, int64_t nTimeToCheckAt = -1)
    {
        if(lastPing == CTnodePing()) return false;

        if(nTimeToCheckAt == -1) {
            nTimeToCheckAt = GetAdjustedTime();
        }
        return nTimeToCheckAt - lastPing.sigTime < nSeconds;
    }

    bool IsEnabled() { return nActiveState == TNODE_ENABLED; }
    bool IsPreEnabled() { return nActiveState == TNODE_PRE_ENABLED; }
    bool IsPoSeBanned() { return nActiveState == TNODE_POSE_BAN; }
    // NOTE: this one relies on nPoSeBanScore, not on nActiveState as everything else here
    bool IsPoSeVerified() { return nPoSeBanScore <= -TNODE_POSE_BAN_MAX_SCORE; }
    bool IsExpired() { return nActiveState == TNODE_EXPIRED; }
    bool IsOutpointSpent() { return nActiveState == TNODE_OUTPOINT_SPENT; }
    bool IsUpdateRequired() { return nActiveState == TNODE_UPDATE_REQUIRED; }
    bool IsWatchdogExpired() { return nActiveState == TNODE_WATCHDOG_EXPIRED; }
    bool IsNewStartRequired() { return nActiveState == TNODE_NEW_START_REQUIRED; }

    static bool IsValidStateForAutoStart(int nActiveStateIn)
    {
        return  nActiveStateIn == TNODE_ENABLED ||
                nActiveStateIn == TNODE_PRE_ENABLED ||
                nActiveStateIn == TNODE_EXPIRED ||
                nActiveStateIn == TNODE_WATCHDOG_EXPIRED;
    }

    bool IsValidForPayment();

    static bool IsLegacyWindow(int height);

    bool IsValidNetAddr();
    static bool IsValidNetAddr(CService addrIn);

    void IncreasePoSeBanScore() { if(nPoSeBanScore < TNODE_POSE_BAN_MAX_SCORE) nPoSeBanScore++; }
    void DecreasePoSeBanScore() { if(nPoSeBanScore > -TNODE_POSE_BAN_MAX_SCORE) nPoSeBanScore--; }

    tnode_info_t GetInfo();

    static std::string StateToString(int nStateIn);
    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string ToString() const;

    int GetCollateralAge();

    int GetLastPaidTime() { return nTimeLastPaid; }
    int GetLastPaidBlock() { return nBlockLastPaid; }
    void UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack);

    // KEEP TRACK OF EACH GOVERNANCE ITEM INCASE THIS NODE GOES OFFLINE, SO WE CAN RECALC THEIR STATUS
    void AddGovernanceVote(uint256 nGovernanceObjectHash);
    // RECALCULATE CACHED STATUS FLAGS FOR ALL AFFECTED OBJECTS
    void FlagGovernanceItemsAsDirty();

    void RemoveGovernanceObject(uint256 nGovernanceObjectHash);

    void UpdateWatchdogVoteTime();

    CTnode& operator=(CTnode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CTnode& a, const CTnode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CTnode& a, const CTnode& b)
    {
        return !(a.vin == b.vin);
    }

};


//
// The Tnode Broadcast Class : Contains a different serialize method for sending tnodes through the network
//

class CTnodeBroadcast : public CTnode
{
public:

    bool fRecovery;

    CTnodeBroadcast() : CTnode(), fRecovery(false) {}
    CTnodeBroadcast(const CTnode& mn) : CTnode(mn), fRecovery(false) {}
    CTnodeBroadcast(CService addrNew, CTxIn vinNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyTnodeNew, int nProtocolVersionIn) :
        CTnode(addrNew, vinNew, pubKeyCollateralAddressNew, pubKeyTnodeNew, nProtocolVersionIn), fRecovery(false) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyTnode);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nProtocolVersion);
        READWRITE(lastPing);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << pubKeyCollateralAddress;
        ss << sigTime;
        return ss.GetHash();
    }

    /// Create Tnode broadcast, needs to be relayed manually after that
    static bool Create(CTxIn vin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyTnodeNew, CPubKey pubKeyTnodeNew, std::string &strErrorRet, CTnodeBroadcast &mnbRet);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CTnodeBroadcast &mnbRet, bool fOffline = false);

    bool SimpleCheck(int& nDos);
    bool Update(CTnode* pmn, int& nDos);
    bool CheckOutpoint(int& nDos);

    bool Sign(CKey& keyCollateralAddress);
    bool CheckSignature(int& nDos);
    void RelayTNode();
};

class CTnodeVerification
{
public:
    CTxIn vin1;
    CTxIn vin2;
    CService addr;
    int nonce;
    int nBlockHeight;
    std::vector<unsigned char> vchSig1;
    std::vector<unsigned char> vchSig2;

    CTnodeVerification() :
        vin1(),
        vin2(),
        addr(),
        nonce(0),
        nBlockHeight(0),
        vchSig1(),
        vchSig2()
        {}

    CTnodeVerification(CService addr, int nonce, int nBlockHeight) :
        vin1(),
        vin2(),
        addr(addr),
        nonce(nonce),
        nBlockHeight(nBlockHeight),
        vchSig1(),
        vchSig2()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vin1);
        READWRITE(vin2);
        READWRITE(addr);
        READWRITE(nonce);
        READWRITE(nBlockHeight);
        READWRITE(vchSig1);
        READWRITE(vchSig2);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin1;
        ss << vin2;
        ss << addr;
        ss << nonce;
        ss << nBlockHeight;
        return ss.GetHash();
    }

    void Relay() const
    {
        CInv inv(MSG_TNODE_VERIFY, GetHash());
        g_connman->RelayInv(inv);
    }
};

#endif

// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activetnode.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "init.h"
//#include "governance.h"
#include "tnode.h"
#include "tnode-payments.h"
#include "tnode-sync.h"
#include "tnodeman.h"
#include "util.h"
#include "net.h"
#include "netbase.h"

#include <boost/lexical_cast.hpp>

CTnodeTimings::CTnodeTimings()
{
    if(Params().GetConsensus().IsRegtest()) {
        minMnp = Regtest::TnodeMinMnpSeconds;
        newStartRequired = Regtest::TnodeNewStartRequiredSeconds;
    } else {
        minMnp = Mainnet::TnodeMinMnpSeconds;
        newStartRequired = Mainnet::TnodeNewStartRequiredSeconds;
    }
}

CTnodeTimings & CTnodeTimings::Inst() {
    static CTnodeTimings inst;
    return inst;
}

int CTnodeTimings::MinMnpSeconds() {
    return Inst().minMnp;
}

int CTnodeTimings::NewStartRequiredSeconds() {
    return Inst().newStartRequired;
}


CTnode::CTnode() :
        vin(),
        addr(),
        pubKeyCollateralAddress(),
        pubKeyTnode(),
        lastPing(),
        vchSig(),
        sigTime(GetAdjustedTime()),
        nLastDsq(0),
        nTimeLastChecked(0),
        nTimeLastPaid(0),
        nTimeLastWatchdogVote(0),
        nActiveState(TNODE_ENABLED),
        nCacheCollateralBlock(0),
        nBlockLastPaid(0),
        nProtocolVersion(LEGACY_TNODES_PROTOCOL_VERSION),
        nPoSeBanScore(0),
        nPoSeBanHeight(0),
        fAllowMixingTx(true),
        fUnitTest(false) {}

CTnode::CTnode(CService addrNew, CTxIn vinNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyTnodeNew, int nProtocolVersionIn) :
        vin(vinNew),
        addr(addrNew),
        pubKeyCollateralAddress(pubKeyCollateralAddressNew),
        pubKeyTnode(pubKeyTnodeNew),
        lastPing(),
        vchSig(),
        sigTime(GetAdjustedTime()),
        nLastDsq(0),
        nTimeLastChecked(0),
        nTimeLastPaid(0),
        nTimeLastWatchdogVote(0),
        nActiveState(TNODE_ENABLED),
        nCacheCollateralBlock(0),
        nBlockLastPaid(0),
        nProtocolVersion(nProtocolVersionIn),
        nPoSeBanScore(0),
        nPoSeBanHeight(0),
        fAllowMixingTx(true),
        fUnitTest(false) {}

CTnode::CTnode(const CTnode &other) :
        vin(other.vin),
        addr(other.addr),
        pubKeyCollateralAddress(other.pubKeyCollateralAddress),
        pubKeyTnode(other.pubKeyTnode),
        lastPing(other.lastPing),
        vchSig(other.vchSig),
        sigTime(other.sigTime),
        nLastDsq(other.nLastDsq),
        nTimeLastChecked(other.nTimeLastChecked),
        nTimeLastPaid(other.nTimeLastPaid),
        nTimeLastWatchdogVote(other.nTimeLastWatchdogVote),
        nActiveState(other.nActiveState),
        nCacheCollateralBlock(other.nCacheCollateralBlock),
        nBlockLastPaid(other.nBlockLastPaid),
        nProtocolVersion(other.nProtocolVersion),
        nPoSeBanScore(other.nPoSeBanScore),
        nPoSeBanHeight(other.nPoSeBanHeight),
        fAllowMixingTx(other.fAllowMixingTx),
        fUnitTest(other.fUnitTest) {}

CTnode::CTnode(const CTnodeBroadcast &mnb) :
        vin(mnb.vin),
        addr(mnb.addr),
        pubKeyCollateralAddress(mnb.pubKeyCollateralAddress),
        pubKeyTnode(mnb.pubKeyTnode),
        lastPing(mnb.lastPing),
        vchSig(mnb.vchSig),
        sigTime(mnb.sigTime),
        nLastDsq(0),
        nTimeLastChecked(0),
        nTimeLastPaid(0),
        nTimeLastWatchdogVote(mnb.sigTime),
        nActiveState(mnb.nActiveState),
        nCacheCollateralBlock(0),
        nBlockLastPaid(0),
        nProtocolVersion(mnb.nProtocolVersion),
        nPoSeBanScore(0),
        nPoSeBanHeight(0),
        fAllowMixingTx(true),
        fUnitTest(false) {}

//CSporkManager sporkManager;
//
// When a new tnode broadcast is sent, update our information
//
bool CTnode::UpdateFromNewBroadcast(CTnodeBroadcast &mnb) {
    if (mnb.sigTime <= sigTime && !mnb.fRecovery) return false;

    pubKeyTnode = mnb.pubKeyTnode;
    sigTime = mnb.sigTime;
    vchSig = mnb.vchSig;
    nProtocolVersion = mnb.nProtocolVersion;
    addr = mnb.addr;
    nPoSeBanScore = 0;
    nPoSeBanHeight = 0;
    nTimeLastChecked = 0;
    int nDos = 0;
    if (mnb.lastPing == CTnodePing() || (mnb.lastPing != CTnodePing() && mnb.lastPing.CheckAndUpdate(this, true, nDos))) {
        lastPing = mnb.lastPing;
        mnodeman.mapSeenTnodePing.insert(std::make_pair(lastPing.GetHash(), lastPing));
    }
    // if it matches our Tnode privkey...
    if (fTnodeMode && pubKeyTnode == activeTnode.pubKeyTnode) {
        nPoSeBanScore = -TNODE_POSE_BAN_MAX_SCORE;
        if (nProtocolVersion == LEGACY_TNODES_PROTOCOL_VERSION) {
            // ... and PROTOCOL_VERSION, then we've been remotely activated ...
            activeTnode.ManageState();
        } else {
            // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
            // but also do not ban the node we get this message from
            LogPrintf("CTnode::UpdateFromNewBroadcast -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", nProtocolVersion, LEGACY_TNODES_PROTOCOL_VERSION);
            return false;
        }
    }
    return true;
}

//
// Deterministically calculate a given "score" for a Tnode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
arith_uint256 CTnode::CalculateScore(const uint256 &blockHash) {
    uint256 aux = ArithToUint256(UintToArith256(vin.prevout.hash) + vin.prevout.n);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << blockHash;
    arith_uint256 hash2 = UintToArith256(ss.GetHash());

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << blockHash;
    ss2 << aux;
    arith_uint256 hash3 = UintToArith256(ss2.GetHash());

    return (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);
}

void CTnode::Check(bool fForce) {
    LOCK(cs);

    if (ShutdownRequested()) return;

    if (!fForce && (GetTime() - nTimeLastChecked < TNODE_CHECK_SECONDS)) return;
    nTimeLastChecked = GetTime();

    LogPrint("tnode", "CTnode::Check -- Tnode %s is in %s state\n", vin.prevout.ToStringShort(), GetStateString());

    //once spent, stop doing the checks
    if (IsOutpointSpent()) return;

    int nHeight = 0;
    if (!fUnitTest) {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) return;

        Coin coin;
        if (!pcoinsTip->GetCoin(vin.prevout, coin) || coin.out.IsNull() || coin.IsSpent()) {
            nActiveState = TNODE_OUTPOINT_SPENT;
            LogPrint("tnode", "CTnode::Check -- Failed to find Tnode UTXO, tnode=%s\n", vin.prevout.ToStringShort());
            return;
        }

        nHeight = chainActive.Height();
    }

    if (IsPoSeBanned()) {
        if (nHeight < nPoSeBanHeight) return; // too early?
        // Otherwise give it a chance to proceed further to do all the usual checks and to change its state.
        // Tnode still will be on the edge and can be banned back easily if it keeps ignoring mnverify
        // or connect attempts. Will require few mnverify messages to strengthen its position in mn list.
        LogPrintf("CTnode::Check -- Tnode %s is unbanned and back in list now\n", vin.prevout.ToStringShort());
        DecreasePoSeBanScore();
    } else if (nPoSeBanScore >= TNODE_POSE_BAN_MAX_SCORE) {
        nActiveState = TNODE_POSE_BAN;
        // ban for the whole payment cycle
        nPoSeBanHeight = nHeight + mnodeman.size();
        LogPrintf("CTnode::Check -- Tnode %s is banned till block %d now\n", vin.prevout.ToStringShort(), nPoSeBanHeight);
        return;
    }

    int nActiveStatePrev = nActiveState;
    bool fOurTnode = fTnodeMode && activeTnode.pubKeyTnode == pubKeyTnode;

    // tnode doesn't meet payment protocol requirements ...
/*    bool fRequireUpdate = nProtocolVersion < znpayments.GetMinTnodePaymentsProto() ||
                          // or it's our own node and we just updated it to the new protocol but we are still waiting for activation ...
                          (fOurTnode && nProtocolVersion < PROTOCOL_VERSION); */

    // tnode doesn't meet payment protocol requirements ...
    bool fRequireUpdate = nProtocolVersion < tnpayments.GetMinTnodePaymentsProto() ||
                          // or it's our own node and we just updated it to the new protocol but we are still waiting for activation ...
                          (fOurTnode && (nProtocolVersion < MIN_TNODE_PAYMENT_PROTO_VERSION_1 || nProtocolVersion > MIN_TNODE_PAYMENT_PROTO_VERSION_2));

    if (fRequireUpdate) {
        nActiveState = TNODE_UPDATE_REQUIRED;
        if (nActiveStatePrev != nActiveState) {
            LogPrint("tnode", "CTnode::Check -- Tnode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
        }
        return;
    }

    // keep old tnodes on start, give them a chance to receive updates...
    bool fWaitForPing = !tnodeSync.IsTnodeListSynced() && !IsPingedWithin(TNODE_MIN_MNP_SECONDS);

    if (fWaitForPing && !fOurTnode) {
        // ...but if it was already expired before the initial check - return right away
        if (IsExpired() || IsWatchdogExpired() || IsNewStartRequired()) {
            LogPrint("tnode", "CTnode::Check -- Tnode %s is in %s state, waiting for ping\n", vin.prevout.ToStringShort(), GetStateString());
            return;
        }
    }

    // don't expire if we are still in "waiting for ping" mode unless it's our own tnode
    if (!fWaitForPing || fOurTnode) {

        if (!IsPingedWithin(TNODE_NEW_START_REQUIRED_SECONDS)) {
            nActiveState = TNODE_NEW_START_REQUIRED;
            if (nActiveStatePrev != nActiveState) {
                LogPrint("tnode", "CTnode::Check -- Tnode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }

        bool fWatchdogActive = tnodeSync.IsSynced() && mnodeman.IsWatchdogActive();
        bool fWatchdogExpired = (fWatchdogActive && ((GetTime() - nTimeLastWatchdogVote) > TNODE_WATCHDOG_MAX_SECONDS));

//        LogPrint("tnode", "CTnode::Check -- outpoint=%s, nTimeLastWatchdogVote=%d, GetTime()=%d, fWatchdogExpired=%d\n",
//                vin.prevout.ToStringShort(), nTimeLastWatchdogVote, GetTime(), fWatchdogExpired);

        if (fWatchdogExpired) {
            nActiveState = TNODE_WATCHDOG_EXPIRED;
            if (nActiveStatePrev != nActiveState) {
                LogPrint("tnode", "CTnode::Check -- Tnode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }

        if (!IsPingedWithin(TNODE_EXPIRATION_SECONDS)) {
            nActiveState = TNODE_EXPIRED;
            if (nActiveStatePrev != nActiveState) {
                LogPrint("tnode", "CTnode::Check -- Tnode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && lastPing.sigTime - sigTime < TNODE_MIN_MNP_SECONDS) {
        nActiveState = TNODE_PRE_ENABLED;
        if (nActiveStatePrev != nActiveState) {
            LogPrint("tnode", "CTnode::Check -- Tnode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
        }
        return;
    }

    nActiveState = TNODE_ENABLED; // OK
    if (nActiveStatePrev != nActiveState) {
        LogPrint("tnode", "CTnode::Check -- Tnode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
    }
}

bool CTnode::IsLegacyWindow(int height) {
    const Consensus::Params& params = ::Params().GetConsensus();
    return height >= params.DIP0003Height && height < params.DIP0003EnforcementHeight;
}

bool CTnode::IsValidNetAddr() {
    return IsValidNetAddr(addr);
}

bool CTnode::IsValidForPayment() {
    if (nActiveState == TNODE_ENABLED) {
        return true;
    }
//    if(!sporkManager.IsSporkActive(SPORK_14_REQUIRE_SENTINEL_FLAG) &&
//       (nActiveState == TNODE_WATCHDOG_EXPIRED)) {
//        return true;
//    }

    return false;
}

bool CTnode::IsValidNetAddr(CService addrIn) {
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkIDString() == CBaseChainParams::REGTEST ||
           (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}

tnode_info_t CTnode::GetInfo() {
    tnode_info_t info;
    info.vin = vin;
    info.addr = addr;
    info.pubKeyCollateralAddress = pubKeyCollateralAddress;
    info.pubKeyTnode = pubKeyTnode;
    info.sigTime = sigTime;
    info.nLastDsq = nLastDsq;
    info.nTimeLastChecked = nTimeLastChecked;
    info.nTimeLastPaid = nTimeLastPaid;
    info.nTimeLastWatchdogVote = nTimeLastWatchdogVote;
    info.nTimeLastPing = lastPing.sigTime;
    info.nActiveState = nActiveState;
    info.nProtocolVersion = nProtocolVersion;
    info.fInfoValid = true;
    return info;
}

std::string CTnode::StateToString(int nStateIn) {
    switch (nStateIn) {
        case TNODE_PRE_ENABLED:
            return "PRE_ENABLED";
        case TNODE_ENABLED:
            return "ENABLED";
        case TNODE_EXPIRED:
            return "EXPIRED";
        case TNODE_OUTPOINT_SPENT:
            return "OUTPOINT_SPENT";
        case TNODE_UPDATE_REQUIRED:
            return "UPDATE_REQUIRED";
        case TNODE_WATCHDOG_EXPIRED:
            return "WATCHDOG_EXPIRED";
        case TNODE_NEW_START_REQUIRED:
            return "NEW_START_REQUIRED";
        case TNODE_POSE_BAN:
            return "POSE_BAN";
        default:
            return "UNKNOWN";
    }
}

std::string CTnode::GetStateString() const {
    return StateToString(nActiveState);
}

std::string CTnode::GetStatus() const {
    // TODO: return smth a bit more human readable here
    return GetStateString();
}

std::string CTnode::ToString() const {
    std::string str;
    str += "tnode{";
    str += addr.ToString();
    str += " ";
    str += std::to_string(nProtocolVersion);
    str += " ";
    str += vin.prevout.ToStringShort();
    str += " ";
    str += CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString();
    str += " ";
    str += std::to_string(lastPing == CTnodePing() ? sigTime : lastPing.sigTime);
    str += " ";
    str += std::to_string(lastPing == CTnodePing() ? 0 : lastPing.sigTime - sigTime);
    str += " ";
    str += std::to_string(nBlockLastPaid);
    str += "}\n";
    return str;
}

int CTnode::GetCollateralAge() {
    int nHeight;
    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain || !chainActive.Tip()) return -1;
        nHeight = chainActive.Height();
    }

    if (nCacheCollateralBlock == 0) {
        int nInputAge = GetInputAge(vin);
        if (nInputAge > 0) {
            nCacheCollateralBlock = nHeight - nInputAge;
        } else {
            return nInputAge;
        }
    }

    return nHeight - nCacheCollateralBlock;
}

void CTnode::UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack) {
    if (!pindex) {
        LogPrintf("CTnode::UpdateLastPaid pindex is NULL\n");
        return;
    }

    const Consensus::Params &params = Params().GetConsensus();
    const CBlockIndex *BlockReading = pindex;

    CScript mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());
    LogPrint("tnode", "CTnode::UpdateLastPaidBlock -- searching for block with payment to %s\n", vin.prevout.ToStringShort());

    LOCK(cs_mapTnodeBlocks);

    for (int i = 0; BlockReading && BlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++) {
//        LogPrintf("tnpayments.mapTnodeBlocks.count(BlockReading->nHeight)=%s\n", znpayments.mapTnodeBlocks.count(BlockReading->nHeight));
//        LogPrintf("tnpayments.mapTnodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)=%s\n", znpayments.mapTnodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2));
        if (tnpayments.mapTnodeBlocks.count(BlockReading->nHeight) &&
            tnpayments.mapTnodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)) {
            // LogPrintf("i=%s, BlockReading->nHeight=%s\n", i, BlockReading->nHeight);
            CBlock block;
            if (!ReadBlockFromDisk(block, BlockReading, Params().GetConsensus())) // shouldn't really happen
            {
                LogPrintf("ReadBlockFromDisk failed\n");
                continue;
            }
            bool fMTP = BlockReading->nHeight > 0 && BlockReading->nTime >= params.nMTPSwitchTime;
            CAmount nTnodePayment = GetTnodePayment(params, fMTP);

            BOOST_FOREACH(CTxOut txout, block.vtx[0]->vout)
            if (mnpayee == txout.scriptPubKey && nTnodePayment == txout.nValue) {
                nBlockLastPaid = BlockReading->nHeight;
                nTimeLastPaid = BlockReading->nTime;
                LogPrint("tnode", "CTnode::UpdateLastPaidBlock -- searching for block with payment to %s -- found new %d\n", vin.prevout.ToStringShort(), nBlockLastPaid);
                return;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    // Last payment for this tnode wasn't found in latest znpayments blocks
    // or it was found in znpayments blocks but wasn't found in the blockchain.
    // LogPrint("tnode", "CTnode::UpdateLastPaidBlock -- searching for block with payment to %s -- keeping old %d\n", vin.prevout.ToStringShort(), nBlockLastPaid);
}

bool CTnodeBroadcast::Create(std::string strService, std::string strKeyTnode, std::string strTxHash, std::string strOutputIndex, std::string &strErrorRet, CTnodeBroadcast &mnbRet, bool fOffline) {
    LogPrintf("CTnodeBroadcast::Create\n");
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyTnodeNew;
    CKey keyTnodeNew;
    //need correct blocks to send ping
    if (!fOffline && !tnodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Tnode";
        LogPrintf("CTnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    //TODO
    if (!darkSendSigner.GetKeysFromSecret(strKeyTnode, keyTnodeNew, pubKeyTnodeNew)) {
        strErrorRet = strprintf("Invalid tnode key %s", strKeyTnode);
        LogPrintf("CTnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!pwalletMain->GetTnodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for tnode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CTnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    // TODO: upgrade dash

    CService service = LookupNumeric(strService.c_str());
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            strErrorRet = strprintf("Invalid port %u for tnode %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
            LogPrintf("CTnodeBroadcast::Create -- %s\n", strErrorRet);
            return false;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for tnode %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
        LogPrintf("CTnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    return Create(txin, LookupNumeric(strService.c_str()), keyCollateralAddressNew, pubKeyCollateralAddressNew, keyTnodeNew, pubKeyTnodeNew, strErrorRet, mnbRet);
}

bool CTnodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyTnodeNew, CPubKey pubKeyTnodeNew, std::string &strErrorRet, CTnodeBroadcast &mnbRet) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("tnode", "CTnodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyTnodeNew.GetID() = %s\n",
             CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
             pubKeyTnodeNew.GetID().ToString());


    CTnodePing mnp(txin);
    if (!mnp.Sign(keyTnodeNew, pubKeyTnodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, tnode=%s", txin.prevout.ToStringShort());
        LogPrintf("CTnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CTnodeBroadcast();
        return false;
    }

    int nHeight = chainActive.Height();
    mnbRet = CTnodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyTnodeNew, LEGACY_TNODES_PROTOCOL_VERSION);

    if (!mnbRet.IsValidNetAddr()) {
        strErrorRet = strprintf("Invalid IP address, tnode=%s", txin.prevout.ToStringShort());
        LogPrintf("CTnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CTnodeBroadcast();
        return false;
    }

    mnbRet.lastPing = mnp;
    if (!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, tnode=%s", txin.prevout.ToStringShort());
        LogPrintf("CTnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CTnodeBroadcast();
        return false;
    }

    return true;
}

bool CTnodeBroadcast::SimpleCheck(int &nDos) {
    nDos = 0;

    // make sure addr is valid
    if (!IsValidNetAddr()) {
        LogPrintf("CTnodeBroadcast::SimpleCheck -- Invalid addr, rejected: tnode=%s  addr=%s\n",
                  vin.prevout.ToStringShort(), addr.ToString());
        return false;
    }

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CTnodeBroadcast::SimpleCheck -- Signature rejected, too far into the future: tnode=%s\n", vin.prevout.ToStringShort());
        nDos = 1;
        return false;
    }

    // empty ping or incorrect sigTime/unknown blockhash
    if (lastPing == CTnodePing() || !lastPing.SimpleCheck(nDos)) {
        // one of us is probably forked or smth, just mark it as expired and check the rest of the rules
        nActiveState = TNODE_EXPIRED;
    }

    if (nProtocolVersion < tnpayments.GetMinTnodePaymentsProto()) {
        LogPrintf("CTnodeBroadcast::SimpleCheck -- ignoring outdated Tnode: tnode=%s  nProtocolVersion=%d\n", vin.prevout.ToStringShort(), nProtocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25) {
        LogPrintf("CTnodeBroadcast::SimpleCheck -- pubKeyCollateralAddress has the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyTnode.GetID());

    if (pubkeyScript2.size() != 25) {
        LogPrintf("CTnodeBroadcast::SimpleCheck -- pubKeyTnode has the wrong size\n");
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty()) {
        LogPrintf("CTnodeBroadcast::SimpleCheck -- Ignore Not Empty ScriptSig %s\n", vin.ToString());
        nDos = 100;
        return false;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (addr.GetPort() != mainnetDefaultPort) return false;
    } else if (addr.GetPort() == mainnetDefaultPort) return false;

    return true;
}

bool CTnodeBroadcast::Update(CTnode *pmn, int &nDos) {
    nDos = 0;

    if (pmn->sigTime == sigTime && !fRecovery) {
        // mapSeenTnodeBroadcast in CTnodeMan::CheckMnbAndUpdateTnodeList should filter legit duplicates
        // but this still can happen if we just started, which is ok, just do nothing here.
        return false;
    }

    // this broadcast is older than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    if (pmn->sigTime > sigTime) {
        LogPrintf("CTnodeBroadcast::Update -- Bad sigTime %d (existing broadcast is at %d) for Tnode %s %s\n",
                  sigTime, pmn->sigTime, vin.prevout.ToStringShort(), addr.ToString());
        return false;
    }

    pmn->Check();

    // tnode is banned by PoSe
    if (pmn->IsPoSeBanned()) {
        LogPrintf("CTnodeBroadcast::Update -- Banned by PoSe, tnode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    // IsVnAssociatedWithPubkey is validated once in CheckOutpoint, after that they just need to match
    if (pmn->pubKeyCollateralAddress != pubKeyCollateralAddress) {
        LogPrintf("CTnodeBroadcast::Update -- Got mismatched pubKeyCollateralAddress and vin\n");
        nDos = 33;
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CTnodeBroadcast::Update -- CheckSignature() failed, tnode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    // if ther was no tnode broadcast recently or if it matches our Tnode privkey...
    if (!pmn->IsBroadcastedWithin(TNODE_MIN_MNB_SECONDS) || (fTnodeMode && pubKeyTnode == activeTnode.pubKeyTnode)) {
        // take the newest entry
        LogPrintf("CTnodeBroadcast::Update -- Got UPDATED Tnode entry: addr=%s\n", addr.ToString());
        if (pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            RelayTNode();
        }
        tnodeSync.AddedTnodeList();
    }

    return true;
}

bool CTnodeBroadcast::CheckOutpoint(int &nDos) {
    // we are a tnode with the same vin (i.e. already activated) and this mnb is ours (matches our Tnode privkey)
    // so nothing to do here for us
    if (fTnodeMode && vin.prevout == activeTnode.vin.prevout && pubKeyTnode == activeTnode.pubKeyTnode) {
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CTnodeBroadcast::CheckOutpoint -- CheckSignature() failed, tnode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            // not mnb fault, let it to be checked again later
            LogPrint("tnode", "CTnodeBroadcast::CheckOutpoint -- Failed to aquire lock, addr=%s", addr.ToString());
            mnodeman.mapSeenTnodeBroadcast.erase(GetHash());
            return false;
        }

        Coin coin;
        if (!pcoinsTip->GetCoin(vin.prevout, coin) || coin.out.IsNull() || coin.IsSpent()) {
            LogPrint("tnode", "CTnodeBroadcast::CheckOutpoint -- Failed to find Tnode UTXO, tnode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
        if (coin.out.nValue != TNODE_COIN_REQUIRED * COIN) {
            LogPrint("tnode", "CTnodeBroadcast::CheckOutpoint -- Tnode UTXO should have 10000 TCR, tnode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
        if (chainActive.Height() - coin.nHeight + 1 < Params().GetConsensus().nTnodeMinimumConfirmations) {
            LogPrintf("CTnodeBroadcast::CheckOutpoint -- Tnode UTXO must have at least %d confirmations, tnode=%s\n",
                      Params().GetConsensus().nTnodeMinimumConfirmations, vin.prevout.ToStringShort());
            // maybe we miss few blocks, let this mnb to be checked again later
            mnodeman.mapSeenTnodeBroadcast.erase(GetHash());
            return false;
        }
    }

    LogPrint("tnode", "CTnodeBroadcast::CheckOutpoint -- Tnode UTXO verified\n");

    // make sure the vout that was signed is related to the transaction that spawned the Tnode
    //  - this is expensive, so it's only done once per Tnode
    if (!darkSendSigner.IsVinAssociatedWithPubkey(vin, pubKeyCollateralAddress)) {
        LogPrintf("CTnodeMan::CheckOutpoint -- Got mismatched pubKeyCollateralAddress and vin\n");
        nDos = 33;
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 10000 TCR tx got nTnodeMinimumConfirmations
    uint256 hashBlock = uint256();
    CTransactionRef tx2;
    GetTransaction(vin.prevout.hash, tx2, Params().GetConsensus(), hashBlock, true);
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex *pMNIndex = (*mi).second; // block for 10000 TCR tx -> 1 confirmation
            CBlockIndex *pConfIndex = chainActive[pMNIndex->nHeight + Params().GetConsensus().nTnodeMinimumConfirmations - 1]; // block where tx got nTnodeMinimumConfirmations
            if (pConfIndex->GetBlockTime() > sigTime) {
                LogPrintf("CTnodeBroadcast::CheckOutpoint -- Bad sigTime %d (%d conf block is at %d) for Tnode %s %s\n",
                          sigTime, Params().GetConsensus().nTnodeMinimumConfirmations, pConfIndex->GetBlockTime(), vin.prevout.ToStringShort(), addr.ToString());
                return false;
            }
        }
    }

    return true;
}

bool CTnodeBroadcast::Sign(CKey &keyCollateralAddress) {
    std::string strError;
    std::string strMessage;

    sigTime = GetAdjustedTime();

    strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) +
                 pubKeyCollateralAddress.GetID().ToString() + pubKeyTnode.GetID().ToString() +
                 boost::lexical_cast<std::string>(nProtocolVersion);

    if (!darkSendSigner.SignMessage(strMessage, vchSig, keyCollateralAddress)) {
        LogPrintf("CTnodeBroadcast::Sign -- SignMessage() failed\n");
        return false;
    }

    if (!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
        LogPrintf("CTnodeBroadcast::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CTnodeBroadcast::CheckSignature(int &nDos) {
    std::string strMessage;
    std::string strError = "";
    nDos = 0;

    strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) +
                 pubKeyCollateralAddress.GetID().ToString() + pubKeyTnode.GetID().ToString() +
                 boost::lexical_cast<std::string>(nProtocolVersion);

    LogPrint("tnode", "CTnodeBroadcast::CheckSignature -- strMessage: %s  pubKeyCollateralAddress address: %s  sig: %s\n", strMessage, CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString(), EncodeBase64(&vchSig[0], vchSig.size()));

    if (!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
        LogPrintf("CTnodeBroadcast::CheckSignature -- Got bad Tnode announce signature, error: %s\n", strError);
        nDos = 100;
        return false;
    }

    return true;
}

void CTnodeBroadcast::RelayTNode() {
    LogPrintf("CTnodeBroadcast::RelayTNode\n");
    CInv inv(MSG_TNODE_ANNOUNCE, GetHash());
    g_connman->RelayInv(inv);
}

CTnodePing::CTnodePing(CTxIn &vinNew) {
    LOCK(cs_main);
    if (!chainActive.Tip() || chainActive.Height() < 12) return;

    vin = vinNew;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector < unsigned char > ();
}

bool CTnodePing::Sign(CKey &keyTnode, CPubKey &pubKeyTnode) {
    std::string strError;
    std::string strTNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if (!darkSendSigner.SignMessage(strMessage, vchSig, keyTnode)) {
        LogPrintf("CTnodePing::Sign -- SignMessage() failed\n");
        return false;
    }

    if (!darkSendSigner.VerifyMessage(pubKeyTnode, vchSig, strMessage, strError)) {
        LogPrintf("CTnodePing::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CTnodePing::CheckSignature(CPubKey &pubKeyTnode, int &nDos) {
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string strError = "";
    nDos = 0;

    if (!darkSendSigner.VerifyMessage(pubKeyTnode, vchSig, strMessage, strError)) {
        LogPrintf("CTnodePing::CheckSignature -- Got bad Tnode ping signature, tnode=%s, error: %s\n", vin.prevout.ToStringShort(), strError);
        nDos = 33;
        return false;
    }
    return true;
}

bool CTnodePing::SimpleCheck(int &nDos) {
    // don't ban by default
    nDos = 0;

    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CTnodePing::SimpleCheck -- Signature rejected, too far into the future, tnode=%s\n", vin.prevout.ToStringShort());
        nDos = 1;
        return false;
    }

    {
//        LOCK(cs_main);
        AssertLockHeld(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if (mi == mapBlockIndex.end()) {
            LogPrint("tnode", "CTnodePing::SimpleCheck -- Tnode ping is invalid, unknown block hash: tnode=%s blockHash=%s\n", vin.prevout.ToStringShort(), blockHash.ToString());
            // maybe we stuck or forked so we shouldn't ban this node, just fail to accept this ping
            // TODO: or should we also request this block?
            return false;
        }
    }
    LogPrint("tnode", "CTnodePing::SimpleCheck -- Tnode ping verified: tnode=%s  blockHash=%s  sigTime=%d\n", vin.prevout.ToStringShort(), blockHash.ToString(), sigTime);
    return true;
}

bool CTnodePing::CheckAndUpdate(CTnode *pmn, bool fFromNewBroadcast, int &nDos) {
    // don't ban by default
    nDos = 0;

    if (!SimpleCheck(nDos)) {
        return false;
    }

    if (pmn == NULL) {
        LogPrint("tnode", "CTnodePing::CheckAndUpdate -- Couldn't find Tnode entry, tnode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    if (!fFromNewBroadcast) {
        if (pmn->IsUpdateRequired()) {
            LogPrint("tnode", "CTnodePing::CheckAndUpdate -- tnode protocol is outdated, tnode=%s\n", vin.prevout.ToStringShort());
            return false;
        }

        if (pmn->IsNewStartRequired()) {
            LogPrint("tnode", "CTnodePing::CheckAndUpdate -- tnode is completely expired, new start is required, tnode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
    }

    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if ((*mi).second && (*mi).second->nHeight < chainActive.Height() - 24) {
            // LogPrintf("CTnodePing::CheckAndUpdate -- Tnode ping is invalid, block hash is too old: tnode=%s  blockHash=%s\n", vin.prevout.ToStringShort(), blockHash.ToString());
            // nDos = 1;
            return false;
        }
    }

    LogPrint("tnode", "CTnodePing::CheckAndUpdate -- New ping: tnode=%s  blockHash=%s  sigTime=%d\n", vin.prevout.ToStringShort(), blockHash.ToString(), sigTime);

    // LogPrintf("mnping - Found corresponding mn for vin: %s\n", vin.prevout.ToStringShort());
    // update only if there is no known ping for this tnode or
    // last ping was more then TNODE_MIN_MNP_SECONDS-60 ago comparing to this one
    if (pmn->IsPingedWithin(TNODE_MIN_MNP_SECONDS - 60, sigTime)) {
        LogPrint("tnode", "CTnodePing::CheckAndUpdate -- Tnode ping arrived too early, tnode=%s\n", vin.prevout.ToStringShort());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }

    if (!CheckSignature(pmn->pubKeyTnode, nDos)) return false;

    // so, ping seems to be ok

    // if we are still syncing and there was no known ping for this mn for quite a while
    // (NOTE: assuming that TNODE_EXPIRATION_SECONDS/2 should be enough to finish mn list sync)
    if (!tnodeSync.IsTnodeListSynced() && !pmn->IsPingedWithin(TNODE_EXPIRATION_SECONDS / 2)) {
        // let's bump sync timeout
        LogPrint("tnode", "CTnodePing::CheckAndUpdate -- bumping sync timeout, tnode=%s\n", vin.prevout.ToStringShort());
        tnodeSync.AddedTnodeList();
    }

    // let's store this ping as the last one
    LogPrint("tnode", "CTnodePing::CheckAndUpdate -- Tnode ping accepted, tnode=%s\n", vin.prevout.ToStringShort());
    pmn->lastPing = *this;

    // and update mnodeman.mapSeenTnodeBroadcast.lastPing which is probably outdated
    CTnodeBroadcast mnb(*pmn);
    uint256 hash = mnb.GetHash();
    if (mnodeman.mapSeenTnodeBroadcast.count(hash)) {
        mnodeman.mapSeenTnodeBroadcast[hash].second.lastPing = *this;
    }

    pmn->Check(true); // force update, ignoring cache
    if (!pmn->IsEnabled()) return false;

    LogPrint("tnode", "CTnodePing::CheckAndUpdate -- Tnode ping acceepted and relayed, tnode=%s\n", vin.prevout.ToStringShort());
    Relay();

    return true;
}

void CTnodePing::Relay() {
    CInv inv(MSG_TNODE_PING, GetHash());
    g_connman->RelayInv(inv);
}

//void CTnode::AddGovernanceVote(uint256 nGovernanceObjectHash)
//{
//    if(mapGovernanceObjectsVotedOn.count(nGovernanceObjectHash)) {
//        mapGovernanceObjectsVotedOn[nGovernanceObjectHash]++;
//    } else {
//        mapGovernanceObjectsVotedOn.insert(std::make_pair(nGovernanceObjectHash, 1));
//    }
//}

//void CTnode::RemoveGovernanceObject(uint256 nGovernanceObjectHash)
//{
//    std::map<uint256, int>::iterator it = mapGovernanceObjectsVotedOn.find(nGovernanceObjectHash);
//    if(it == mapGovernanceObjectsVotedOn.end()) {
//        return;
//    }
//    mapGovernanceObjectsVotedOn.erase(it);
//}

void CTnode::UpdateWatchdogVoteTime() {
    LOCK(cs);
    nTimeLastWatchdogVote = GetTime();
}

/**
*   FLAG GOVERNANCE ITEMS AS DIRTY
*
*   - When tnode come and go on the network, we must flag the items they voted on to recalc it's cached flags
*
*/
//void CTnode::FlagGovernanceItemsAsDirty()
//{
//    std::vector<uint256> vecDirty;
//    {
//        std::map<uint256, int>::iterator it = mapGovernanceObjectsVotedOn.begin();
//        while(it != mapGovernanceObjectsVotedOn.end()) {
//            vecDirty.push_back(it->first);
//            ++it;
//        }
//    }
//    for(size_t i = 0; i < vecDirty.size(); ++i) {
//        mnodeman.AddDirtyGovernanceObjectHash(vecDirty[i]);
//    }
//}

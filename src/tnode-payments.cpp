// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activetnode.h"
#include "tnode-payments.h"
#include "tnode-sync.h"
#include "tnodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"
#include "net.h"
#include "net_processing.h"
#include "netmessagemaker.h"

#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CTnodePayments tnpayments;

CCriticalSection cs_vecPayees;
CCriticalSection cs_mapTnodeBlocks;
CCriticalSection cs_mapTnodePaymentVotes;

/**
* IsTnodeBlockValueValid
*
*   Determine if coinbase outgoing created money is the correct value
*
*   Why is this needed?
*   - In Zcoin some blocks are superblocks, which output much higher amounts of coins
*   - Otherblocks are 10% lower in outgoing value, so in total, no extra coins are created
*   - When non-superblocks are detected, the normal schedule should be maintained
*/

bool IsTnodeBlockValueValid(const CBlock &block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet) {
    strErrorRet = "";

    bool isBlockRewardValueMet = (block.vtx[0]->GetValueOut() <= blockReward);
    if (fDebug) LogPrintf("block.vtx[0].GetValueOut() %lld <= blockReward %lld\n", block.vtx[0]->GetValueOut(), blockReward);

    // we are still using budgets, but we have no data about them anymore,
    // all we know is predefined budget cycle and window

//    const Consensus::Params &consensusParams = Params().GetConsensus();
//
////    if (nBlockHeight < consensusParams.nSuperblockStartBlock) {
//        int nOffset = nBlockHeight % consensusParams.nBudgetPaymentsCycleBlocks;
//        if (nBlockHeight >= consensusParams.nBudgetPaymentsStartBlock &&
//            nOffset < consensusParams.nBudgetPaymentsWindowBlocks) {
//            // NOTE: make sure SPORK_13_OLD_SUPERBLOCK_FLAG is disabled when 12.1 starts to go live
//            if (tnodeSync.IsSynced() && !sporkManager.IsSporkActive(SPORK_13_OLD_SUPERBLOCK_FLAG)) {
//                // no budget blocks should be accepted here, if SPORK_13_OLD_SUPERBLOCK_FLAG is disabled
//                LogPrint("gobject", "IsTnodeBlockValueValid -- Client synced but budget spork is disabled, checking block value against block reward\n");
//                if (!isBlockRewardValueMet) {
//                    strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, budgets are disabled",
//                                            nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
//                }
//                return isBlockRewardValueMet;
//            }
//            LogPrint("gobject", "IsTnodeBlockValueValid -- WARNING: Skipping budget block value checks, accepting block\n");
//            // TODO: reprocess blocks to make sure they are legit?
//            return true;
//        }
//        // LogPrint("gobject", "IsTnodeBlockValueValid -- Block is not in budget cycle window, checking block value against block reward\n");
//        if (!isBlockRewardValueMet) {
//            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, block is not in budget cycle window",
//                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
//        }
//        return isBlockRewardValueMet;
//    }

    // superblocks started

//    CAmount nSuperblockMaxValue =  blockReward + CSuperblock::GetPaymentsLimit(nBlockHeight);
//    bool isSuperblockMaxValueMet = (block.vtx[0].GetValueOut() <= nSuperblockMaxValue);
//    bool isSuperblockMaxValueMet = false;

//    LogPrint("gobject", "block.vtx[0].GetValueOut() %lld <= nSuperblockMaxValue %lld\n", block.vtx[0].GetValueOut(), nSuperblockMaxValue);

    if (!tnodeSync.IsSynced()) {
        // not enough data but at least it must NOT exceed superblock max value
//        if(CSuperblock::IsValidBlockHeight(nBlockHeight)) {
//            if(fDebug) LogPrintf("IsTnodeBlockPayeeValid -- WARNING: Client not synced, checking superblock max bounds only\n");
//            if(!isSuperblockMaxValueMet) {
//                strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded superblock max value",
//                                        nBlockHeight, block.vtx[0].GetValueOut(), nSuperblockMaxValue);
//            }
//            return isSuperblockMaxValueMet;
//        }
        if (!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, only regular blocks are allowed at this height",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
        // it MUST be a regular block otherwise
        return isBlockRewardValueMet;
    }

    // we are synced, let's try to check as much data as we can

    if (sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED)) {
////        if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
////            if(CSuperblockManager::IsValid(block.vtx[0], nBlockHeight, blockReward)) {
////                LogPrint("gobject", "IsTnodeBlockValueValid -- Valid superblock at height %d: %s", nBlockHeight, block.vtx[0].ToString());
////                // all checks are done in CSuperblock::IsValid, nothing to do here
////                return true;
////            }
////
////            // triggered but invalid? that's weird
////            LogPrintf("IsTnodeBlockValueValid -- ERROR: Invalid superblock detected at height %d: %s", nBlockHeight, block.vtx[0].ToString());
////            // should NOT allow invalid superblocks, when superblocks are enabled
////            strErrorRet = strprintf("invalid superblock detected at height %d", nBlockHeight);
////            return false;
////        }
//        LogPrint("gobject", "IsTnodeBlockValueValid -- No triggered superblock detected at height %d\n", nBlockHeight);
//        if(!isBlockRewardValueMet) {
//            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, no triggered superblock detected",
//                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
//        }
    } else {
//        // should NOT allow superblocks at all, when superblocks are disabled
        LogPrint("gobject", "IsTnodeBlockValueValid -- Superblocks are disabled, no superblocks allowed\n");
        if (!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, superblocks are disabled",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
    }

    // it MUST be a regular block
    return isBlockRewardValueMet;
}

bool IsTnodeBlockPayeeValid(const CTransaction &txNew, int nBlockHeight, CAmount blockReward, bool fMTP) {
    // we can only check tnode payment /
    const Consensus::Params &consensusParams = Params().GetConsensus();

    if (nBlockHeight < consensusParams.nTnodePaymentsStartBlock) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        if (fDebug) LogPrintf("IsTnodeBlockPayeeValid -- tnode isn't start\n");
        return true;
    }
    if (!tnodeSync.IsSynced() && Params().NetworkIDString() != CBaseChainParams::REGTEST) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        if (fDebug) LogPrintf("IsTnodeBlockPayeeValid -- WARNING: Client not synced, skipping block payee checks\n");
        return true;
    }

    //check for tnode payee
    if (tnpayments.IsTransactionValid(txNew, nBlockHeight, fMTP)) {
        LogPrint("tnpayments", "IsTnodeBlockPayeeValid -- Valid tnode payment at height %d: %s", nBlockHeight, txNew.ToString());
        return true;
    } else {
        if(sporkManager.IsSporkActive(SPORK_8_TNODE_PAYMENT_ENFORCEMENT)){
            return false;
        } else {
            LogPrintf("TNode payment enforcement is disabled, accepting block\n");
            return true;
        }
    }
}

void FillTnodeBlockPayments(CMutableTransaction &txNew, int nBlockHeight, CAmount tnodePayment, CTxOut &txoutTnodeRet, std::vector <CTxOut> &voutSuperblockRet) {
    // only create superblocks if spork is enabled AND if superblock is actually triggered
    // (height should be validated inside)
//    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED) &&
//        CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
//            LogPrint("gobject", "FillTnodeBlockPayments -- triggered superblock creation at height %d\n", nBlockHeight);
//            CSuperblockManager::CreateSuperblock(txNew, nBlockHeight, voutSuperblockRet);
//            return;
//    }

    // FILL BLOCK PAYEE WITH TNODE PAYMENT OTHERWISE
    tnpayments.FillBlockPayee(txNew, nBlockHeight, tnodePayment, txoutTnodeRet);
    LogPrint("tnpayments", "FillTnodeBlockPayments -- nBlockHeight %d tnodePayment %lld txoutTnodeRet %s txNew %s",
             nBlockHeight, tnodePayment, txoutTnodeRet.ToString(), txNew.ToString());
}

std::string GetRequiredPaymentsString(int nBlockHeight) {
    // IF WE HAVE A ACTIVATED TRIGGER FOR THIS HEIGHT - IT IS A SUPERBLOCK, GET THE REQUIRED PAYEES
//    if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
//        return CSuperblockManager::GetRequiredPaymentsString(nBlockHeight);
//    }

    // OTHERWISE, PAY TNODE
    return tnpayments.GetRequiredPaymentsString(nBlockHeight);
}

void CTnodePayments::Clear() {
    LOCK2(cs_mapTnodeBlocks, cs_mapTnodePaymentVotes);
    mapTnodeBlocks.clear();
    mapTnodePaymentVotes.clear();
}

bool CTnodePayments::CanVote(COutPoint outTnode, int nBlockHeight) {
    LOCK(cs_mapTnodePaymentVotes);

    if (mapTnodesLastVote.count(outTnode) && mapTnodesLastVote[outTnode] == nBlockHeight) {
        return false;
    }

    //record this tnode voted
    mapTnodesLastVote[outTnode] = nBlockHeight;
    return true;
}

std::string CTnodePayee::ToString() const {
    CTxDestination address1;
    ExtractDestination(scriptPubKey, address1);
    CBitcoinAddress address2(address1);
    std::string str;
    str += "(address: ";
    str += address2.ToString();
    str += ")\n";
    return str;
}

/**
*   FillBlockPayee
*
*   Fill Tnode ONLY payment block
*/

void CTnodePayments::FillBlockPayee(CMutableTransaction &txNew, int nBlockHeight, CAmount tnodePayment, CTxOut &txoutTnodeRet) {
    // make sure it's not filled yet
    txoutTnodeRet = CTxOut();

    CScript payee;
    bool foundMaxVotedPayee = true;

    if (!tnpayments.GetBlockPayee(nBlockHeight, payee)) {
        // no tnode detected...
        // LogPrintf("no tnode detected...\n");
        foundMaxVotedPayee = false;
        int nCount = 0;
        CTnode *winningNode = mnodeman.GetNextTnodeInQueueForPayment(nBlockHeight, true, nCount);
        if (!winningNode) {
            if(Params().NetworkIDString() != CBaseChainParams::REGTEST) {
                // ...and we can't calculate it on our own
                LogPrintf("CTnodePayments::FillBlockPayee -- Failed to detect tnode to pay\n");
                return;
            }
        }
        // fill payee with locally calculated winner and hope for the best
        if (winningNode) {
            payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
            LogPrintf("payee=%s\n", winningNode->ToString());
        }
        else
            payee = txNew.vout[0].scriptPubKey;//This is only for unit tests scenario on REGTEST
    }
    txoutTnodeRet = CTxOut(tnodePayment, payee);
    txNew.vout.push_back(txoutTnodeRet);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);
    if (foundMaxVotedPayee) {
        LogPrintf("CTnodePayments::FillBlockPayee::foundMaxVotedPayee -- Tnode payment %lld to %s\n", tnodePayment, address2.ToString());
    } else {
        LogPrintf("CTnodePayments::FillBlockPayee -- Tnode payment %lld to %s\n", tnodePayment, address2.ToString());
    }

}

int CTnodePayments::GetMinTnodePaymentsProto() {
    return sporkManager.IsSporkActive(SPORK_10_TNODE_PAY_UPDATED_NODES)
           ? MIN_TNODE_PAYMENT_PROTO_VERSION_2
           : MIN_TNODE_PAYMENT_PROTO_VERSION_1;
}

void CTnodePayments::ProcessMessage(CNode *pfrom, std::string &strCommand, CDataStream &vRecv) {

//    LogPrintf("CTnodePayments::ProcessMessage strCommand=%s\n", strCommand);
    // Ignore any payments messages until tnode list is synced
    if (!tnodeSync.IsTnodeListSynced()) return;

    if (fLiteMode) return; // disable all Zcoin specific functionality

    bool fTestNet = (Params().NetworkIDString() == CBaseChainParams::TESTNET || Params().NetworkIDString() == CBaseChainParams::REGTEST);

    bool fTestNet = (Params().NetworkIDString() == CBaseChainParams::TESTNET);

    if (strCommand == NetMsgType::TNODEPAYMENTSYNC) { //Tnode Payments Request Sync

        // Ignore such requests until we are fully synced.
        // We could start processing this after tnode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!tnodeSync.IsSynced()) return;

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (netfulfilledman.HasFulfilledRequest(pfrom->addr, NetMsgType::TNODEPAYMENTSYNC)) {
            // Asking for the payments list multiple times in a short period of time is no good
            LogPrintf("TNODEPAYMENTSYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
            if (!fTestNet) Misbehaving(pfrom->GetId(), 20);
            return;
        }
        netfulfilledman.AddFulfilledRequest(pfrom->addr, NetMsgType::TNODEPAYMENTSYNC);

        Sync(pfrom);
        LogPrint("tnpayments", "TNODEPAYMENTSYNC -- Sent Tnode payment votes to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::TNODEPAYMENTVOTE) { // Tnode Payments Vote for the Winner

        CTnodePaymentVote vote;
        vRecv >> vote;

        if (pfrom->nVersion < GetMinTnodePaymentsProto()) return;

        if (!pCurrentBlockIndex) return;

        uint256 nHash = vote.GetHash();

        pfrom->setAskFor.erase(nHash);

        {
            LOCK(cs_mapTnodePaymentVotes);
            if (mapTnodePaymentVotes.count(nHash)) {
                LogPrint("tnpayments", "TNODEPAYMENTVOTE -- hash=%s, nHeight=%d seen\n", nHash.ToString(), pCurrentBlockIndex->nHeight);
                return;
            }

            // Avoid processing same vote multiple times
            mapTnodePaymentVotes[nHash] = vote;
            // but first mark vote as non-verified,
            // AddPaymentVote() below should take care of it if vote is actually ok
            mapTnodePaymentVotes[nHash].MarkAsNotVerified();
        }

        int nFirstBlock = pCurrentBlockIndex->nHeight - GetStorageLimit();
        if (vote.nBlockHeight < nFirstBlock || vote.nBlockHeight > pCurrentBlockIndex->nHeight + 20) {
            LogPrint("tnpayments", "TNODEPAYMENTVOTE -- vote out of range: nFirstBlock=%d, nBlockHeight=%d, nHeight=%d\n", nFirstBlock, vote.nBlockHeight, pCurrentBlockIndex->nHeight);
            return;
        }

        std::string strError = "";
        if (!vote.IsValid(pfrom, pCurrentBlockIndex->nHeight, strError)) {
            LogPrint("tnpayments", "TNODEPAYMENTVOTE -- invalid message, error: %s\n", strError);
            return;
        }

        if (!CanVote(vote.vinTnode.prevout, vote.nBlockHeight)) {
            LogPrintf("TNODEPAYMENTVOTE -- tnode already voted, tnode=%s\n", vote.vinTnode.prevout.ToStringShort());
            return;
        }

        tnode_info_t mnInfo = mnodeman.GetTnodeInfo(vote.vinTnode);
        if (!mnInfo.fInfoValid) {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrintf("TNODEPAYMENTVOTE -- tnode is missing %s\n", vote.vinTnode.prevout.ToStringShort());
            mnodeman.AskForMN(pfrom, vote.vinTnode);
            return;
        }

        int nDos = 0;
        if (!vote.CheckSignature(mnInfo.pubKeyTnode, pCurrentBlockIndex->nHeight, nDos)) {
            if (nDos) {
                LogPrintf("TNODEPAYMENTVOTE -- ERROR: invalid signature\n");
                if (!fTestNet) Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogPrint("tnpayments", "TNODEPAYMENTVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            mnodeman.AskForMN(pfrom, vote.vinTnode);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        CTxDestination address1;
        ExtractDestination(vote.payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("tnpayments", "TNODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s\n", address2.ToString(), vote.nBlockHeight, pCurrentBlockIndex->nHeight, vote.vinTnode.prevout.ToStringShort());

        if (AddPaymentVote(vote)) {
            vote.Relay();
            tnodeSync.AddedPaymentVote();
        }
    }
}

bool CTnodePaymentVote::Sign() {
    std::string strError;
    std::string strMessage = vinTnode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             ScriptToAsmStr(payee);

    if (!darkSendSigner.SignMessage(strMessage, vchSig, activeTnode.keyTnode)) {
        LogPrintf("CTnodePaymentVote::Sign -- SignMessage() failed\n");
        return false;
    }

    if (!darkSendSigner.VerifyMessage(activeTnode.pubKeyTnode, vchSig, strMessage, strError)) {
        LogPrintf("CTnodePaymentVote::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CTnodePayments::GetBlockPayee(int nBlockHeight, CScript &payee) {
    if (mapTnodeBlocks.count(nBlockHeight)) {
        return mapTnodeBlocks[nBlockHeight].GetBestPayee(payee);
    }

    return false;
}

// Is this tnode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CTnodePayments::IsScheduled(CTnode &mn, int nNotBlockHeight) {
    LOCK(cs_mapTnodeBlocks);

    if (!pCurrentBlockIndex) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int64_t h = pCurrentBlockIndex->nHeight; h <= pCurrentBlockIndex->nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapTnodeBlocks.count(h) && mapTnodeBlocks[h].GetBestPayee(payee) && mnpayee == payee) {
            return true;
        }
    }

    return false;
}

bool CTnodePayments::AddPaymentVote(const CTnodePaymentVote &vote) {
    LogPrint("tnode-payments", "CTnodePayments::AddPaymentVote\n");
    uint256 blockHash = uint256();
    if (!GetBlockHash(blockHash, vote.nBlockHeight - 101)) return false;

    if (HasVerifiedPaymentVote(vote.GetHash())) return false;

    LOCK2(cs_mapTnodeBlocks, cs_mapTnodePaymentVotes);

    mapTnodePaymentVotes[vote.GetHash()] = vote;

    if (!mapTnodeBlocks.count(vote.nBlockHeight)) {
        CTnodeBlockPayees blockPayees(vote.nBlockHeight);
        mapTnodeBlocks[vote.nBlockHeight] = blockPayees;
    }

    mapTnodeBlocks[vote.nBlockHeight].AddPayee(vote);

    return true;
}

bool CTnodePayments::HasVerifiedPaymentVote(uint256 hashIn) {
    LOCK(cs_mapTnodePaymentVotes);
    std::map<uint256, CTnodePaymentVote>::iterator it = mapTnodePaymentVotes.find(hashIn);
    return it != mapTnodePaymentVotes.end() && it->second.IsVerified();
}

void CTnodeBlockPayees::AddPayee(const CTnodePaymentVote &vote) {
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CTnodePayee & payee, vecPayees)
    {
        if (payee.GetPayee() == vote.payee) {
            payee.AddVoteHash(vote.GetHash());
            return;
        }
    }
    CTnodePayee payeeNew(vote.payee, vote.GetHash());
    vecPayees.push_back(payeeNew);
}

bool CTnodeBlockPayees::GetBestPayee(CScript &payeeRet) {
    LOCK(cs_vecPayees);
    LogPrint("tnpayments", "CTnodeBlockPayees::GetBestPayee, vecPayees.size()=%s\n", vecPayees.size());
    if (!vecPayees.size()) {
        LogPrint("tnpayments", "CTnodeBlockPayees::GetBestPayee -- ERROR: couldn't find any payee\n");
        return false;
    }

    int nVotes = -1;
    BOOST_FOREACH(CTnodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() > nVotes) {
            payeeRet = payee.GetPayee();
            nVotes = payee.GetVoteCount();
        }
    }

    return (nVotes > -1);
}

bool CTnodeBlockPayees::HasPayeeWithVotes(CScript payeeIn, int nVotesReq) {
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CTnodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() >= nVotesReq && payee.GetPayee() == payeeIn) {
            return true;
        }
    }

//    LogPrint("tnpayments", "CTnodeBlockPayees::HasPayeeWithVotes -- ERROR: couldn't find any payee with %d+ votes\n", nVotesReq);
    return false;
}

bool CTnodeBlockPayees::IsTransactionValid(const CTransaction &txNew, bool fMTP) {
    LOCK(cs_vecPayees);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";

    CAmount nTnodePayment = GetTnodePayment(Params().GetConsensus(), fMTP);

    //require at least TNPAYMENTS_SIGNATURES_REQUIRED signatures

    BOOST_FOREACH(CTnodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() >= nMaxSignatures) {
            nMaxSignatures = payee.GetVoteCount();
        }
    }

    // if we don't have at least TNPAYMENTS_SIGNATURES_REQUIRED signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < TNPAYMENTS_SIGNATURES_REQUIRED) return true;

    bool hasValidPayee = false;

    BOOST_FOREACH(CTnodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() >= TNPAYMENTS_SIGNATURES_REQUIRED) {
            hasValidPayee = true;

            BOOST_FOREACH(CTxOut txout, txNew.vout) {
                if (payee.GetPayee() == txout.scriptPubKey && nTnodePayment == txout.nValue) {
                    LogPrint("tnpayments", "CTnodeBlockPayees::IsTransactionValid -- Found required payment\n");
                    return true;
                }
            }

            CTxDestination address1;
            ExtractDestination(payee.GetPayee(), address1);
            CBitcoinAddress address2(address1);

            if (strPayeesPossible == "") {
                strPayeesPossible = address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    if (!hasValidPayee) return true;

    LogPrintf("CTnodeBlockPayees::IsTransactionValid -- ERROR: Missing required payment, possible payees: '%s', amount: %f TCR\n", strPayeesPossible, (float) nTnodePayment / COIN);
    return false;
}

std::string CTnodeBlockPayees::GetRequiredPaymentsString() {
    LOCK(cs_vecPayees);

    std::string strRequiredPayments = "Unknown";

    BOOST_FOREACH(CTnodePayee & payee, vecPayees)
    {
        CTxDestination address1;
        ExtractDestination(payee.GetPayee(), address1);
        CBitcoinAddress address2(address1);

        if (strRequiredPayments != "Unknown") {
            strRequiredPayments += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        } else {
            strRequiredPayments = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        }
    }

    return strRequiredPayments;
}

std::string CTnodePayments::GetRequiredPaymentsString(int nBlockHeight) {
    LOCK(cs_mapTnodeBlocks);

    if (mapTnodeBlocks.count(nBlockHeight)) {
        return mapTnodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CTnodePayments::IsTransactionValid(const CTransaction &txNew, int nBlockHeight, bool fMTP) {
    LOCK(cs_mapTnodeBlocks);

    if (mapTnodeBlocks.count(nBlockHeight)) {
        return mapTnodeBlocks[nBlockHeight].IsTransactionValid(txNew, fMTP);
    }

    return true;
}

void CTnodePayments::CheckAndRemove() {
    if (!pCurrentBlockIndex) return;

    LOCK2(cs_mapTnodeBlocks, cs_mapTnodePaymentVotes);

    int nLimit = GetStorageLimit();

    std::map<uint256, CTnodePaymentVote>::iterator it = mapTnodePaymentVotes.begin();
    while (it != mapTnodePaymentVotes.end()) {
        CTnodePaymentVote vote = (*it).second;

        if (pCurrentBlockIndex->nHeight - vote.nBlockHeight > nLimit) {
            LogPrint("tnpayments", "CTnodePayments::CheckAndRemove -- Removing old Tnode payment: nBlockHeight=%d\n", vote.nBlockHeight);
            mapTnodePaymentVotes.erase(it++);
            mapTnodeBlocks.erase(vote.nBlockHeight);
        } else {
            ++it;
        }
    }
    LogPrintf("CTnodePayments::CheckAndRemove -- %s\n", ToString());
}

bool CTnodePaymentVote::IsValid(CNode *pnode, int nValidationHeight, std::string &strError) {
    CTnode *pmn = mnodeman.Find(vinTnode);

    if (!pmn) {
        strError = strprintf("Unknown Tnode: prevout=%s", vinTnode.prevout.ToStringShort());
        // Only ask if we are already synced and still have no idea about that Tnode
        if (tnodeSync.IsTnodeListSynced()) {
            mnodeman.AskForMN(pnode, vinTnode);
        }

        return false;
    }

    int nMinRequiredProtocol;
    if (nBlockHeight >= nValidationHeight) {
        // new votes must comply SPORK_10_TNODE_PAY_UPDATED_NODES rules
        nMinRequiredProtocol = tnpayments.GetMinTnodePaymentsProto();
    } else {
        // allow non-updated tnodes for old blocks
        nMinRequiredProtocol = MIN_TNODE_PAYMENT_PROTO_VERSION_1;
    }

    if (pmn->nProtocolVersion < nMinRequiredProtocol) {
        strError = strprintf("Tnode protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d", pmn->nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only tnodes should try to check tnode rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify tnode rank for future block votes only.
    if (!fTnodeMode && nBlockHeight < nValidationHeight) return true;

    int nRank = mnodeman.GetTnodeRank(vinTnode, nBlockHeight - 101, nMinRequiredProtocol, false);

    if (nRank == -1) {
        LogPrint("tnpayments", "CTnodePaymentVote::IsValid -- Can't calculate rank for tnode %s\n",
                 vinTnode.prevout.ToStringShort());
        return false;
    }

    if (nRank > TNPAYMENTS_SIGNATURES_TOTAL) {
        // It's common to have tnodes mistakenly think they are in the top 10
        // We don't want to print all of these messages in normal mode, debug mode should print though
        strError = strprintf("Tnode is not in the top %d (%d)", TNPAYMENTS_SIGNATURES_TOTAL, nRank);
        // Only ban for new mnw which is out of bounds, for old mnw MN list itself might be way too much off
        if (nRank > TNPAYMENTS_SIGNATURES_TOTAL * 2 && nBlockHeight > nValidationHeight) {
            strError = strprintf("Tnode is not in the top %d (%d)", TNPAYMENTS_SIGNATURES_TOTAL * 2, nRank);
            LogPrintf("CTnodePaymentVote::IsValid -- Error: %s\n", strError);
            Misbehaving(pnode->GetId(), 20);
        }
        // Still invalid however
        return false;
    }

    return true;
}

bool CTnodePayments::ProcessBlock(int nBlockHeight) {

    // DETERMINE IF WE SHOULD BE VOTING FOR THE NEXT PAYEE

    if (fLiteMode || !fTnodeMode) {
        return false;
    }

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about tnodes.
    if (!tnodeSync.IsTnodeListSynced()) {
        return false;
    }

    int nRank = mnodeman.GetTnodeRank(activeTnode.vin, nBlockHeight - 101, GetMinTnodePaymentsProto(), false);

    if (nRank == -1) {
        LogPrint("tnpayments", "CTnodePayments::ProcessBlock -- Unknown Tnode\n");
        return false;
    }

    if (nRank > TNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("tnpayments", "CTnodePayments::ProcessBlock -- Tnode not in the top %d (%d)\n", TNPAYMENTS_SIGNATURES_TOTAL, nRank);
        return false;
    }

    // LOCATE THE NEXT TNODE WHICH SHOULD BE PAID

    LogPrintf("CTnodePayments::ProcessBlock -- Start: nBlockHeight=%d, tnode=%s\n", nBlockHeight, activeTnode.vin.prevout.ToStringShort());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    CTnode *pmn = mnodeman.GetNextTnodeInQueueForPayment(nBlockHeight, true, nCount);

    if (pmn == NULL) {
        LogPrintf("CTnodePayments::ProcessBlock -- ERROR: Failed to find tnode to pay\n");
        return false;
    }

    LogPrintf("CTnodePayments::ProcessBlock -- Tnode found by GetNextTnodeInQueueForPayment(): %s\n", pmn->vin.prevout.ToStringShort());


    CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());

    CTnodePaymentVote voteNew(activeTnode.vin, nBlockHeight, payee);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);

    // SIGN MESSAGE TO NETWORK WITH OUR TNODE KEYS

    if (voteNew.Sign()) {
        if (AddPaymentVote(voteNew)) {
            voteNew.Relay();
            return true;
        }
    }

    return false;
}

void CTnodePaymentVote::Relay() {
    // do not relay until synced
    if (!tnodeSync.IsWinnersListSynced()) {
        LogPrint("tnode", "CTnodePaymentVote::Relay - tnodeSync.IsWinnersListSynced() not sync\n");
        return;
    }
    CInv inv(MSG_TNODE_PAYMENT_VOTE, GetHash());
    g_connman->RelayInv(inv);
}

bool CTnodePaymentVote::CheckSignature(const CPubKey &pubKeyTnode, int nValidationHeight, int &nDos) {
    // do not ban by default
    nDos = 0;

    std::string strMessage = vinTnode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             ScriptToAsmStr(payee);

    std::string strError = "";
    if (!darkSendSigner.VerifyMessage(pubKeyTnode, vchSig, strMessage, strError)) {
        // Only ban for future block vote when we are already synced.
        // Otherwise it could be the case when MN which signed this vote is using another key now
        // and we have no idea about the old one.
        if (tnodeSync.IsTnodeListSynced() && nBlockHeight > nValidationHeight) {
            nDos = 20;
        }
        return error("CTnodePaymentVote::CheckSignature -- Got bad Tnode payment signature, tnode=%s, error: %s", vinTnode.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

std::string CTnodePaymentVote::ToString() const {
    std::ostringstream info;

    info << vinTnode.prevout.ToStringShort() <<
         ", " << nBlockHeight <<
         ", " << ScriptToAsmStr(payee) <<
         ", " << (int) vchSig.size();

    return info.str();
}

// Send only votes for future blocks, node should request every other missing payment block individually
void CTnodePayments::Sync(CNode *pnode) {
    LOCK(cs_mapTnodeBlocks);

    if (!pCurrentBlockIndex) return;

    int nInvCount = 0;

    for (int h = pCurrentBlockIndex->nHeight; h < pCurrentBlockIndex->nHeight + 20; h++) {
        if (mapTnodeBlocks.count(h)) {
            BOOST_FOREACH(CTnodePayee & payee, mapTnodeBlocks[h].vecPayees)
            {
                std::vector <uint256> vecVoteHashes = payee.GetVoteHashes();
                BOOST_FOREACH(uint256 & hash, vecVoteHashes)
                {
                    if (!HasVerifiedPaymentVote(hash)) continue;
                    pnode->PushInventory(CInv(MSG_TNODE_PAYMENT_VOTE, hash));
                    nInvCount++;
                }
            }
        }
    }

    LogPrintf("CTnodePayments::Sync -- Sent %d votes to peer %d\n", nInvCount, pnode->id);
    g_connman->PushMessage(pnode, CNetMsgMaker(LEGACY_TNODES_PROTOCOL_VERSION).Make(NetMsgType::SYNCSTATUSCOUNT, TNODE_SYNC_MNW, nInvCount));
}

// Request low data/unknown payment blocks in batches directly from some node instead of/after preliminary Sync.
void CTnodePayments::RequestLowDataPaymentBlocks(CNode *pnode) {
    if (!pCurrentBlockIndex) return;

    LOCK2(cs_main, cs_mapTnodeBlocks);

    std::vector <CInv> vToFetch;
    int nLimit = GetStorageLimit();

    const CBlockIndex *pindex = pCurrentBlockIndex;

    while (pCurrentBlockIndex->nHeight - pindex->nHeight < nLimit) {
        if (!mapTnodeBlocks.count(pindex->nHeight)) {
            // We have no idea about this block height, let's ask
            vToFetch.push_back(CInv(MSG_TNODE_PAYMENT_BLOCK, pindex->GetBlockHash()));
            // We should not violate GETDATA rules
            if (vToFetch.size() == MAX_INV_SZ) {
                LogPrintf("CTnodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d blocks\n", pnode->id, MAX_INV_SZ);
                g_connman->PushMessage(pnode, CNetMsgMaker(LEGACY_TNODES_PROTOCOL_VERSION).Make(NetMsgType::GETDATA, vToFetch));
                // Start filling new batch
                vToFetch.clear();
            }
        }
        if (!pindex->pprev) break;
        pindex = pindex->pprev;
    }

    std::map<int, CTnodeBlockPayees>::iterator it = mapTnodeBlocks.begin();

    while (it != mapTnodeBlocks.end()) {
        int nTotalVotes = 0;
        bool fFound = false;
        BOOST_FOREACH(CTnodePayee & payee, it->second.vecPayees)
        {
            if (payee.GetVoteCount() >= TNPAYMENTS_SIGNATURES_REQUIRED) {
                fFound = true;
                break;
            }
            nTotalVotes += payee.GetVoteCount();
        }
        // A clear winner (TNPAYMENTS_SIGNATURES_REQUIRED+ votes) was found
        // or no clear winner was found but there are at least avg number of votes
        if (fFound || nTotalVotes >= (TNPAYMENTS_SIGNATURES_TOTAL + TNPAYMENTS_SIGNATURES_REQUIRED) / 2) {
            // so just move to the next block
            ++it;
            continue;
        }
        // DEBUG
//        DBG (
//            // Let's see why this failed
//            BOOST_FOREACH(CTnodePayee& payee, it->second.vecPayees) {
//                CTxDestination address1;
//                ExtractDestination(payee.GetPayee(), address1);
//                CBitcoinAddress address2(address1);
//                printf("payee %s votes %d\n", address2.ToString().c_str(), payee.GetVoteCount());
//            }
//            printf("block %d votes total %d\n", it->first, nTotalVotes);
//        )
        // END DEBUG
        // Low data block found, let's try to sync it
        uint256 hash;
        if (GetBlockHash(hash, it->first)) {
            vToFetch.push_back(CInv(MSG_TNODE_PAYMENT_BLOCK, hash));
        }
        // We should not violate GETDATA rules
        if (vToFetch.size() == MAX_INV_SZ) {
            LogPrintf("CTnodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, MAX_INV_SZ);
            g_connman->PushMessage(pnode, CNetMsgMaker(LEGACY_TNODES_PROTOCOL_VERSION).Make(NetMsgType::GETDATA, vToFetch));
            // Start filling new batch
            vToFetch.clear();
        }
        ++it;
    }
    // Ask for the rest of it
    if (!vToFetch.empty()) {
        LogPrintf("CTnodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, vToFetch.size());
        g_connman->PushMessage(pnode, CNetMsgMaker(LEGACY_TNODES_PROTOCOL_VERSION).Make(NetMsgType::GETDATA, vToFetch));
    }
}

std::string CTnodePayments::ToString() const {
    std::ostringstream info;

    info << "Votes: " << (int) mapTnodePaymentVotes.size() <<
         ", Blocks: " << (int) mapTnodeBlocks.size();

    return info.str();
}

bool CTnodePayments::IsEnoughData() {
    float nAverageVotes = (TNPAYMENTS_SIGNATURES_TOTAL + TNPAYMENTS_SIGNATURES_REQUIRED) / 2;
    int nStorageLimit = GetStorageLimit();
    return GetBlockCount() > nStorageLimit && GetVoteCount() > nStorageLimit * nAverageVotes;
}

int CTnodePayments::GetStorageLimit() {
    return std::max(int(mnodeman.size() * nStorageCoeff), nMinBlocksToStore);
}

void CTnodePayments::UpdatedBlockTip(const CBlockIndex *pindex) {
    pCurrentBlockIndex = pindex;
    LogPrint("tnpayments", "CTnodePayments::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);
    
    ProcessBlock(pindex->nHeight + 5);
}

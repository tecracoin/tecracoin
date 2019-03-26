// Copyright (c) 2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zpivchain.h"
//#include "invalid.h"
#include "main.h"
#include "init.h"
#include "txdb.h"
#include "ui_interface.h"
#include "zerocoin.h"
#include "wallet/wallet.h"

// 6 comes from OPCODE (1) + vch.size() (1) + BIGNUM size (4)
#define SCRIPT_OFFSET 6
// For Script size (BIGNUM/Uint256 size)
#define BIGNUM_SIZE   4

bool BlockToMintValueVector(const CBlock& block, const libzerocoin::CoinDenomination denom, vector<CBigNum>& vValues)
{
    for (const CTransaction& tx : block.vtx) {
        if(!tx.IsZerocoinMint())
            continue;

        for (const CTxOut& txOut : tx.vout) {
            if(!txOut.scriptPubKey.IsZerocoinMint())
                continue;

            CValidationState state;
            libzerocoin::PublicCoin coin(ZCParamsV2);
            if(!TxOutToPublicCoin(txOut, coin, state))
                return false;

            if (coin.getDenomination() != denom)
                continue;

            vValues.push_back(coin.getValue());
        }
    }

    return true;
}

bool BlockToPubcoinList(const CBlock& block, std::list<libzerocoin::PublicCoin>& listPubcoins)
{
    for (const CTransaction& tx : block.vtx) {
        if(!tx.IsZerocoinMint())
            continue;

        for (unsigned int i = 0; i < tx.vout.size(); i++) {
            const CTxOut txOut = tx.vout[i];
            if(!txOut.scriptPubKey.IsZerocoinMint())
                continue;

            CValidationState state;
            libzerocoin::PublicCoin pubCoin(ZCParamsV2);
            if(!TxOutToPublicCoin(txOut, pubCoin, state))
                return false;

            listPubcoins.emplace_back(pubCoin);
        }
    }

    return true;
}

//return a list of zerocoin mints contained in a specific block
bool BlockToZerocoinMintList(const CBlock& block, std::list<CZerocoinEntry>& vMints)
{
    for (const CTransaction& tx : block.vtx) {
        if(!tx.IsZerocoinMint())
            continue;

        for (unsigned int i = 0; i < tx.vout.size(); i++) {
            const CTxOut txOut = tx.vout[i];
            if(!txOut.scriptPubKey.IsZerocoinMint())
                continue;

            CValidationState state;
            libzerocoin::PublicCoin pubCoin(ZCParamsV2);
            if(!TxOutToPublicCoin(txOut, pubCoin, state))
                return false;

            Bignum value = const_cast<Bignum&>(pubCoin.getValue());
            Bignum randomness = 0;
            Bignum serialNumber = 0;
            CZerocoinEntry zerocoin = CZerocoinEntry(pubCoin.getDenomination(), value, randomness, serialNumber, false);
            vMints.push_back(zerocoin);
        }
    }

    return true;
}

bool IsSerialKnown(const CBigNum& bnSerial)
{
    return CZerocoinState::GetZerocoinState()->IsUsedCoinSerial(bnSerial);
}

bool IsSerialInBlockchain(const CBigNum& bnSerial, int& nHeightTx)
{
    uint256 txHash;
    txHash.SetNull();
    // if not in zerocoinState then its not in the blockchain
    if (!CZerocoinState::GetZerocoinState()->IsUsedCoinSerial(bnSerial))
        return false;

    return IsTransactionInChain(txHash, nHeightTx);
}

bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend, CTransaction& tx)
{
    txidSpend.SetNull();
    CMintMeta mMeta;

    if (!CZerocoinState::GetZerocoinState()->IsUsedCoinSerialHash(hashSerial))
        return false;

    if(!pwalletMain->zpivTracker->Get(hashSerial, mMeta))
        return false;

    txidSpend = mMeta.txid;

    return IsTransactionInChain(txidSpend, nHeightTx, tx);
}

libzerocoin::CoinSpend TxInToZerocoinSpend(const CTxIn& txin)
{
    // extract the CoinSpend from the txin
    std::vector<char, zero_after_free_allocator<char> > dataTxIn;
    dataTxIn.insert(dataTxIn.end(), txin.scriptSig.begin() + BIGNUM_SIZE, txin.scriptSig.end());
    CDataStream serializedCoinSpend(dataTxIn, SER_NETWORK, PROTOCOL_VERSION);
    //bool fModulusV2 = (txin.nSequence >= ZC_MODULUS_V2_BASE_ID);
    //libzerocoin::Params* paramsAccumulator = fModulusV2 ? ZCParamsV2 : ZCParams;
    libzerocoin::CoinSpend spend(ZCParamsV2, serializedCoinSpend);

    return spend;
}

bool TxOutToPublicCoin(const CTxOut& txout, libzerocoin::PublicCoin& pubCoin, CValidationState& state)
{
    CBigNum publicZerocoin;
    vector<unsigned char> vchZeroMint;
    vchZeroMint.insert(vchZeroMint.end(), txout.scriptPubKey.begin() + SCRIPT_OFFSET,
                       txout.scriptPubKey.begin() + txout.scriptPubKey.size());
    publicZerocoin.setvch(vchZeroMint);

    libzerocoin::CoinDenomination denomination = libzerocoin::AmountToZerocoinDenomination(txout.nValue);
    LogPrint("zero", "%s ZCPRINT denomination %d pubcoin %s\n", __func__, denomination, publicZerocoin.GetHex());
    if (denomination == libzerocoin::ZQ_ERROR)
        return state.DoS(100, error("TxOutToPublicCoin : txout.nValue is not correct"));

    libzerocoin::PublicCoin checkPubCoin(ZCParamsV2, publicZerocoin, denomination);
    pubCoin = checkPubCoin;

    return true;
}

//return a list of zerocoin spends contained in a specific block, list may have many denominations
std::list<libzerocoin::CoinDenomination> ZerocoinSpendListFromBlock(const CBlock& block)
{
    std::list<libzerocoin::CoinDenomination> vSpends;
    for (const CTransaction& tx : block.vtx) {
        if (!tx.IsZerocoinSpend())
            continue;

        for (const CTxIn& txin : tx.vin) {
            if (!txin.scriptSig.IsZerocoinSpend())
                continue;

            libzerocoin::CoinDenomination c = libzerocoin::IntToZerocoinDenomination(txin.nSequence);
            vSpends.push_back(c);
        }
    }
    return vSpends;
}


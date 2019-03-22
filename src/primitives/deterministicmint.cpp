// Copyright (c) 2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//#include <libzerocoin/Coin.h>
#include <tinyformat.h>
#include "deterministicmint.h"

using namespace libzerocoin;

CDeterministicMint::CDeterministicMint()
{
    SetNull();
}

CDeterministicMint::CDeterministicMint(uint8_t nVersion, const uint32_t& nCount, const uint256& hashSeed, const uint256& hashSerial, const Bignum& pubcoin)
{
    SetNull();
    this->nVersion = nVersion;
    this->nCount = nCount;
    this->hashSeed = hashSeed;
    this->hashSerial = hashSerial;
    this->pubcoin = pubcoin;
}

void CDeterministicMint::SetNull()
{
    nCount = 0;
    hashSeed.SetNull();
    hashSerial.SetNull();
    txid.SetNull();
    nHeight = 0;
    denom = CoinDenomination::ZQ_ERROR;
    isUsed = false;
}

std::string CDeterministicMint::ToString() const
{
    return strprintf(" DeterministicMint:\n   version=%d\n   count=%d\n   hashseed=%s\n   hashSerial=%s\n   hashPubcoin=%s\n   txid=%s\n   height=%d\n   denom=%d\n   isUsed=%d\n",
    nVersion, nCount, hashSeed.GetHex(), hashSerial.GetHex(), GetPubCoinHash(pubcoin).GetHex(), txid.GetHex(), nHeight, denom, isUsed);
}

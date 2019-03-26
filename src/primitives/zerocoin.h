// Copyright (c) 2017-2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PRIMITIVES_ZEROCOIN_H
#define PRIMITIVES_ZEROCOIN_H

#include <amount.h>
#include <limits.h>
#include "libzerocoin/bitcoin_bignum/bignum.h"
#include "libzerocoin/Zerocoin.h"
#include "key.h"
#include "serialize.h"
#include "zerocoin_params.h"

//struct that is safe to store essential mint data, without holding any information that allows for actual spending (serial, randomness, private key)
struct CMintMeta
{
    int nHeight;
    CBigNum pubcoin;
    uint256 hashSerial;
    uint8_t nVersion;
    libzerocoin::CoinDenomination denom;
    uint256 txid;
    bool isUsed;
    bool isArchived;
    bool isDeterministic;
    bool isSeedCorrect;

    bool operator <(const CMintMeta& a) const;
};

uint256 GetSerialHash(const CBigNum& bnSerial);
uint256 GetPubCoinHash(const CBigNum& bnValue);

class CZerocoinEntry
{
private:
    template <typename Stream>
    auto is_eof_helper(Stream &s, bool) -> decltype(s.eof()) {
        return s.eof();
    }

    template <typename Stream>
    bool is_eof_helper(Stream &s, int) {
        return false;
    }

    template<typename Stream>
    bool is_eof(Stream &s) {
        return is_eof_helper(s, true);
    }

public:
    //public
    Bignum value;
    int denomination;
    //private
    Bignum randomness;
    Bignum serialNumber;
    vector<unsigned char> ecdsaSecretKey;

    bool IsUsed;
    int nHeight;
    int id;

    CZerocoinEntry(const CZerocoinEntry& other) {
        denomination = other.denomination;
        nHeight = other.nHeight;
        value = other.value;
        randomness = other.randomness;
        serialNumber = other.serialNumber;
        IsUsed = other.IsUsed;
        ecdsaSecretKey = other.ecdsaSecretKey;
    }

    CZerocoinEntry(int denom, const CBigNum& value, const CBigNum& randomness, const CBigNum& serialNumber, bool isUsed)
    {
        SetNull();
        this->denomination = denom;
        this->value = value;
        this->randomness = randomness;
        this->serialNumber = serialNumber;
        this->IsUsed = isUsed;
    }

    CZerocoinEntry()
    {
        SetNull();
    }

    void SetNull()
    {
        IsUsed = false;
        randomness = 0;
        serialNumber = 0;
        value = 0;
        denomination = -1;
        nHeight = -1;
        id = -1;
    }

    bool IsCorrectV2Mint() const {
        return value > 0 && randomness > 0 && serialNumber > 0 && serialNumber.bitSize() <= 160 &&
                ecdsaSecretKey.size() >= 32;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(IsUsed);
        READWRITE(randomness);
        READWRITE(serialNumber);
        READWRITE(value);
        READWRITE(denomination);
        READWRITE(nHeight);
        READWRITE(id);
        if (ser_action.ForRead()) {
            if (!is_eof(s)) {
                int nStoredVersion = 0;
                READWRITE(nStoredVersion);
                if (nStoredVersion >= ZC_ADVANCED_WALLETDB_MINT_VERSION)
                    READWRITE(ecdsaSecretKey);
            }
        }
        else {
            READWRITE(nVersion);
            READWRITE(ecdsaSecretKey);
        }
    }

};


class CZerocoinSpendEntry
{
public:
    Bignum coinSerial;
    uint256 hashTx;
    Bignum pubCoin;
    int denomination;
    int id;

    CZerocoinSpendEntry()
    {
        SetNull();
    }

    void SetNull()
    {
        coinSerial = 0;
//        hashTx =
        pubCoin = 0;
        denomination = 0;
        id = 0;
    }
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(coinSerial);
        READWRITE(hashTx);
        READWRITE(pubCoin);
        READWRITE(denomination);
        READWRITE(id);
    }
};

#endif //PRIMITIVES_ZEROCOIN_H

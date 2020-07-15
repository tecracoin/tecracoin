// Copyright (c) 2020 The Zcoin Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sigmawalletv0.h"

#include "../wallet/wallet.h"

namespace exodus {

SigmaWalletV0::SigmaWalletV0() : SigmaWallet(new Database())
{
}

uint32_t SigmaWalletV0::BIP44ChangeIndex() const
{
    return BIP44_EXODUS_MINT_INDEX_V0;
}

SigmaPrivateKey SigmaWalletV0::GeneratePrivateKey(uint512 const &seed)
{
    SigmaPrivateKey priv;

    // first 32 bytes as seed
    uint256 serialSeed;
    std::copy(seed.begin(), seed.begin() + 32, serialSeed.begin());
    priv.serial.memberFromSeed(serialSeed.begin());

    // last 32 bytes as seed
    uint256 randomnessSeed;
    std::copy(seed.begin() + 32, seed.end(), randomnessSeed.begin());
    priv.randomness.memberFromSeed(randomnessSeed.begin());

    return priv;
}

SigmaWalletV0::Database::Database()
{
}

bool SigmaWalletV0::Database::WriteMint(SigmaMintId const &id, SigmaMint const &mint, CWalletDB *db)
{
    auto local = Connection(db);
    return local->WriteExodusMintV0(id, mint);
}

bool SigmaWalletV0::Database::ReadMint(SigmaMintId const &id, SigmaMint &mint, CWalletDB *db) const
{
    auto local = Connection(db);
    return local->ReadExodusMintV0(id, mint);
}

bool SigmaWalletV0::Database::EraseMint(SigmaMintId const &id, CWalletDB *db)
{
    auto local = Connection(db);
    return local->EraseExodusMintV0(id);
}

bool SigmaWalletV0::Database::HasMint(SigmaMintId const &id, CWalletDB *db) const
{
    auto local = Connection(db);
    return local->HasExodusMintV0(id);
}

bool SigmaWalletV0::Database::WriteMintId(uint160 const &hash, SigmaMintId const &mintId, CWalletDB *db)
{
    auto local = Connection(db);
    return local->WriteExodusMintIdV0(hash, mintId);
}

bool SigmaWalletV0::Database::ReadMintId(uint160 const &hash, SigmaMintId &mintId, CWalletDB *db) const
{
    auto local = Connection(db);
    return local->ReadExodusMintIdV0(hash, mintId);
}

bool SigmaWalletV0::Database::EraseMintId(uint160 const &hash, CWalletDB *db)
{
    auto local = Connection(db);
    return local->EraseExodusMintIdV0(hash);
}

bool SigmaWalletV0::Database::HasMintId(uint160 const &hash, CWalletDB *db) const
{
    auto local = Connection(db);
    return local->HasExodusMintIdV0(hash);
}

bool SigmaWalletV0::Database::WriteMintPool(std::vector<MintPoolEntry> const &mints, CWalletDB *db)
{
    auto local = Connection(db);
    return local->WriteExodusMintPoolV0(mints);
}

bool SigmaWalletV0::Database::ReadMintPool(std::vector<MintPoolEntry> &mints, CWalletDB *db)
{
    auto local = Connection(db);
    return local->ReadExodusMintPoolV0(mints);
}

void SigmaWalletV0::Database::ListMints(std::function<void(SigmaMintId&, SigmaMint&)> const &inserter, CWalletDB *db)
{
    auto local = Connection(db);
    local->ListExodusMintsV0<SigmaMintId, SigmaMint>(inserter);
}

}
#include "util.h"

#include <stdint.h>
#include <vector>

#include "chainparams.h"
#include "key.h"
#include "main.h"
#include "pubkey.h"
#include "txdb.h"
#include "txmempool.h"
#include "zerocoin_v3.h"

#include "test/fixtures.h"
#include "test/testutil.h"

#include "wallet/db.h"
#include "wallet/wallet.h"

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread.hpp>

BOOST_FIXTURE_TEST_SUITE(sigma_reorg, ZerocoinTestingSetup200)

/*
1. Add 2 more blocks to the chain with 2 mints and 2 spends in each.
2. Create another chain of blocks of length 3 such that a fork appears at the very start, and these 3 blocks replace the initial 2.
3. Make sure that the blockchain automatically switched to the new chain.
*/
BOOST_AUTO_TEST_CASE(sigma_reorg_test_simple_fork)
{
    CZerocoinStateV3 *zerocoinState = CZerocoinStateV3::GetZerocoinState();
    string denomination;
    vector<uint256> vtxid;

    // Create 400-200+1 = 201 new empty blocks. // consensus.nMintV3SigmaStartBlock = 400
    CreateAndProcessEmptyBlocks(201, scriptPubKey);

    denomination = "1";
    string stringError;
    // Make sure that transactions get to mempool
    pwalletMain->SetBroadcastTransactions(true);

    vector<pair<std::string, int>> denominationPairs;
    std::pair<std::string, int> denominationPair(denomination, 3);
    denominationPairs.push_back(denominationPair);

    // Create 6 sigma mints in 2 transaction.
    BOOST_CHECK_MESSAGE(pwalletMain->CreateZerocoinMintModel(
        stringError, denominationPairs, SIGMA), stringError + " - Create Mint failed");
    BOOST_CHECK_MESSAGE(pwalletMain->CreateZerocoinMintModel(
        stringError, denominationPairs, SIGMA), stringError + " - Create Mint failed");
    BOOST_CHECK_MESSAGE(mempool.size() == 2, "Mints were not added to mempool");

    vtxid.clear();
    mempool.queryHashes(vtxid);
    vtxid.resize(1);

    // Create a block with just 3 mints, but do not process it.
    CBlock block_with_3_mints = CreateBlock(vtxid, scriptPubKey);

    // All 2 transactions must be able to be added to the next block.
    int previousHeight = chainActive.Height();

    // Create a block with all 6 mints and process it.
    CBlock block_with_all_mints = CreateAndProcessBlock({}, scriptPubKey);
    BOOST_CHECK_MESSAGE(previousHeight + 1 == chainActive.Height(), "Block not added to chain");
    BOOST_CHECK_MESSAGE(mempool.size() == 0, "Expected empty mempool.");

    // Create 6 more empty blocks, to let the mints mature.
    CreateAndProcessEmptyBlocks(6, scriptPubKey);

    // Create 3 more mints, and 2 more spends.
    BOOST_CHECK_MESSAGE(pwalletMain->CreateZerocoinMintModel(
        stringError, denominationPairs, SIGMA), stringError + " - Create Mint failed");
    BOOST_CHECK_MESSAGE(mempool.size() == 1, "Mint was not added to mempool.");

    BOOST_CHECK_MESSAGE(pwalletMain->CreateZerocoinSpendModel(stringError, "", denomination.c_str()), "Spend sigma failed.");
    BOOST_CHECK_MESSAGE(pwalletMain->CreateZerocoinSpendModel(stringError, "", denomination.c_str()), "Spend sigma failed.");

    // There are 3 transaction, one will have 5 mints in it, and the other 2 will have 1 spend each.
    BOOST_CHECK_MESSAGE(mempool.size() == 3, "Spends not added to mempool.");

    previousHeight = chainActive.Height();

    CBlock block_with_spends = CreateAndProcessBlock({}, scriptPubKey);
    BOOST_CHECK_MESSAGE(previousHeight + 1 == chainActive.Height(), "Block not added to chain");

    // Disconnect the last 8 blocks, I.E. all the blocks with our mints and spends.
    DisconnectBlocks(8);

    // There must be 5 transactions in the mempool.
    BOOST_CHECK_MESSAGE(mempool.size() == 5, "Transactions not added back to mempool on blocks removal.");

    // Now create more blocks, using the same transactions. We can not create a block with 
    // all 4 transactions, because some of them are spends.
    previousHeight = chainActive.Height();
    BOOST_CHECK_MESSAGE(ProcessBlock(block_with_3_mints), "Block with 3 mints cannot be added back to chain");
    BOOST_CHECK_MESSAGE(previousHeight + 1 == chainActive.Height(), "Block not added to chain");

    vtxid.clear();
    mempool.clear();
    zerocoinState->Reset();
}

BOOST_AUTO_TEST_SUITE_END()

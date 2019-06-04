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
*/
BOOST_AUTO_TEST_CASE(sigma_reorg_test_simple_fork)
{
    sigma::CSigmaState *zerocoinState = sigma::CSigmaState::GetState();
    string denomination;
    vector<uint256> three_mints_ids;

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

    mempool.queryHashes(three_mints_ids);
    three_mints_ids.resize(1);

    // Create a block with just 3 mints, but do not process it.
    // CBlock block_with_3_mints = CreateBlock(three_mints_ids, scriptPubKey);

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

    {
        LOCK(cs_main);
        // Disconnect the last 8 blocks, I.E. all the blocks with our mints and spends.
        DisconnectBlocks(8);

        // Invalidate the first block of these 8, the one with 6 mints.
        // CValidationState state;
        // InvalidateBlock(state, Params(), mapBlockIndex[block_with_all_mints.GetHash()]);
    }

    // Now create more blocks, using the same transactions. We can not create a block with 
    // all 4 transactions, because some of them are spends.
    previousHeight = chainActive.Height();
    
    CBlock block_with_3_mints = CreateAndProcessBlock(three_mints_ids, scriptPubKey);
    BOOST_CHECK_MESSAGE(previousHeight + 1 == chainActive.Height(),
        "Block with 3 mints not added to chain");

    three_mints_ids.clear();
    mempool.clear();
    zerocoinState->Reset();
}

BOOST_AUTO_TEST_SUITE_END()

// Copyright (c) 2014-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "main.h"

#include "test/test_bitcoin.h"

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>
#include <miner.h>

#define MAIN_TESTS_INITIAL_SUBSIDY 112.5
#define MAIN_TESTS_PREMINE_SUBSIDY 21000000

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup)

static void TestBlockSubsidyHalvings(const Consensus::Params& consensusParams)
{
    int maxHalvings = 64;
    CAmount nInitialSubsidy = MAIN_TESTS_INITIAL_SUBSIDY * COIN;

    CAmount nPreviousSubsidy = nInitialSubsidy * 2; // for height == 0
    BOOST_CHECK_EQUAL(nPreviousSubsidy, nInitialSubsidy * 2);
    for (int nHalvings = 0; nHalvings < maxHalvings; nHalvings++) {
        int nHeight = nHalvings * consensusParams.nSubsidyHalvingInterval;
        int nTime = 1475020800;
        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams, nTime);
        BOOST_CHECK(nSubsidy <= nInitialSubsidy);
        if(nHeight > 0)
            BOOST_CHECK_EQUAL(nSubsidy, nPreviousSubsidy / 2);
        nPreviousSubsidy = nPreviousSubsidy / 2;
    }
    BOOST_CHECK_EQUAL(GetBlockSubsidy(maxHalvings * consensusParams.nSubsidyHalvingInterval, consensusParams), 0);
}

static void TestBlockSubsidyHalvings(int nSubsidyHalvingInterval)
{
    Consensus::Params consensusParams;
    consensusParams.nSubsidyHalvingInterval = nSubsidyHalvingInterval;
    TestBlockSubsidyHalvings(consensusParams);
}

static void TestRewardsStageStarts(const Consensus::Params &consensus){
    // Tnode payments must start before rewards2StageStart for proper founders rewards logic
    BOOST_CHECK(consensus.nTnodePaymentsStartBlock < consensus.rewardsStage2Start);
    BOOST_CHECK(consensus.rewardsStage2Start < consensus.rewardsStage3Start);
    BOOST_CHECK(consensus.rewardsStage3Start < consensus.rewardsStage4Start);
}

BOOST_AUTO_TEST_CASE(founders_reward_test)
{
    // Check premine
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1, Params(CBaseChainParams::MAIN).GetConsensus()), MAIN_TESTS_PREMINE_SUBSIDY * COIN);

    // Check rewards stages
    TestRewardsStageStarts(Params(CBaseChainParams::MAIN).GetConsensus());
    TestRewardsStageStarts(Params(CBaseChainParams::TESTNET).GetConsensus());
    TestRewardsStageStarts(Params(CBaseChainParams::REGTEST).GetConsensus());
}

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    TestBlockSubsidyHalvings(Params(CBaseChainParams::MAIN).GetConsensus());
    TestBlockSubsidyHalvings(Params(CBaseChainParams::TESTNET).GetConsensus());
    TestBlockSubsidyHalvings(Params(CBaseChainParams::REGTEST).GetConsensus());
    TestBlockSubsidyHalvings(150); // Just another interval
    TestBlockSubsidyHalvings(1000); // Just another interval
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    const Consensus::Params& consensusParams = Params(CBaseChainParams::MAIN).GetConsensus();
    CAmount nSum = 0;
    for (int nHeight = 0; nHeight < 27720000; nHeight += 1000) {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams);
        if(nHeight == 0)
            nSubsidy = MAIN_TESTS_INITIAL_SUBSIDY * COIN;
        BOOST_CHECK(nSubsidy <= MAIN_TESTS_INITIAL_SUBSIDY * COIN);
        nSum += nSubsidy * 1000;
        BOOST_CHECK(MoneyRange(nSum));
    }
    nSum += MAIN_TESTS_PREMINE_SUBSIDY * COIN;
    // should equal 210*10^6*COIN
    BOOST_CHECK_EQUAL(nSum, 20999999988240000ULL);
}

bool ReturnFalse() { return false; }
bool ReturnTrue() { return true; }

BOOST_AUTO_TEST_CASE(test_combiner_all)
{
    boost::signals2::signal<bool (), CombinerAll> Test;
    BOOST_CHECK(Test());
    Test.connect(&ReturnFalse);
    BOOST_CHECK(!Test());
    Test.connect(&ReturnTrue);
    BOOST_CHECK(!Test());
    Test.disconnect(&ReturnFalse);
    BOOST_CHECK(Test());
    Test.disconnect(&ReturnTrue);
    BOOST_CHECK(Test());
}
BOOST_AUTO_TEST_SUITE_END()

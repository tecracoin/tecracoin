// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018-2020 The TecraCoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"
#include "consensus/consensus.h"
#include "zerocoin_params.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"
#include "libzerocoin/bitcoin_bignum/bignum.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "chainparamsseeds.h"
#include "arith_uint256.h"
#include "base58.h"

static int64_t defaultPowTargetSpacing = 2.5 * 60; //2.5 minutes per block
static int64_t nDefaultSubsidyHalvingInterval = 840000; //every 4 years

static CBlock CreateGenesisBlock(const char *pszTimestamp, const CScript &genesisOutputScript, uint32_t nTime, uint32_t nNonce,
        uint32_t nBits, int32_t nVersion, const CAmount &genesisReward,
        std::vector<unsigned char> extraNonce) {
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 504365040 << CBigNum(4).getvch() << std::vector < unsigned char >
            ((const unsigned char *) pszTimestamp, (const unsigned char *) pszTimestamp + strlen(pszTimestamp)) << extraNonce;
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount &genesisReward,
                   std::vector<unsigned char> extraNonce, bool testnet=false) {
    //btzc: zcoin timestamp
    const char *pszTimestamp = !testnet? "The NY Times 2018/07/12 It Came From a Black Hole, and Landed in Antarctica":
    "The NY Times 2020/04/04 Staggered U.S. Braces for More Infections";
    const CScript genesisOutputScript = CScript();
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward,
                              extraNonce);
}

// this one is for testing only
static Consensus::LLMQParams llmq5_60 = {
        .type = Consensus::LLMQ_5_60,
        .name = "llmq_5_60",
        .size = 5,
        .minSize = 3,
        .threshold = 3,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 8,

        .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

        .keepOldConnections = 3,
};

// to use on testnet
static Consensus::LLMQParams llmq10_70 = {
        .type = Consensus::LLMQ_10_70,
        .name = "llmq_10_70",
        .size = 10,
        .minSize = 8,
        .threshold = 7,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 8,

        .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

        .keepOldConnections = 3,
};

static Consensus::LLMQParams llmq50_60 = {
        .type = Consensus::LLMQ_50_60,
        .name = "llmq_50_60",
        .size = 50,
        .minSize = 40,
        .threshold = 30,

        .dkgInterval = 18, // one DKG per 90 minutes
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 16,
        .dkgBadVotesThreshold = 40,

        .signingActiveQuorumCount = 16, // a full day worth of LLMQs

        .keepOldConnections = 17,
};

static Consensus::LLMQParams llmq400_60 = {
        .type = Consensus::LLMQ_400_60,
        .name = "llmq_400_60",
        .size = 400,
        .minSize = 300,
        .threshold = 240,

        .dkgInterval = 12 * 12, // one DKG every 12 hours
        .dkgPhaseBlocks = 4,
        .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 28,
        .dkgBadVotesThreshold = 300,

        .signingActiveQuorumCount = 4, // two days worth of LLMQs

        .keepOldConnections = 5,
};

// Used for deployment and min-proto-version signalling, so it needs a higher threshold
static Consensus::LLMQParams llmq400_85 = {
        .type = Consensus::LLMQ_400_85,
        .name = "llmq_400_85",
        .size = 400,
        .minSize = 350,
        .threshold = 340,

        .dkgInterval = 12 * 24, // one DKG every 24 hours
        .dkgPhaseBlocks = 4,
        .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 48, // give it a larger mining window to make sure it is mined
        .dkgBadVotesThreshold = 300,

        .signingActiveQuorumCount = 4, // two days worth of LLMQs

        .keepOldConnections = 5,
};


/**
 * Main network
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";

        consensus.chainType = Consensus::chainMain;
        consensus.nSubsidyHalvingInterval = nDefaultSubsidyHalvingInterval;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.nMinNFactor = 10;
        consensus.nMaxNFactor = 30;
        consensus.nChainStartTime = 1539907200;
        consensus.BIP34Height = 227931;
        consensus.BIP34Hash = uint256S("0x000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8");
        consensus.BIP65Height = INT_MAX; //OP_CHECKLOCKTIMEVERIFY
        consensus.BIP66Height = INT_MAX; //Strict DER signatures
        consensus.powLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 60 * 60; // 60 minutes between retargets
        consensus.nPowTargetSpacing = defaultPowTargetSpacing;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1475020800; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1462060800; // May 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1479168000; // November 15th, 2016.
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1510704000; // November 15th, 2017.

        // Deployment of MTP
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].bit = 12;
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].nStartTime = SWITCH_TO_MTP_BLOCK_HEADER - 2*60; // 2 hours leeway
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].nTimeout = SWITCH_TO_MTP_BLOCK_HEADER + consensus.nMinerConfirmationWindow*2 * 5*60;

        consensus.nCheckBugFixedAtBlock = ZC_CHECK_BUG_FIXED_AT_BLOCK;
        consensus.nTnodePaymentsBugFixedAtBlock = ZC_TNODE_PAYMENT_BUG_FIXED_AT_BLOCK;
        consensus.nSpendV15StartBlock = ZC_V1_5_STARTING_BLOCK;
        consensus.nSpendV2ID_1 = ZC_V2_SWITCH_ID_1;
        consensus.nSpendV2ID_10 = ZC_V2_SWITCH_ID_10;
        consensus.nSpendV2ID_25 = ZC_V2_SWITCH_ID_25;
        consensus.nSpendV2ID_50 = ZC_V2_SWITCH_ID_50;
        consensus.nSpendV2ID_100 = ZC_V2_SWITCH_ID_100;
        consensus.nModulusV2StartBlock = ZC_MODULUS_V2_START_BLOCK;
        consensus.nModulusV1MempoolStopBlock = ZC_MODULUS_V1_MEMPOOL_STOP_BLOCK;
        consensus.nModulusV1StopBlock = ZC_MODULUS_V1_STOP_BLOCK;
        consensus.nMultipleSpendInputsInOneTxStartBlock = ZC_MULTIPLE_SPEND_INPUT_STARTING_BLOCK;
        consensus.nDontAllowDupTxsStartBlock = 0;

        // tnode params
        consensus.nTnodeMinimumConfirmations = 15;
        consensus.nTnodePaymentsStartBlock = HF_TNODE_HEIGHT + 25 * 60 / 2.5;// 25h after tnode start; must be less than rewardsStageStage2Start
        consensus.nPremineSubsidy = 21000000; // 21mln TCR

        consensus.nMTPSwitchTime = SWITCH_TO_MTP_BLOCK_HEADER;

        // evo tnodes TecraCoin
        consensus.DIP0003Height = 450000; // Approximately 2021-01-16T15:40:34+00:00 unix ts 1610811634
        consensus.DIP0003EnforcementHeight = 470000; // Approx 2021-02-20T09:00:34+00:00 ts 1613811634
        consensus.DIP0008Height = INT_MAX; //chainlocks not ready
        consensus.nEvoTnodeMinimumConfirmations = 15;

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_50_60] = llmq50_60;
        consensus.llmqs[Consensus::LLMQ_400_60] = llmq400_60;
        consensus.llmqs[Consensus::LLMQ_400_85] = llmq400_85;
        consensus.nLLMQPowTargetSpacing = 5*60;

        consensus.nMTPSwitchTime = SWITCH_TO_MTP_BLOCK_HEADER;
        consensus.nMTPStartBlock = 192804; //TecraCoin

        consensus.nDisableZerocoinStartBlock = 108500;

        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour
        strSporkPubKey = "043e62180057b1fcbd3ca534f0a32ec83b967ae663a6fc7321ce0cf9f866ca909be062575c1aad9cd7ef0823938d0cc6b37161f9da5136731816db7e5794ec4063";

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
       `  * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x9e;
        pchMessageStart[1] = 0xce;
        pchMessageStart[2] = 0x3c;
        pchMessageStart[3] = 0x7c;

        nDefaultPort = 2718;
        nPruneAfterHeight = 100000;
        /**
         * btzc: tecracoin init genesis block
         * nBits = 0x1e0ffff0
         * nTime = 1539907200
         * nNonce = 317425
         * genesisReward = 0 * COIN
         * nVersion = 2
         * extraNonce
         */
        std::vector<unsigned char> extraNonce(4);
        extraNonce[0] = 0x65;
        extraNonce[1] = 0x2d;
        extraNonce[2] = 0x00;
        extraNonce[3] = 0x00;
        genesis = CreateGenesisBlock(ZC_GENESIS_BLOCK_TIME, 317425, 0x1e0ffff0, 2, 0 * COIN, extraNonce);
        const std::string s = genesis.GetHash().ToString();
        // std::cout << "tecracoin new hashMerkleRoot hash: " << genesis.hashMerkleRoot.ToString() << std::endl;
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000008c721bdb1312f1954156f64828a052e8e8ce5a914f7b301a44eba154989"));
        assert(genesis.hashMerkleRoot == uint256S("0x9cb610c4373619597a4e6e2bcf131a09f6aac19edcfbcdf5eb6185d53947f26d"));
        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("seed.tecracoin.io", "seed.tecracoin.io", false));
        vSeeds.push_back(CDNSSeedData("seed2.tecracoin.io", "seed2.tecracoin.io", false));

        // TecraCoin addresses start with 'T'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector < unsigned char > (1, 65);
        // TecraCoin script addresses start with 'B' or 'C'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector < unsigned char > (1, 27);
        // TecraCoin private keys start with 'Q'
        base58Prefixes[SECRET_KEY] = std::vector < unsigned char > (1, 58);
        // TecraCoin BIP32 pubkeys start with 'xpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container < std::vector < unsigned char > > ();
        // TecraCoin BIP32 pubkeys start with 'xpub' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container < std::vector < unsigned char > > ();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fAllowMultiplePorts = false;

        /**
         * What makes a good checkpoint block?
         * + Is surrounded by blocks with reasonable timestamps
         *   (no blocks before with a timestamp after, none after with
         *    timestamp before)
         * + Contains no strange transactions
         */
        checkpointData = (CCheckpointData) {
                boost::assign::map_list_of
                (0, uint256S("0x000008c721bdb1312f1954156f64828a052e8e8ce5a914f7b301a44eba154989"))
                (2500, uint256S("0x00000179620d5efd4770d98f43474fd54045d6e4723445cb1907e12b576ee14e"))
                (6860, uint256S("0x0000001a85edff4034839d410fd4efc6ed36a4e9b9a92ed399a1343acce44a32"))
                (291588, uint256S("0xa7d8afb46a810bc3a53cd7f036085a4d776f86bd035bf8d64eb82e27dfcbb32b"))
                (311766, uint256S("0x7d828dd2b55cef5dfc376177dda08a02e780d9fd5dea3f192671adadbd6632d2"))
                (398849, uint256S("0x3b1309eb011eaab8b86aca32d2d30df3c892f7374b4cc0e2a51ed06a57c126f2"))
        };

        chainTxData = ChainTxData{
                1603196462, // * UNIX timestamp of last checkpoint block
                454351,     // * total number of transactions between genesis and last checkpoint
                            //   (the tx=... number in the SetBestChain debug.log lines)
                0.014       // * estimated number of transactions per second after checkpoint
        };

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000001d2adb54c0eb6b1");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x3b1309eb011eaab8b86aca32d2d30df3c892f7374b4cc0e2a51ed06a57c126f2"); //398849

        consensus.nSpendV15StartBlock = ZC_V1_5_STARTING_BLOCK;
        consensus.nSpendV2ID_1 = ZC_V2_SWITCH_ID_1;
        consensus.nSpendV2ID_10 = ZC_V2_SWITCH_ID_10;
        consensus.nSpendV2ID_25 = ZC_V2_SWITCH_ID_25;
        consensus.nSpendV2ID_50 = ZC_V2_SWITCH_ID_50;
        consensus.nSpendV2ID_100 = ZC_V2_SWITCH_ID_100;
        consensus.nModulusV2StartBlock = ZC_MODULUS_V2_START_BLOCK;
        consensus.nModulusV1MempoolStopBlock = ZC_MODULUS_V1_MEMPOOL_STOP_BLOCK;
        consensus.nModulusV1StopBlock = ZC_MODULUS_V1_STOP_BLOCK;

        // Sigma related values. Tecra: TODO REMOVE
        consensus.nSigmaStartBlock = ZC_SIGMA_STARTING_BLOCK;
        consensus.nSigmaPaddingBlock = ZC_SIGMA_PADDING_BLOCK;
        consensus.nDisableUnpaddedSigmaBlock = ZC_SIGMA_DISABLE_UNPADDED_BLOCK;
        consensus.nOldSigmaBanBlock = ZC_OLD_SIGMA_BAN_BLOCK;
        consensus.nZerocoinV2MintMempoolGracefulPeriod = ZC_V2_MINT_GRACEFUL_MEMPOOL_PERIOD;
        consensus.nZerocoinV2MintGracefulPeriod = ZC_V2_MINT_GRACEFUL_PERIOD;
        consensus.nZerocoinV2SpendMempoolGracefulPeriod = ZC_V2_SPEND_GRACEFUL_MEMPOOL_PERIOD;
        consensus.nZerocoinV2SpendGracefulPeriod = ZC_V2_SPEND_GRACEFUL_PERIOD;
        consensus.nMaxSigmaInputPerBlock = ZC_SIGMA_INPUT_LIMIT_PER_BLOCK;
        consensus.nMaxValueSigmaSpendPerBlock = ZC_SIGMA_VALUE_SPEND_LIMIT_PER_BLOCK;
        consensus.nMaxSigmaInputPerTransaction = ZC_SIGMA_INPUT_LIMIT_PER_TRANSACTION;
        consensus.nMaxValueSigmaSpendPerTransaction = ZC_SIGMA_VALUE_SPEND_LIMIT_PER_TRANSACTION;
        consensus.nZerocoinToSigmaRemintWindowSize = 50000;

        // Dandelion related values.
        consensus.nDandelionEmbargoMinimum = DANDELION_EMBARGO_MINIMUM;
        consensus.nDandelionEmbargoAvgAdd = DANDELION_EMBARGO_AVG_ADD;
        consensus.nDandelionMaxDestinations = DANDELION_MAX_DESTINATIONS;
        consensus.nDandelionShuffleInterval = DANDELION_SHUFFLE_INTERVAL;
        consensus.nDandelionFluff = DANDELION_FLUFF;

        // Bip39: Mnemonic code for generating deterministic keys
        consensus.nMnemonicBlock = 222400;

        /**
         * Mainnet founders
         */
        foundersAddr[0] = "TR4GdMfDF2ZW74RRgxFxh4kwWgMaDw3BqH"; //premine
        foundersAddr[1] = "TC4frBMpSm2PF2FuUNqJ3qicn4EHL59ejL"; //dev team
        foundersAddr[2] = "TNTkzXXJf8Yw3W1i29iQQgcxVfc3JicS2s"; //science projects
        foundersAddr[3] = "TD6A1JC3jUT91riUxpQpMQZJVBa4xU2vQC"; //crypto-interest


        consensus.rewardsStage2Start = 71000;
        consensus.rewardsStage3Start = 840000;  //wp 1.3
        consensus.rewardsStage4Start = 1680000;
        consensus.rewardsStage5Start = 2520000;
        consensus.rewardsStage6Start = 3366000;
    }
};

static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";

        consensus.chainType = Consensus::chainTestnet;
        consensus.nSubsidyHalvingInterval = nDefaultSubsidyHalvingInterval;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 100;
        consensus.nMinNFactor = 10;
        consensus.nMaxNFactor = 30;
        consensus.nChainStartTime = 1539820800;
        consensus.BIP34Height = 2221;
        consensus.BIP34Hash = uint256S("0x000001fb456c55918c82e7956c07a9e6941385085093db3577ee0d795b444bcc");
        consensus.powLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 60 * 60; // 60 minutes between retargets
        consensus.nPowTargetSpacing = defaultPowTargetSpacing;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1586026090; // 04/04/2020
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1586476800; // 04/10/2020 @ 12:00am (UTC)

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1587859200; // 04/26/2020 @ 12:00am (UTC)
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1590796800; // 05/30/2020 @ 12:00am (UTC)

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = consensus.nChainStartTime; //we do not want segwit (for now?)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = consensus.nChainStartTime;

        // Deployment of MTP
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].bit = 12;
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].nStartTime = SWITCH_TO_MTP_BLOCK_HEADER - 2*60; //2 minutes
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].nTimeout = SWITCH_TO_MTP_BLOCK_HEADER + consensus.nMinerConfirmationWindow*2*5*60; //enforce

        consensus.nSpendV15StartBlock = 1;
        consensus.nCheckBugFixedAtBlock = 1;
        consensus.nTnodePaymentsBugFixedAtBlock = 100;

        consensus.nSpendV2ID_1 = ZC_V2_TESTNET_SWITCH_ID_1;
        consensus.nSpendV2ID_10 = ZC_V2_TESTNET_SWITCH_ID_10;
        consensus.nSpendV2ID_25 = ZC_V2_TESTNET_SWITCH_ID_25;
        consensus.nSpendV2ID_50 = ZC_V2_TESTNET_SWITCH_ID_50;
        consensus.nSpendV2ID_100 = ZC_V2_TESTNET_SWITCH_ID_100;
        consensus.nModulusV2StartBlock = ZC_MODULUS_V2_TESTNET_START_BLOCK;
        consensus.nModulusV1MempoolStopBlock = ZC_MODULUS_V1_TESTNET_MEMPOOL_STOP_BLOCK;
        consensus.nModulusV1StopBlock = ZC_MODULUS_V1_TESTNET_STOP_BLOCK;
        consensus.nMultipleSpendInputsInOneTxStartBlock = INT_MAX;
        consensus.nDontAllowDupTxsStartBlock = 1;

        // Tnode params testnet
        consensus.nTnodeMinimumConfirmations = 1;
        consensus.nTnodePaymentsStartBlock = HF_TNODE_HEIGHT + 25 * 60 / 2.5;// 25h after tnode start; must be less than rewardsStageStage2Start
        consensus.nPremineSubsidy = 21000000; // 21mln TCR
        //consensus.nTnodePaymentsIncreaseBlock = 360; // not used for now, probably later
        //consensus.nTnodePaymentsIncreasePeriod = 650; // not used for now, probably later
        //consensus.nSuperblockStartBlock = 61000;
        //consensus.nBudgetPaymentsStartBlock = 60000;
        //consensus.nBudgetPaymentsCycleBlocks = 50;
        //consensus.nBudgetPaymentsWindowBlocks = 10;
        nMaxTipAge = 0x7fffffff; // allow mining on top of old blocks for testnet

        // evo tnodes
        consensus.DIP0003Height = 60000; // TecraCoin: around 10/09/2020 @ 0:0am (UTC)
        consensus.DIP0003EnforcementHeight = 63000; //TCR: around 15/09/2020 @ 5:00am (UTC)
        consensus.DIP0008Height = INT_MAX;
        consensus.nEvoTnodeMinimumConfirmations = 1;

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_10_70] = llmq10_70;
        consensus.llmqs[Consensus::LLMQ_50_60] = llmq50_60;
        consensus.llmqs[Consensus::LLMQ_400_60] = llmq400_60;
        consensus.llmqs[Consensus::LLMQ_400_85] = llmq400_85;
        consensus.nLLMQPowTargetSpacing = 20;

        consensus.nMTPSwitchTime = SWITCH_TO_MTP_BLOCK_HEADER_TESTNET;
        consensus.nMTPStartBlock = 15593; // TecraCoin

        consensus.nDisableZerocoinStartBlock = 1;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        strSporkPubKey = "048779365ea4301c3da88204a79f202ad51fc5497727ae11a804b95091dfbd0ad3ef88456e0d09428ae97b70be75f8f49b0b52dad6900c6933717dcfe4ba9302d2";

        pchMessageStart[0] = 0x2c;
        pchMessageStart[1] = 0xc2;
        pchMessageStart[2] = 0x18;
        pchMessageStart[3] = 0xef;

        consensus.nDisableZerocoinStartBlock = 20;

        nDefaultPort = 2818;
        nPruneAfterHeight = 1000;

        std::vector<unsigned char> extraNonce(4);
        extraNonce[0] = 0x5d;
        extraNonce[1] = 0x9a;
        extraNonce[2] = 0x00;
        extraNonce[3] = 0x00;

        genesis = CreateGenesisBlock(1586024828, 73343, 0x1e0ffff0, 2, 0 * COIN, extraNonce, true);

        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x00000507375707d9ddd815d2c54aa54e9e29ad0992d51b44155044003e224b78"));
        assert(genesis.hashMerkleRoot == uint256S("0x2645ab62325df30e5d575394159d646cbcf705a4c737b9a8001a7e0c4e99e8ce"));

        vFixedSeeds.clear();
        vSeeds.clear();

        vSeeds.push_back(CDNSSeedData("testnet-seed.tecracoin.io", "testnet-seed.tecracoin.io", false));

        // Testnet TecraCoin addresses start with 'G'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector < unsigned char > (1, 38);
        // Testnet TecraCoin script addresses start with '2'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector < unsigned char > (1, 216);
        // Testnet TecraCoin private keys start with '2'
        base58Prefixes[SECRET_KEY] = std::vector < unsigned char > (1, 218);
        // Testnet TecraCoin BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container < std::vector < unsigned char > > ();
        // Testnet TecraCoin BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container < std::vector < unsigned char > > ();
        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fAllowMultiplePorts = true;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
                        (1, uint256S("0x00b360392b7230eef10baec8fe24a0a5590164681028b868a57ae0c1754365ec"))
                        (1097, uint256S("0x0000083dcce10b707687d6f15074976b541fc7d72d760931292227d0f0ea9122"))
                        (10572, uint256S("0x00065ba00de7f8b4f3dbf085b8e3ed25fa396692e816f7f2953a851c904626f3"))
                        (58000, uint256S("0x5d4051eb81d2eb32faa536ccc2e576141b062993caa0ed1814b51dda26d70b19"))
                        (77910, uint256S("0x8fafbb35ee9b6383d24cefd3e96cf4504a751c44d0757c4123ca27174242c9d9"))
        };

        chainTxData = ChainTxData{
            1603196161, //timestamp
            94569,      //transactions
            0.001       //tx/s
        };

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000000009c4bc7c84"); //77910

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x8fafbb35ee9b6383d24cefd3e96cf4504a751c44d0757c4123ca27174242c9d9"); //77910

        consensus.nSpendV15StartBlock = ZC_V1_5_TESTNET_STARTING_BLOCK;
        consensus.nSpendV2ID_1 = ZC_V2_TESTNET_SWITCH_ID_1;
        consensus.nSpendV2ID_10 = ZC_V2_TESTNET_SWITCH_ID_10;
        consensus.nSpendV2ID_25 = ZC_V2_TESTNET_SWITCH_ID_25;
        consensus.nSpendV2ID_50 = ZC_V2_TESTNET_SWITCH_ID_50;
        consensus.nSpendV2ID_100 = ZC_V2_TESTNET_SWITCH_ID_100;
        consensus.nModulusV2StartBlock = ZC_MODULUS_V2_TESTNET_START_BLOCK;
        consensus.nModulusV1MempoolStopBlock = ZC_MODULUS_V1_TESTNET_MEMPOOL_STOP_BLOCK;
        consensus.nModulusV1StopBlock = ZC_MODULUS_V1_TESTNET_STOP_BLOCK;

        // Sigma related values.
        consensus.nSigmaStartBlock = 1;
        consensus.nSigmaPaddingBlock = 1;
        consensus.nDisableUnpaddedSigmaBlock = 1;
        consensus.nOldSigmaBanBlock = 1;

        consensus.nZerocoinV2MintMempoolGracefulPeriod = ZC_V2_MINT_TESTNET_GRACEFUL_MEMPOOL_PERIOD;
        consensus.nZerocoinV2MintGracefulPeriod = ZC_V2_MINT_TESTNET_GRACEFUL_PERIOD;
        consensus.nZerocoinV2SpendMempoolGracefulPeriod = ZC_V2_SPEND_TESTNET_GRACEFUL_MEMPOOL_PERIOD;
        consensus.nZerocoinV2SpendGracefulPeriod = ZC_V2_SPEND_TESTNET_GRACEFUL_PERIOD;
        consensus.nMaxSigmaInputPerBlock = ZC_SIGMA_INPUT_LIMIT_PER_BLOCK;
        consensus.nMaxValueSigmaSpendPerBlock = ZC_SIGMA_VALUE_SPEND_LIMIT_PER_BLOCK;
        consensus.nMaxSigmaInputPerTransaction = ZC_SIGMA_INPUT_LIMIT_PER_TRANSACTION;
        consensus.nMaxValueSigmaSpendPerTransaction = ZC_SIGMA_VALUE_SPEND_LIMIT_PER_TRANSACTION;
        consensus.nZerocoinToSigmaRemintWindowSize = 0;

        // Dandelion related values.
        consensus.nDandelionEmbargoMinimum = DANDELION_TESTNET_EMBARGO_MINIMUM;
        consensus.nDandelionEmbargoAvgAdd = DANDELION_TESTNET_EMBARGO_AVG_ADD;
        consensus.nDandelionMaxDestinations = DANDELION_MAX_DESTINATIONS;
        consensus.nDandelionShuffleInterval = DANDELION_SHUFFLE_INTERVAL;
        consensus.nDandelionFluff = DANDELION_FLUFF;

        // Bip39
        consensus.nMnemonicBlock = 1;
        
        /**
         * Testnet founders
         */
        foundersAddr[0] = "GKR6SjJxF9HbvMVeZMBstuW9mRFBXCdkH6";// premine
        foundersAddr[1] = "Gf8XeYLLucQjMS8apuwBTPfbPN7eGd7r5h";// dev team
        foundersAddr[2] = "Gf3ZcqRci9yqu9ABsEp2SsvEmtvGjp6AoG";// science projects
        foundersAddr[3] = "GWrM3WGoKUegYJ6yTGHtH4ozmwZx9F8MiK";// crypto-interest


        consensus.rewardsStage2Start = 7100;
        consensus.rewardsStage3Start = 30000;
        consensus.rewardsStage4Start = 51000;
        consensus.rewardsStage5Start = 76000;
        consensus.rewardsStage6Start = 97000;

    }
};

static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";

        consensus.chainType = Consensus::chainRegtest;
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = -1; // BIP34 has not necessarily activated on regtest
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 60 * 60 * 1000; // 60*1000 minutes between retargets
        consensus.nPowTargetSpacing = 1;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = INT_MAX;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].bit = 12;
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].nStartTime = INT_MAX;
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].nTimeout = 999999999999ULL;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");
        // Tnode code
        consensus.nTnodePaymentsStartBlock = 400;//4000;// for testing purposes this needs to be low
        consensus.nTnodeMinimumConfirmations = 1;
        consensus.nPremineSubsidy = 21000000; // 21mln TCR
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        nMaxTipAge = 6 * 60 * 60; // ~8640 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin

        consensus.nDisableZerocoinStartBlock = INT_MAX;

        pchMessageStart[0] = 0x61;
        pchMessageStart[1] = 0xee;
        pchMessageStart[2] = 0x03;
        pchMessageStart[3] = 0x05;

        nDefaultPort = 2845;
        consensus.nCheckBugFixedAtBlock = 0;
        consensus.nTnodePaymentsBugFixedAtBlock = 1;
        consensus.nSpendV15StartBlock = 1;
        consensus.nSpendV2ID_1 = 2;
        consensus.nSpendV2ID_10 = 3;
        consensus.nSpendV2ID_25 = 3;
        consensus.nSpendV2ID_50 = 3;
        consensus.nSpendV2ID_100 = 3;
        consensus.nModulusV2StartBlock = 130;
        consensus.nModulusV1MempoolStopBlock = 135;
        consensus.nModulusV1StopBlock = 140;
        consensus.nMultipleSpendInputsInOneTxStartBlock = 1;
        consensus.nDontAllowDupTxsStartBlock = 1;

        consensus.nMTPSwitchTime = INT_MAX;

        consensus.nCheckBugFixedAtBlock = 120;
        consensus.nTnodePaymentsBugFixedAtBlock = 1;
        consensus.nSpendV15StartBlock = 1;
        consensus.nSpendV2ID_1 = 2;
        consensus.nSpendV2ID_10 = 3;
        consensus.nSpendV2ID_25 = 3;
        consensus.nSpendV2ID_50 = 3;
        consensus.nSpendV2ID_100 = 3;
        consensus.nModulusV2StartBlock = 130;
        consensus.nModulusV1MempoolStopBlock = 135;
        consensus.nModulusV1StopBlock = 140;
        consensus.nMultipleSpendInputsInOneTxStartBlock = 1;
        consensus.nDontAllowDupTxsStartBlock = 1;

        // evo tnodes
        consensus.DIP0003Height = 10500;//500;//10500;
        consensus.DIP0003EnforcementHeight = 11500;//600;//11500;
        consensus.DIP0008Height = INT_MAX;
        consensus.nEvoTnodeMinimumConfirmations = 1;

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_5_60] = llmq5_60;
        consensus.llmqs[Consensus::LLMQ_50_60] = llmq50_60;
        consensus.llmqs[Consensus::LLMQ_400_60] = llmq400_60;
        consensus.llmqs[Consensus::LLMQ_400_85] = llmq400_85;
        consensus.nLLMQPowTargetSpacing = 1;

        consensus.nMTPSwitchTime = INT_MAX;
        consensus.nMTPStartBlock = 0;

        consensus.nDisableZerocoinStartBlock = INT_MAX;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 18444;
        nPruneAfterHeight = 1000;

        /**
          * btzc: regtest params
          * nTime: 1539907200
          * nNonce: 433906595
          */
        std::vector<unsigned char> extraNonce(4);
        extraNonce[0] = 0x09;
        extraNonce[1] = 0x00;
        extraNonce[2] = 0x00;
        extraNonce[3] = 0x00;
        genesis = CreateGenesisBlock(ZC_GENESIS_BLOCK_TIME, 433906595, 0x1d00ffff, 1, 0 * COIN, extraNonce);
        consensus.hashGenesisBlock = genesis.GetHash();

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fAllowMultiplePorts = true;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (0, uint256S("0x000000004a61fcae2d1a068cdede78989a38eb1f85cf03804dd2817c7e028391"))
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };
        // Regtest TecraCoin addresses start with 'f'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector < unsigned char > (1, 95);
        // Regtest TecraCoin script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector < unsigned char > (1, 127);
        // Regtest private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] = std::vector < unsigned char > (1, 245);
        // Regtest TecraCoin BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container < std::vector < unsigned char > > ();
        // Regtest TecraCoin BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container < std::vector < unsigned char > > ();

        nSpendV15StartBlock = ZC_V1_5_TESTNET_STARTING_BLOCK;
        nSpendV2ID_1 = ZC_V2_TESTNET_SWITCH_ID_1;
        nSpendV2ID_10 = ZC_V2_TESTNET_SWITCH_ID_10;
        nSpendV2ID_25 = ZC_V2_TESTNET_SWITCH_ID_25;
        nSpendV2ID_50 = ZC_V2_TESTNET_SWITCH_ID_50;
        nSpendV2ID_100 = ZC_V2_TESTNET_SWITCH_ID_100;
        nModulusV2StartBlock = ZC_MODULUS_V2_TESTNET_START_BLOCK;
        nModulusV1MempoolStopBlock = ZC_MODULUS_V1_TESTNET_MEMPOOL_STOP_BLOCK;
        nModulusV1StopBlock = ZC_MODULUS_V1_TESTNET_STOP_BLOCK;

        // Sigma related values.
        consensus.nSigmaStartBlock = 400;
        consensus.nSigmaPaddingBlock = 550;
        consensus.nDisableUnpaddedSigmaBlock = 510;
        consensus.nOldSigmaBanBlock = 450;
        consensus.nZerocoinV2MintMempoolGracefulPeriod = 2;
        consensus.nZerocoinV2MintGracefulPeriod = 5;
        consensus.nZerocoinV2SpendMempoolGracefulPeriod = 10;
        consensus.nZerocoinV2SpendGracefulPeriod = 20;
        consensus.nMaxSigmaInputPerBlock = ZC_SIGMA_INPUT_LIMIT_PER_BLOCK;
        consensus.nMaxValueSigmaSpendPerBlock = ZC_SIGMA_VALUE_SPEND_LIMIT_PER_BLOCK;
        consensus.nMaxSigmaInputPerTransaction = ZC_SIGMA_INPUT_LIMIT_PER_TRANSACTION;
        consensus.nMaxValueSigmaSpendPerTransaction = ZC_SIGMA_VALUE_SPEND_LIMIT_PER_TRANSACTION;
        consensus.nZerocoinToSigmaRemintWindowSize = 1000;

        // Dandelion related values.
        consensus.nDandelionEmbargoMinimum = 0;
        consensus.nDandelionEmbargoAvgAdd = 1;
        consensus.nDandelionMaxDestinations = DANDELION_MAX_DESTINATIONS;
        consensus.nDandelionShuffleInterval = DANDELION_SHUFFLE_INTERVAL;
        consensus.nDandelionFluff = DANDELION_FLUFF;

        // Bip39
        consensus.nMnemonicBlock = 0;

        /**
         * Regtest founders
         */
        foundersAddr[0] = "fTo1upagmNaejBGkND8HfsjfVKgnBmuLLY";
        foundersAddr[1] = "fdAAqxKZrgvt6o4YKSer2rNWaw1y5C8vZU";
        foundersAddr[2] = "fc52erY95gPPodezPd3Rfj6h4DWKPN8pkM";
        foundersAddr[3] = "fT5R28XFJx7A618pNVjBoQmyYNQviqCzBs";

        consensus.rewardsStage2Start = 13000;//3;// for testing purposes this needs to be low
        // Some random thresholds
        consensus.rewardsStage3Start = 18000;//10;
        consensus.rewardsStage4Start = 200000;
        consensus.rewardsStage5Start = 300000;
        consensus.rewardsStage6Start = 400000;
    }

    void UpdateBIP9Parameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
    {
        consensus.vDeployments[d].nStartTime = nStartTime;
        consensus.vDeployments[d].nTimeout = nTimeout;
    }
};
static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams& Params(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
            return mainParams;
    else if (chain == CBaseChainParams::TESTNET)
            return testNetParams;
    else if (chain == CBaseChainParams::REGTEST)
            return regTestParams;
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}

void UpdateRegtestBIP9Parameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    regTestParams.UpdateBIP9Parameters(d, nStartTime, nTimeout);
}


CScript CChainParams::GetFounderScript(unsigned int founderIndex) const{
    return GetScriptForDestination(CBitcoinAddress(this->foundersAddr[founderIndex]).Get());
}

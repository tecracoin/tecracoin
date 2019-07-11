#include "exodus/test/utils_tx.h"

#include "exodus/exodus.h"
#include "exodus/script.h"
#include "exodus/tx.h"

#include "base58.h"
#include "coins.h"
#include "primitives/transaction.h"
#include "random.h"
#include "script/script.h"
#include "script/standard.h"
#include "test/test_bitcoin.h"

#include <stdint.h>
#include <algorithm>
#include <limits>
#include <vector>

#include <boost/test/unit_test.hpp>

using namespace exodus;

BOOST_FIXTURE_TEST_SUITE(exodus_sender_bycontribution_tests, BasicTestingSetup)

// Forward declarations
static CTransaction TxClassB(const std::vector<CTxOut>& txInputs);
static bool GetSenderByContribution(const std::vector<CTxOut>& vouts, std::string& strSender);
static CTxOut createTxOut(int64_t amount, const std::string& dest);
static CKeyID createRandomKeyId();
static CScriptID createRandomScriptId();
void shuffleAndCheck(std::vector<CTxOut>& vouts, unsigned nRounds);

// Test settings
static const unsigned nOutputs = 256;
static const unsigned nAllRounds = 2;
static const unsigned nShuffleRounds = 16;

/**
 * Tests the invalidation of the transaction, when there are not allowed inputs.
 */
BOOST_AUTO_TEST_CASE(invalid_inputs)
{
    {
        std::vector<CTxOut> vouts;
        vouts.push_back(PayToPubKey_Unrelated());
        vouts.push_back(PayToPubKeyHash_Unrelated());
        std::string strSender;
        BOOST_CHECK(!GetSenderByContribution(vouts, strSender));
    }
    {
        std::vector<CTxOut> vouts;
        vouts.push_back(PayToPubKeyHash_Unrelated());
        vouts.push_back(PayToBareMultisig_1of3());
        std::string strSender;
        BOOST_CHECK(!GetSenderByContribution(vouts, strSender));
    }
    {
        std::vector<CTxOut> vouts;
        vouts.push_back(PayToScriptHash_Unrelated());
        vouts.push_back(PayToPubKeyHash_Exodus());
        vouts.push_back(NonStandardOutput());
        std::string strSender;
        BOOST_CHECK(!GetSenderByContribution(vouts, strSender));
    }
}

/**
 * Tests sender selection "by sum" with pay-to-pubkey-hash outputs, where a single
 * candidate has the highest output value.
 */
BOOST_AUTO_TEST_CASE(p2pkh_contribution_by_sum_test)
{
    std::vector<CTxOut> vouts;
    vouts.push_back(createTxOut(100, "TKDFdsoucnFUTRRxxLz5hc8fBvdoQDbNpQ"));
    vouts.push_back(createTxOut(100, "TKDFdsoucnFUTRRxxLz5hc8fBvdoQDbNpQ"));
    vouts.push_back(createTxOut(100, "TKDFdsoucnFUTRRxxLz5hc8fBvdoQDbNpQ"));
    vouts.push_back(createTxOut(100, "TWJ17P7YCPZXTYfCyD6p9buHZkkTT25Jux"));
    vouts.push_back(createTxOut(100, "TWJ17P7YCPZXTYfCyD6p9buHZkkTT25Jux"));
    vouts.push_back(createTxOut(999, "TFksdUj8dFsJ89wxS6SVDguehoggzoBGgg")); // Winner
    vouts.push_back(createTxOut(100, "TAbNdXMgyKzy2T31xTFPAnjvkkFJzJXgbv"));
    vouts.push_back(createTxOut(100, "TAbNdXMgyKzy2T31xTFPAnjvkkFJzJXgbv"));
    vouts.push_back(createTxOut(100, "TDbDuQR2LpmwwEVuKcrmLhGfHJBhRqfRDB"));

    std::string strExpected("TFksdUj8dFsJ89wxS6SVDguehoggzoBGgg");

    for (int i = 0; i < 10; ++i) {
        std::random_shuffle(vouts.begin(), vouts.end(), GetRandInt);

        std::string strSender;
        BOOST_CHECK(GetSenderByContribution(vouts, strSender));
        BOOST_CHECK_EQUAL(strExpected, strSender);
    }
}

/**
 * Tests sender selection "by sum" with pay-to-pubkey-hash outputs, where a candidate
 * with the highest output value by sum, with more than one output, is chosen.
 */
BOOST_AUTO_TEST_CASE(p2pkh_contribution_by_total_sum_test)
{
    std::vector<CTxOut> vouts;
    vouts.push_back(createTxOut(499, "TKDFdsoucnFUTRRxxLz5hc8fBvdoQDbNpQ"));
    vouts.push_back(createTxOut(501, "TKDFdsoucnFUTRRxxLz5hc8fBvdoQDbNpQ"));
    vouts.push_back(createTxOut(295, "TWJ17P7YCPZXTYfCyD6p9buHZkkTT25Jux")); // Winner
    vouts.push_back(createTxOut(310, "TWJ17P7YCPZXTYfCyD6p9buHZkkTT25Jux")); // Winner
    vouts.push_back(createTxOut(400, "TWJ17P7YCPZXTYfCyD6p9buHZkkTT25Jux")); // Winner
    vouts.push_back(createTxOut(500, "TFksdUj8dFsJ89wxS6SVDguehoggzoBGgg"));
    vouts.push_back(createTxOut(500, "TFksdUj8dFsJ89wxS6SVDguehoggzoBGgg"));

    std::string strExpected("TWJ17P7YCPZXTYfCyD6p9buHZkkTT25Jux");

    for (int i = 0; i < 10; ++i) {
        std::random_shuffle(vouts.begin(), vouts.end(), GetRandInt);

        std::string strSender;
        BOOST_CHECK(GetSenderByContribution(vouts, strSender));
        BOOST_CHECK_EQUAL(strExpected, strSender);
    }
}

/**
 * Tests sender selection "by sum" with pay-to-pubkey-hash outputs, where all outputs
 * have equal values, and a candidate is chosen based on the lexicographical order of
 * the base58 string representation (!) of the candidate.
 *
 * Note: it reflects the behavior of Omni Core, but this edge case is not specified.
 */
BOOST_AUTO_TEST_CASE(p2pkh_contribution_by_sum_order_test)
{
    std::vector<CTxOut> vouts;
    vouts.push_back(createTxOut(1000, "TAbNdXMgyKzy2T31xTFPAnjvkkFJzJXgbv")); // Winner
    vouts.push_back(createTxOut(1000, "TEDZNzytEvYJZtduKeRL4u3ED1LgR2eYqL"));
    vouts.push_back(createTxOut(1000, "TKDFdsoucnFUTRRxxLz5hc8fBvdoQDbNpQ"));
    vouts.push_back(createTxOut(1000, "TDbDuQR2LpmwwEVuKcrmLhGfHJBhRqfRDB"));
    vouts.push_back(createTxOut(1000, "TYQJDiKxD3kvrBXesC3Rpi6Hmw4cFnLzAm"));
    vouts.push_back(createTxOut(1000, "TFksdUj8dFsJ89wxS6SVDguehoggzoBGgg"));
    vouts.push_back(createTxOut(1000, "TCiRBcHbpxpTTGK9VdYp7aEcbgKjjhsJUL"));
    vouts.push_back(createTxOut(1000, "TWJ17P7YCPZXTYfCyD6p9buHZkkTT25Jux"));
    vouts.push_back(createTxOut(1000, "TEsMwaVrfMhAEGSPn9ooVYifFf7rAD4jdP"));

    std::string strExpected("TAbNdXMgyKzy2T31xTFPAnjvkkFJzJXgbv");

    for (int i = 0; i < 10; ++i) {
        std::random_shuffle(vouts.begin(), vouts.end(), GetRandInt);

        std::string strSender;
        BOOST_CHECK(GetSenderByContribution(vouts, strSender));
        BOOST_CHECK_EQUAL(strExpected, strSender);
    }
}

/**
 * Tests sender selection "by sum" with pay-to-script-hash outputs, where a single
 * candidate has the highest output value.
 */
BOOST_AUTO_TEST_CASE(p2sh_contribution_by_sum_test)
{
    std::vector<CTxOut> vouts;
    vouts.push_back(createTxOut(100, "TDbDuQR2LpmwwEVuKcrmLhGfHJBhRqfRDB"));
    vouts.push_back(createTxOut(150, "TEDZNzytEvYJZtduKeRL4u3ED1LgR2eYqL"));
    vouts.push_back(createTxOut(400, "TKDFdsoucnFUTRRxxLz5hc8fBvdoQDbNpQ"));
    vouts.push_back(createTxOut(100, "TDbDuQR2LpmwwEVuKcrmLhGfHJBhRqfRDB"));
    vouts.push_back(createTxOut(400, "TAbNdXMgyKzy2T31xTFPAnjvkkFJzJXgbv"));
    vouts.push_back(createTxOut(100, "TDbDuQR2LpmwwEVuKcrmLhGfHJBhRqfRDB"));
    vouts.push_back(createTxOut(777, "TYQJDiKxD3kvrBXesC3Rpi6Hmw4cFnLzAm")); // Winner
    vouts.push_back(createTxOut(100, "TFksdUj8dFsJ89wxS6SVDguehoggzoBGgg"));

    std::string strExpected("TYQJDiKxD3kvrBXesC3Rpi6Hmw4cFnLzAm");

    for (int i = 0; i < 10; ++i) {
        std::random_shuffle(vouts.begin(), vouts.end(), GetRandInt);

        std::string strSender;
        BOOST_CHECK(GetSenderByContribution(vouts, strSender));
        BOOST_CHECK_EQUAL(strExpected, strSender);
    }
}

/**
 * Tests sender selection "by sum" with pay-to-pubkey-hash and pay-to-script-hash
 * outputs mixed, where a candidate with the highest output value by sum, with more
 * than one output, is chosen.
 */
BOOST_AUTO_TEST_CASE(p2sh_contribution_by_total_sum_test)
{
    std::vector<CTxOut> vouts;

    vouts.push_back(createTxOut(100, "TDbDuQR2LpmwwEVuKcrmLhGfHJBhRqfRDB"));
    vouts.push_back(createTxOut(500, "TDbDuQR2LpmwwEVuKcrmLhGfHJBhRqfRDB"));
    vouts.push_back(createTxOut(600, "TEDZNzytEvYJZtduKeRL4u3ED1LgR2eYqL")); // Winner
    vouts.push_back(createTxOut(500, "TKDFdsoucnFUTRRxxLz5hc8fBvdoQDbNpQ"));
    vouts.push_back(createTxOut(100, "TKDFdsoucnFUTRRxxLz5hc8fBvdoQDbNpQ"));
    vouts.push_back(createTxOut(350, "TEDZNzytEvYJZtduKeRL4u3ED1LgR2eYqL")); // Winner
    vouts.push_back(createTxOut(110, "TKDFdsoucnFUTRRxxLz5hc8fBvdoQDbNpQ"));

    std::string strExpected("TEDZNzytEvYJZtduKeRL4u3ED1LgR2eYqL");

    for (int i = 0; i < 10; ++i) {
        std::random_shuffle(vouts.begin(), vouts.end(), GetRandInt);

        std::string strSender;
        BOOST_CHECK(GetSenderByContribution(vouts, strSender));
        BOOST_CHECK_EQUAL(strExpected, strSender);
    }
}

/**
 * Tests sender selection "by sum" with pay-to-script-hash outputs, where all outputs
 * have equal values, and a candidate is chosen based on the lexicographical order of
 * the base58 string representation (!) of the candidate.
 *
 * Note: it reflects the behavior of Omni Core, but this edge case is not specified.
 */
BOOST_AUTO_TEST_CASE(p2sh_contribution_by_sum_order_test)
{
    std::vector<CTxOut> vouts;
    vouts.push_back(createTxOut(1000, "TAbNdXMgyKzy2T31xTFPAnjvkkFJzJXgbv")); // Winner
    vouts.push_back(createTxOut(1000, "TEDZNzytEvYJZtduKeRL4u3ED1LgR2eYqL"));
    vouts.push_back(createTxOut(1000, "TKDFdsoucnFUTRRxxLz5hc8fBvdoQDbNpQ"));
    vouts.push_back(createTxOut(1000, "TDbDuQR2LpmwwEVuKcrmLhGfHJBhRqfRDB"));
    vouts.push_back(createTxOut(1000, "TYQJDiKxD3kvrBXesC3Rpi6Hmw4cFnLzAm"));
    vouts.push_back(createTxOut(1000, "TFksdUj8dFsJ89wxS6SVDguehoggzoBGgg"));
    vouts.push_back(createTxOut(1000, "TCiRBcHbpxpTTGK9VdYp7aEcbgKjjhsJUL"));
    vouts.push_back(createTxOut(1000, "TWJ17P7YCPZXTYfCyD6p9buHZkkTT25Jux"));
    vouts.push_back(createTxOut(1000, "TEsMwaVrfMhAEGSPn9ooVYifFf7rAD4jdP"));

    std::string strExpected("TAbNdXMgyKzy2T31xTFPAnjvkkFJzJXgbv");

    for (int i = 0; i < 10; ++i) {
        std::random_shuffle(vouts.begin(), vouts.end(), GetRandInt);

        std::string strSender;
        BOOST_CHECK(GetSenderByContribution(vouts, strSender));
        BOOST_CHECK_EQUAL(strExpected, strSender);
    }
}

/**
 * Tests sender selection "by sum", where the lexicographical order of the base58
 * representation as string (instead of uint160) determines the chosen candidate.
 *
 * In practise this implies selecting the sender "by sum" via a comparison of
 * CBitcoinAddress objects would yield faulty results.
 *
 * Note: it reflects the behavior of Omni Core, but this edge case is not specified.
 */
BOOST_AUTO_TEST_CASE(sender_selection_string_based_test)
{
    std::vector<CTxOut> vouts;
    // Hash 160: 06798B72667BFB682B9DCE42EE7D300E8AE55655
    vouts.push_back(createTxOut(1000, "TAZSfHUHBA1jmXYfg2XrrjzfRc3SM2QZnF"));
    // Hash 160: 06944DC93BAC707C96D01636C758678F0F68C65D
    vouts.push_back(createTxOut(1000, "TAZziYQPrxGoP4DMJ9gvs81b8A5Ef6sYyy"));
    // Hash 160: 06666E57F5677D792B301BFC9E583141118CD679
    vouts.push_back(createTxOut(1000, "TAZ3mF2ffWr8vdL7Kk7CsHEMyqZnqMRuNT"));// Winner
    // Hash 160: 066A4D21D14CC91E424515F7A8D9B73CE59F7406
    vouts.push_back(createTxOut(1000, "TAZ8Q9rxuJCPji8spNhCMQk2vpnoCuorEw"));  // Not!

    std::string strExpected("TAZ3mF2ffWr8vdL7Kk7CsHEMyqZnqMRuNT");

    for (int i = 0; i < 24; ++i) {
        std::random_shuffle(vouts.begin(), vouts.end(), GetRandInt);

        std::string strSender;
        BOOST_CHECK(GetSenderByContribution(vouts, strSender));
        BOOST_CHECK_EQUAL(strExpected, strSender);
    }
}

/**
 * Tests order independence of the sender selection "by sum" for pay-to-pubkey-hash
 * outputs, where all output values are equal.
 */
BOOST_AUTO_TEST_CASE(sender_selection_same_amount_test)
{
    for (unsigned i = 0; i < nAllRounds; ++i) {
        std::vector<CTxOut> vouts;
        for (unsigned n = 0; n < nOutputs; ++n) {
            CTxOut output(static_cast<int64_t>(1000),
                    GetScriptForDestination(createRandomKeyId()));
            vouts.push_back(output);
        }
        shuffleAndCheck(vouts, nShuffleRounds);
    }
}

/**
 * Tests order independence of the sender selection "by sum" for pay-to-pubkey-hash
 * outputs, where output values are different for each output.
 */
BOOST_AUTO_TEST_CASE(sender_selection_increasing_amount_test)
{
    for (unsigned i = 0; i < nAllRounds; ++i) {
        std::vector<CTxOut> vouts;
        for (unsigned n = 0; n < nOutputs; ++n) {
            CTxOut output(static_cast<int64_t>(1000 + n),
                    GetScriptForDestination(createRandomKeyId()));
            vouts.push_back(output);
        }
        shuffleAndCheck(vouts, nShuffleRounds);
    }
}

/**
 * Tests order independence of the sender selection "by sum" for pay-to-pubkey-hash
 * and pay-to-script-hash outputs mixed together, where output values are equal for
 * every second output.
 */
BOOST_AUTO_TEST_CASE(sender_selection_mixed_test)
{
    for (unsigned i = 0; i < nAllRounds; ++i) {
        std::vector<CTxOut> vouts;
        for (unsigned n = 0; n < nOutputs; ++n) {
            CScript scriptPubKey;
            if (GetRandInt(2) == 0) {
                scriptPubKey = GetScriptForDestination(createRandomKeyId());
            } else {
                scriptPubKey = GetScriptForDestination(createRandomScriptId());
            };
            int64_t nAmount = static_cast<int64_t>(1000 - n * (n % 2 == 0));
            vouts.push_back(CTxOut(nAmount, scriptPubKey));
        }
        shuffleAndCheck(vouts, nShuffleRounds);
    }
}

/** Creates a dummy class B transaction with the given inputs. */
static CTransaction TxClassB(const std::vector<CTxOut>& txInputs)
{
    CMutableTransaction mutableTx;

    // Inputs:
    for (std::vector<CTxOut>::const_iterator it = txInputs.begin(); it != txInputs.end(); ++it)
    {
        const CTxOut& txOut = *it;

        // Create transaction for input:
        CMutableTransaction inputTx;
        unsigned int nOut = 0;
        inputTx.vout.push_back(txOut);
        CTransaction tx(inputTx);

        // Populate transaction cache:
        CCoinsModifier coins = view.ModifyCoins(tx.GetHash());

        if (nOut >= coins->vout.size()) {
            coins->vout.resize(nOut+1);
        }
        coins->vout[nOut].scriptPubKey = txOut.scriptPubKey;
        coins->vout[nOut].nValue = txOut.nValue;

        // Add input:
        CTxIn txIn(tx.GetHash(), nOut);
        mutableTx.vin.push_back(txIn);
    }

    // Outputs:
    mutableTx.vout.push_back(PayToPubKeyHash_Exodus());
    mutableTx.vout.push_back(PayToBareMultisig_1of3());
    mutableTx.vout.push_back(PayToPubKeyHash_Unrelated());

    return CTransaction(mutableTx);
}

/** Extracts the sender "by contribution". */
static bool GetSenderByContribution(const std::vector<CTxOut>& vouts, std::string& strSender)
{
    int nBlock = std::numeric_limits<int>::max();

    CMPTransaction metaTx;
    CTransaction dummyTx = TxClassB(vouts);

    if (ParseTransaction(dummyTx, nBlock, 1, metaTx) == 0) {
        strSender = metaTx.getSender();
        return true;
    }

    return false;
}

/** Helper to create a CTxOut object. */
static CTxOut createTxOut(int64_t amount, const std::string& dest)
{
    return CTxOut(amount, GetScriptForDestination(CBitcoinAddress(dest).Get()));
}

/** Helper to create a CKeyID object with random value.*/
static CKeyID createRandomKeyId()
{
    std::vector<unsigned char> vch;
    vch.reserve(20);
    for (int i = 0; i < 20; ++i) {
        vch.push_back(static_cast<unsigned char>(GetRandInt(256)));
    }
    return CKeyID(uint160(vch));
}

/** Helper to create a CScriptID object with random value.*/
static CScriptID createRandomScriptId()
{
    std::vector<unsigned char> vch;
    vch.reserve(20);
    for (int i = 0; i < 20; ++i) {
        vch.push_back(static_cast<unsigned char>(GetRandInt(256)));
    }
    return CScriptID(uint160(vch));
}

/**
 * Identifies the sender of a transaction, based on the list of provided transaction
 * outputs, and then shuffles the list n times, while checking, if this produces the
 * same result. The "contribution by sum" sender selection doesn't require specific
 * positions or order of outputs, and should work in all cases.
 */
void shuffleAndCheck(std::vector<CTxOut>& vouts, unsigned nRounds)
{
    std::string strSenderFirst;
    BOOST_CHECK(GetSenderByContribution(vouts, strSenderFirst));

    for (unsigned j = 0; j < nRounds; ++j) {
        std::random_shuffle(vouts.begin(), vouts.end(), GetRandInt);

        std::string strSender;
        BOOST_CHECK(GetSenderByContribution(vouts, strSender));
        BOOST_CHECK_EQUAL(strSenderFirst, strSender);
    }
}


BOOST_AUTO_TEST_SUITE_END()

#include "../packetencoder.h"

#include "../../utilstrencodings.h"

#include <boost/test/unit_test.hpp>

#include <ostream>

namespace std {

template<typename Char, typename Traits>
basic_ostream<Char, Traits>& operator<<(basic_ostream<Char, Traits>& os, const CBase58Data& v)
{
    return os << v.ToString();
}

} // namespace std

namespace elysium {

BOOST_AUTO_TEST_SUITE(elysium_packetencoder_tests)

BOOST_AUTO_TEST_CASE(key_generator)
{
    PacketKeyGenerator g("1CdighsfdfRcj4ytQSskZgQXbUEamuMUNF");

    BOOST_CHECK_EQUAL(HexStr(g.Next()), "1d9a3de5c2e22bf89a1e41e6fedab54582f8a0c3ae14394a59366293dd130c59");
    BOOST_CHECK_EQUAL(HexStr(g.Next()), "0800ed44f1300fb3a5980ecfa8924fedb2d5fdbef8b21bba6526b4fd5f9c167c");
    BOOST_CHECK_EQUAL(HexStr(g.Next()), "7110a59d22d5af6a34b7a196dae7ccc0f27354b34e257832b9955611a9d79b06");
    BOOST_CHECK_EQUAL(HexStr(g.Next()), "aa3f890d32864bea31ee9bd57d2247d8f8ce07b5abaed9372f0b8999d28db963");
}

BOOST_AUTO_TEST_CASE(system_address_mainnet)
{
    SelectParams(CBaseChainParams::MAIN);

    BOOST_CHECK_EQUAL(GetSystemAddress(), CBitcoinAddress("TC4frBMpSm2PF2FuUNqJ3qicn4EHL59ejL"));
}

BOOST_AUTO_TEST_CASE(system_address_testnet)
{
    SelectParams(CBaseChainParams::TESTNET);

    BOOST_CHECK_EQUAL(GetSystemAddress(), CBitcoinAddress("Gf8XeYLLucQjMS8apuwBTPfbPN7eGd7r5h"));

    SelectParams(CBaseChainParams::MAIN);
}

BOOST_AUTO_TEST_CASE(system_address_regtest)
{
    SelectParams(CBaseChainParams::REGTEST);

    BOOST_CHECK_EQUAL(GetSystemAddress(), CBitcoinAddress("fdAAqxKZrgvt6o4YKSer2rNWaw1y5C8vZU"));

    SelectParams(CBaseChainParams::MAIN);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace elysium

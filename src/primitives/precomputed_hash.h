// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2016-2017 The Zcoin Core developers
// Copyright (c) 2018 The TecraCoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//TecraCoin TODO: Fill with precomputed block hashes? Do we even need it?
static const char *precomputedHash[1] = {
        "",
        // hex tx hashes
        //        "0000000da1e36c4636dcc335a04f92eadd914b6b7ba7c491d57e885ee51910fe", "00000027ac4851b1295226716709fc7903f12c13499db94e39a8748f3a022308", "0000001f3ae8427fd5c6ae82f345b67a5bfa81812b3013d106dc1bab080a2074", "0000000000652474481a18f6affe29ed9a5b9f20045a331f0ce5c734b770d4d2"
};

// We rarely reference PoW hash of early mainnet blocks, no need to convert it to uint256 for easy access
uint256 GetPrecomputedBlockPoWHash(int nHeight) {
    if (nHeight > 0 && nHeight < 20500)
        return uint256S(precomputedHash[nHeight]);
    else
        return uint256();
}

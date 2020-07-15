// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2016-2017 The Zcoin Core developers
// Copyright (c) 2018 The TecraCoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//Tecracoin TODO: Fill with precomputed block hashes?
static const char *precomputedHash[1] = {
        "",
};

// We rarely reference PoW hash of early mainnet blocks, no need to convert it to uint256 for easy access
uint256 GetPrecomputedBlockPoWHash(int nHeight) {
    if (nHeight > 0 && nHeight < 20500)
        return uint256S(precomputedHash[nHeight]);
    else
        return uint256();
}

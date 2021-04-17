// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/client.h"
#include "rpc/protocol.h"
#include "util.h"

#include <set>
#include <stdint.h>

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <univalue.h>

using namespace std;

class CRPCConvertParam
{
public:
    std::string methodName; //!< method whose params want conversion
    int paramIdx;           //!< 0-based idx of param to convert
    std::string paramName;  //!< parameter name
};

/**
 * Specifiy a (method, idx, name) here if the argument is a non-string RPC
 * argument and needs to be converted from JSON.
 *
 * @note Parameter indexes start from 0.
 */
static const CRPCConvertParam vRPCConvertParams[] =
{
    { "stop", 0, "detach" },
    { "setmocktime", 0, "timestamp" },
    { "getaddednodeinfo", 0, "node" },
    { "generate", 0, "nblocks" },
    { "generate", 1, "maxtries" },
    { "generatetoaddress", 0, "nblocks" },
    { "generatetoaddress", 2, "maxtries" },
    { "getnetworkhashps", 0, "nblocks" },
    { "getnetworkhashps", 1, "height" },
    { "sendtoaddress", 1, "amount" },
    { "sendtoaddress", 4, "subtractfeefromamount" },
    { "settxfee", 0, "amount" },
    { "listaddressbalances", 0, "minamount" },
    { "getreceivedbyaddress", 1, "minconf" },
    { "getreceivedbyaccount", 1, "minconf" },
    { "listreceivedbyaddress", 0, "minconf" },
    { "listreceivedbyaddress", 1, "include_empty" },
    { "listreceivedbyaddress", 2, "include_watchonly" },
    { "listreceivedbyaccount", 0, "minconf" },
    { "listreceivedbyaccount", 1, "include_empty" },
    { "listreceivedbyaccount", 2, "include_watchonly" },
    { "getbalance", 1, "minconf" },
    { "getbalance", 2, "include_watchonly" },
    { "getblockhash", 0, "height" },
    { "waitforblockheight", 0, "height" },
    { "waitforblockheight", 1, "timeout" },
    { "waitforblock", 1, "timeout" },
    { "waitfornewblock", 0, "timeout" },
    { "move", 2, "amount" },
    { "move", 3, "minconf" },
    { "sendfrom", 2, "amount" },
    { "sendfrom", 3, "minconf" },
    { "listtransactions", 1, "count" },
    { "listtransactions", 2, "skip" },
    { "listtransactions", 3, "include_watchonly" },
    { "listaccounts", 0, "minconf" },
    { "listaccounts", 1, "include_watchonly" },
    { "walletpassphrase", 1, "timeout" },
    { "getblocktemplate", 0, "template_request" },
    { "listsinceblock", 1, "target_confirmations" },
    { "listsinceblock", 2, "include_watchonly" },
    { "sendmany", 1, "amounts" },
    { "sendmany", 2, "minconf" },
    { "sendmany", 4, "subtractfeefrom" },
    { "addmultisigaddress", 0, "nrequired" },
    { "addmultisigaddress", 1, "keys" },
    { "createmultisig", 0, "nrequired" },
    { "createmultisig", 1, "keys" },
    { "listunspent", 0, "minconf" },
    { "listunspent", 1, "maxconf" },
    { "listunspent", 2, "addresses" },
    { "getblock", 1, "verbose" },
    { "getblockheader", 1, "verbose" },
    { "gettransaction", 1, "include_watchonly" },
    { "getrawtransaction", 1, "verbose" },
    { "createrawtransaction", 0, "inputs" },
    { "createrawtransaction", 1, "outputs" },
    { "createrawtransaction", 2, "locktime" },
    { "signrawtransaction", 1, "prevtxs" },
    { "signrawtransaction", 2, "privkeys" },
    { "sendrawtransaction", 1, "allowhighfees" },
    { "fundrawtransaction", 1, "options" },
    { "gettxout", 1, "n" },
    { "gettxout", 2, "include_mempool" },
    { "gettxoutproof", 0, "txids" },
    { "lockunspent", 0, "unlock" },
    { "lockunspent", 1, "transactions" },
    { "importprivkey", 2, "rescan" },
    { "importaddress", 2, "rescan" },
    { "importaddress", 3, "p2sh" },
    { "importpubkey", 2, "rescan" },
    { "importmulti", 0, "requests" },
    { "importmulti", 1, "options" },
    { "verifychain", 0, "checklevel" },
    { "verifychain", 1, "nblocks" },
    { "pruneblockchain", 0, "height" },
    { "keypoolrefill", 0, "newsize" },
    { "getrawmempool", 0, "verbose" },
    { "estimatefee", 0, "nblocks" },
    { "estimatepriority", 0, "nblocks" },
    { "estimatesmartfee", 0, "nblocks" },
    { "estimatesmartpriority", 0, "nblocks" },
    { "prioritisetransaction", 1, "priority_delta" },
    { "prioritisetransaction", 2, "fee_delta" },
    { "setban", 2, "bantime" },
    { "setban", 3, "absolute" },
    { "setnetworkactive", 0, "state" },
    { "getmempoolancestors", 1, "verbose" },
    { "getmempooldescendants", 1, "verbose" },
    { "bumpfee", 1, "options" },
    { "getblockhashes", 0, "min_timestamp" },
    { "getblockhashes", 1, "max_timestamp" },
    { "getspentinfo", 0, "txid"},
    { "getaddresstxids", 0, "address"},
    { "getaddressbalance", 0, "address"},
    { "getaddressdeltas", 0, "address"},
    { "getaddressutxos", 0, "address"},
    { "getaddressmempool", 0, "address"},
    { "getspecialtxes", 1, "type" },
    { "getspecialtxes", 2, "count" },
    { "getspecialtxes", 3, "skip" },
    { "getspecialtxes", 4, "verbosity" },
    // Echo with conversion (For testing only)
    { "echojson", 0, "arg0" },
    { "echojson", 1, "arg1" },
    { "echojson", 2, "arg2" },
    { "echojson", 3, "arg3" },
    { "echojson", 4, "arg4" },
    { "echojson", 5, "arg5" },
    { "echojson", 6, "arg6" },
    { "echojson", 7, "arg7" },
    { "echojson", 8, "arg8" },
    { "echojson", 9, "arg9" },
    //[zcoin]
    { "setmininput", 0, "amount" },
    { "mint", 0, "amount" },
    { "spendmany", 1, "accounts" },
    { "spendmany", 2, "minconf" },
    { "spendmany", 4, "subtractfeefromamount" },
    { "setgenerate", 0, "generate" },
    { "setgenerate", 1, "genproclimit" },
    /* Elysium - data retrieval calls */
	{ "elysium_gettradehistoryforaddress", 1, "count" },
	{ "elysium_gettradehistoryforaddress", 2, "propertyid" },
	{ "elysium_gettradehistoryforpair", 0, "propertyid" },
	{ "elysium_gettradehistoryforpair", 1, "propertyid" },
	{ "elysium_gettradehistoryforpair", 2, "count" },
	{ "elysium_setautocommit", 0, "flag" },
	{ "elysium_getcrowdsale", 0, "propertyid" },
	{ "elysium_getcrowdsale", 1, "verbose" },
	{ "elysium_getgrants", 0, "propertyid" },
	{ "elysium_getbalance", 1, "propertyid" },
	{ "elysium_getproperty", 0, "propertyid"},
	{ "elysium_listtransactions", 1, "count" },
	{ "elysium_listtransactions", 2, "skip" },
	{ "elysium_listtransactions", 3, "startblock" },
	{ "elysium_listtransactions", 4, "endblock" },
	{ "elysium_listmints", 0, "propertyid" },
	{ "elysium_listmints", 1, "denomination" },
	{ "elysium_listmints", 2, "verbose" },
	{ "elysium_getallbalancesforid", 0, "propertyid" },
	{ "elysium_listblocktransactions", 0, "index" },
	{ "elysium_getorderbook", 0, "propertyid" },
	{ "elysium_getorderbook", 1, "propertyid" },
	{ "elysium_getseedblocks", 0, "startblock" },
	{ "elysium_getseedblocks", 1, "endblock" },
	{ "elysium_getmetadexhash", 0, "propertyid" },
	{ "elysium_getfeecache", 0, "propertyid" },
	{ "elysium_getfeeshare", 1, "ecosystem" },
	{ "elysium_getfeetrigger", 0, "propertyid" },
	{ "elysium_getfeedistribution", 0, "distributionid" },
	{ "elysium_getfeedistributions", 0, "propertyid" },
	{ "elysium_getbalanceshash", 0, "propertyid" },

	/* Elysium - transaction calls */
	{ "elysium_send", 2, "propertyid" },
	{ "elysium_sendsto", 1, "propertyid" },
	{ "elysium_sendsto", 4, "distributionproperty" },
	{ "elysium_sendall", 2, "ecosystem" },
	{ "elysium_sendtrade", 1, "propertyidforsale" },
	{ "elysium_sendtrade", 3, "propertiddesired" },
	{ "elysium_sendcanceltradesbyprice", 1, "propertyidforsale" },
	{ "elysium_sendcanceltradesbyprice", 3, "propertiddesired" },
	{ "elysium_sendcanceltradesbypair", 1, "propertyidforsale" },
	{ "elysium_sendcanceltradesbypair", 2, "propertiddesired" },
	{ "elysium_sendcancelalltrades", 1, "ecosystem" },
	{ "elysium_sendissuancefixed", 1, "ecosystem" },
	{ "elysium_sendissuancefixed", 2, "type" },
	{ "elysium_sendissuancefixed", 3, "previousid" },
	{ "elysium_sendissuancefixed", 10, "sigma" },
	{ "elysium_sendissuancemanaged", 1, "ecosystem" },
	{ "elysium_sendissuancemanaged", 2, "type" },
	{ "elysium_sendissuancemanaged", 3, "previousid" },
	{ "elysium_sendissuancemanaged", 9, "sigma" },
	{ "elysium_sendissuancecrowdsale", 1, "ecosystem" },
	{ "elysium_sendissuancecrowdsale", 2, "type" },
	{ "elysium_sendissuancecrowdsale", 3, "previousid" },
	{ "elysium_sendissuancecrowdsale", 9, "propertyiddesired" },
	{ "elysium_sendissuancecrowdsale", 11, "deadline" },
	{ "elysium_sendissuancecrowdsale", 12, "earlybonus" },
	{ "elysium_sendissuancecrowdsale", 13, "issuerpercentage" },
	{ "elysium_senddexsell", 1, "propertyidforsale" },
	{ "elysium_senddexsell", 4, "paymentwindow" },
	{ "elysium_senddexsell", 6, "action" },
	{ "elysium_senddexaccept", 2, "propertyid" },
	{ "elysium_senddexaccept", 4, "override" },
	{ "elysium_sendclosecrowdsale", 1, "propertyid" },
	{ "elysium_sendgrant", 2, "propertyid" },
	{ "elysium_sendrevoke", 1, "propertyid" },
	{ "elysium_sendchangeissuer", 2, "propertyid" },
	{ "elysium_sendenablefreezing", 1, "propertyid" },
	{ "elysium_senddisablefreezing", 1, "propertyid" },
	{ "elysium_sendfreeze", 2, "propertyid" },
	{ "elysium_sendunfreeze", 2, "propertyid" },
	{ "elysium_senddeactivation", 1, "featureid" },
	{ "elysium_sendactivation", 1, "featureid" },
	{ "elysium_sendactivation", 2, "block" },
	{ "elysium_sendactivation", 3, "minclientversion" },
	{ "elysium_sendalert", 1, "alerttype" },
	{ "elysium_sendalert", 2, "expiryvalue" },
	{ "elysium_sendcreatedenomination", 1, "propertyid" },
	{ "elysium_sendmint", 1, "propertyid" },
	{ "elysium_sendmint", 2, "denominations" },
	{ "elysium_sendmint", 3, "denomminconf" },
	{ "elysium_sendspend", 1, "propertyid" },
	{ "elysium_sendspend", 2, "denomination" },

	/* Elysium - raw transaction calls */
	{ "elysium_decodetransaction", 1, "prevtxs" },
	{ "elysium_decodetransaction", 2, "height" },
	{ "elysium_createrawtx_reference", 2, "amount" },
	{ "elysium_createrawtx_input", 2, "n" },
	{ "elysium_createrawtx_change", 1, "prevtxs" },
	{ "elysium_createrawtx_change", 3, "fee" },
	{ "elysium_createrawtx_change", 4, "position" },

	/* Elysium - payload creation */
	{ "elysium_createpayload_simplesend", 0, "propertyid" },
	{ "elysium_createpayload_sendall", 0, "ecosystem" },
	{ "elysium_createpayload_dexsell", 0, "propertyidforsale" },
	{ "elysium_createpayload_dexsell", 3, "paymentwindow" },
	{ "elysium_createpayload_dexsell", 5, "action" },
	{ "elysium_createpayload_dexaccept", 0, "propertyid" },
	{ "elysium_createpayload_sto", 0, "propertyid" },
	{ "elysium_createpayload_sto", 2, "distributionproperty" },
	{ "elysium_createpayload_issuancefixed", 0, "ecosystem" },
	{ "elysium_createpayload_issuancefixed", 1, "type" },
	{ "elysium_createpayload_issuancefixed", 2, "previousid" },
	{ "elysium_createpayload_issuancemanaged", 0, "ecosystem" },
	{ "elysium_createpayload_issuancemanaged", 1, "type" },
	{ "elysium_createpayload_issuancemanaged", 2, "previousid" },
	{ "elysium_createpayload_issuancecrowdsale", 0, "ecosystem" },
	{ "elysium_createpayload_issuancecrowdsale", 1, "type" },
	{ "elysium_createpayload_issuancecrowdsale", 2, "previousid" },
	{ "elysium_createpayload_issuancecrowdsale", 8, "propertyiddesired" },
	{ "elysium_createpayload_issuancecrowdsale", 10, "deadline" },
	{ "elysium_createpayload_issuancecrowdsale", 11, "earlybonus" },
	{ "elysium_createpayload_issuancecrowdsale", 12, "issuerpercentage" },
	{ "elysium_createpayload_closecrowdsale", 0, "propertyid" },
	{ "elysium_createpayload_grant", 0, "propertyid" },
	{ "elysium_createpayload_revoke", 0, "propertyid" },
	{ "elysium_createpayload_changeissuer", 0, "propertyid" },
	{ "elysium_createpayload_trade", 0, "propertyidforsale" },
	{ "elysium_createpayload_trade", 2, "propertiddesired" },
	{ "elysium_createpayload_canceltradesbyprice", 0, "propertyidforsale" },
	{ "elysium_createpayload_canceltradesbyprice", 2, "propertiddesired" },
	{ "elysium_createpayload_canceltradesbypair", 0, "propertyidforsale" },
	{ "elysium_createpayload_canceltradesbypair", 1, "propertiddesired" },
	{ "elysium_createpayload_cancelalltrades", 0, "ecosystem" },

	/* Elysium - backwards compatibility */
	{ "getcrowdsale_MP", 0, "propertyid" },
	{ "getcrowdsale_MP", 1, "verbose" },
	{ "getgrants_MP", 0, "propertyid" },
	{ "send_MP", 2, "propertyid" },
	{ "getbalance_MP", 1, "propertyid" },
	{ "sendtoowners_MP", 1, "propertyid" },
	{ "getproperty_MP", 0, "propertyid" },
	{ "listtransactions_MP", 1, "count" },
	{ "listtransactions_MP", 2, "skip" },
	{ "listtransactions_MP", 3, "startblock" },
	{ "listtransactions_MP", 4, "endblock" },
	{ "getallbalancesforid_MP", 0, "propertyid" },
	{ "listblocktransactions_MP", 0, "propertyid" },
	{ "getorderbook_MP", 0, "propertyid" },
	{ "getorderbook_MP", 1, "propertyid" },
	{ "trade_MP", 1, "propertyidforsale" }, // depreciated
	{ "trade_MP", 3, "propertiddesired" }, // depreciated
	{ "trade_MP", 5, "action" }, // depreciated

    /* Evo spork */
    { "spork", 2, "features"},
};

class CRPCConvertTable
{
private:
    std::set<std::pair<std::string, int>> members;
    std::set<std::pair<std::string, std::string>> membersByName;

public:
    CRPCConvertTable();

    bool convert(const std::string& method, int idx) {
        return (members.count(std::make_pair(method, idx)) > 0);
    }
    bool convert(const std::string& method, const std::string& name) {
        return (membersByName.count(std::make_pair(method, name)) > 0);
    }
};

CRPCConvertTable::CRPCConvertTable()
{
    const unsigned int n_elem =
        (sizeof(vRPCConvertParams) / sizeof(vRPCConvertParams[0]));

    for (unsigned int i = 0; i < n_elem; i++) {
        members.insert(std::make_pair(vRPCConvertParams[i].methodName,
                                      vRPCConvertParams[i].paramIdx));
        membersByName.insert(std::make_pair(vRPCConvertParams[i].methodName,
                                            vRPCConvertParams[i].paramName));
    }
}

static CRPCConvertTable rpcCvtTable;

/** Non-RFC4627 JSON parser, accepts internal values (such as numbers, true, false, null)
 * as well as objects and arrays.
 */
UniValue ParseNonRFCJSONValue(const std::string& strVal)
{
    UniValue jVal;
    if (!jVal.read(std::string("[")+strVal+std::string("]")) ||
        !jVal.isArray() || jVal.size()!=1)
        throw runtime_error(string("Error parsing JSON:")+strVal);
    return jVal[0];
}

UniValue RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    UniValue params(UniValue::VARR);

    for (unsigned int idx = 0; idx < strParams.size(); idx++) {
        const std::string& strVal = strParams[idx];

        if (!rpcCvtTable.convert(strMethod, idx)) {
            // insert string value directly
            params.push_back(strVal);
        } else {
            // parse string as JSON, insert bool/number/object/etc. value
            params.push_back(ParseNonRFCJSONValue(strVal));
        }
    }

    return params;
}

UniValue RPCConvertNamedValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    UniValue params(UniValue::VOBJ);

    for (const std::string &s: strParams) {
        size_t pos = s.find("=");
        if (pos == std::string::npos) {
            throw(std::runtime_error("No '=' in named argument '"+s+"', this needs to be present for every argument (even if it is empty)"));
        }

        std::string name = s.substr(0, pos);
        std::string value = s.substr(pos+1);

        if (!rpcCvtTable.convert(strMethod, name)) {
            // insert string value directly
            params.pushKV(name, value);
        } else {
            // parse string as JSON, insert bool/number/object/etc. value
            params.pushKV(name, ParseNonRFCJSONValue(value));
        }
    }

    return params;
}

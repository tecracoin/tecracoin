// Copyright (c) 2014-2019 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVETNODE_H
#define ACTIVETNODE_H

#include "chainparams.h"
#include "key.h"
#include "net.h"
#include "primitives/transaction.h"
#include "validationinterface.h"

#include "evo/deterministicmns.h"
#include "evo/providertx.h"

struct CActiveTnodeInfo;
class CActiveTnodeManager;

static const int ACTIVE_TNODE_INITIAL          = 0; // initial state
static const int ACTIVE_TNODE_SYNC_IN_PROCESS  = 1;
static const int ACTIVE_TNODE_INPUT_TOO_NEW    = 2;
static const int ACTIVE_TNODE_NOT_CAPABLE      = 3;
static const int ACTIVE_TNODE_STARTED          = 4;

extern CActiveTnodeInfo activeTnodeInfo;
extern CActiveTnodeManager* activeTnodeManager;

struct CActiveTnodeInfo {
    // Keys for the active Tnode
    std::unique_ptr<CBLSPublicKey> blsPubKeyOperator;
    std::unique_ptr<CBLSSecretKey> blsKeyOperator;

    // Initialized while registering Tnode
    uint256 proTxHash;
    COutPoint outpoint;
    CService service;
};


class CActiveTnodeManager : public CValidationInterface
{
public:
    enum masternode_state_t {
        TNODE_WAITING_FOR_PROTX,
        TNODE_POSE_BANNED,
        TNODE_REMOVED,
        TNODE_OPERATOR_KEY_CHANGED,
        TNODE_PROTX_IP_CHANGED,
        TNODE_READY,
        TNODE_ERROR,
    };

private:
    masternode_state_t state{TNODE_WAITING_FOR_PROTX};
    std::string strError;

public:
    virtual void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload);

    void Init();

    std::string GetStateString() const;
    std::string GetStatus() const;

    static bool IsValidNetAddr(CService addrIn);

private:
    bool GetLocalAddress(CService& addrRet);
};

#endif

// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVETNODE_H
#define ACTIVETNODE_H

#include "net.h"
#include "key.h"
#include "wallet/wallet.h"

class CActiveTnode;

static const int ACTIVE_TNODE_INITIAL          = 0; // initial state
static const int ACTIVE_TNODE_SYNC_IN_PROCESS  = 1;
static const int ACTIVE_TNODE_INPUT_TOO_NEW    = 2;
static const int ACTIVE_TNODE_NOT_CAPABLE      = 3;
static const int ACTIVE_TNODE_STARTED          = 4;

extern CActiveTnode activeTnode;

// Responsible for activating the Tnode and pinging the network
class CActiveTnode
{
public:
    enum tnode_type_enum_t {
        TNODE_UNKNOWN = 0,
        TNODE_REMOTE  = 1,
        TNODE_LOCAL   = 2
    };

private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    tnode_type_enum_t eType;

    bool fPingerEnabled;

    /// Ping Tnode
    bool SendTnodePing();

public:
    // Keys for the active Tnode
    CPubKey pubKeyTnode;
    CKey keyTnode;

    // Initialized while registering Tnode
    CTxIn vin;
    CService service;

    int nState; // should be one of ACTIVE_TNODE_XXXX
    std::string strNotCapableReason;

    CActiveTnode()
        : eType(TNODE_UNKNOWN),
          fPingerEnabled(false),
          pubKeyTnode(),
          keyTnode(),
          vin(),
          service(),
          nState(ACTIVE_TNODE_INITIAL)
    {}

    /// Manage state of active Tnode
    void ManageState();

    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

private:
    void ManageStateInitial();
    void ManageStateRemote();
    void ManageStateLocal();
};

#endif

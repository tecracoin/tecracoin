#include "notifyznodewarning.h"

#include "evo/deterministicmns.h"
#include "znode.h"
#include "znodesync-interface.h"
#include "chain.h"
#include "znodeconfig.h"
#include "znodeman.h"
#include "warnings.h"
#include "validation.h"


#ifdef ENABLE_WALLET
#include "walletmodel.h"
#include "wallet/wallet.h"
#endif

bool NotifyTnodeWarning::nConsidered = false;

void NotifyTnodeWarning::notify()
{
    const Consensus::Params& params = ::Params().GetConsensus();
    float numBlocksToEnforcement = params.DIP0003EnforcementHeight - chainActive.Tip()->nHeight;
    float minutesToEnforcement = numBlocksToEnforcement * (params.nPowTargetSpacingMTP / 60);
    float daysDecimal = minutesToEnforcement / 60 / 24;
    float daysToEnforcement = floor(daysDecimal);
    float hoursToEnforcement = floor((daysDecimal > 0 ? (daysDecimal - daysToEnforcement) : 0) * 24);

    std::string strWarning = strprintf(_("WARNING: Legacy znodes detected. You should migrate to the new Tnode layout before it becomes enforced (approximately %i days and %i hours). For details on how to migrate, go to https://zcoin.io/znode-migration"),
        (int)daysToEnforcement,
        (int)hoursToEnforcement);

    SetMiscWarning(strWarning);
    uiInterface.NotifyAlertChanged();
}

bool NotifyTnodeWarning::shouldShow()
{
#ifdef ENABLE_WALLET
    if(nConsidered ||                                         // already fully considered warning
       znodeConfig.getCount() == 0 ||                         // no legacy znodes detected
       !CTnode::IsLegacyWindow(chainActive.Tip()->nHeight) || // outside of legacy window
       !pwalletMain ||                                        // wallet not yet loaded
       !tnodeSyncInterface.IsSynced())                        // znode state not yet synced
        return false;

    // get Tnode entries.
    std::vector<COutPoint> vOutpts;
    bool nGotProReg = false;
    uint256 mnTxHash;
    int outputIndex;
    BOOST_FOREACH(CTnodeConfig::CTnodeEntry mne, znodeConfig.getEntries()) {
      
        CTnode* mn = mnodeman.Find(mne.getTxHash(), mne.getOutputIndex());
        // in the case that the Tnode has dissapeared from the network, was never initialized, or it's outpoint has been spent (disabled Tnode).
        if(mn==NULL || mn->IsOutpointSpent())
            continue;

        // So we have a valid legacy Tnode. get ProReg transactions, look for the same collateral.
        if(!nGotProReg){
            LOCK2(cs_main, pwalletMain->cs_wallet);
            pwalletMain->ListProTxCoins(vOutpts);
            nGotProReg = true;
        }
        bool foundOutpoint = false;
        mnTxHash.SetHex(mne.getTxHash());
        outputIndex = boost::lexical_cast<unsigned int>(mne.getOutputIndex());
        COutPoint outpoint = COutPoint(mnTxHash, outputIndex);
        for (const auto& outpt : vOutpts) {
            if(outpt==outpoint){
                foundOutpoint = true;
                break;
            }
        }

        // if collateral not found, show warning.
        if(!foundOutpoint){
            nConsidered = true;
            return true;
        }
    }

    // if we get to here, the warning will never be shown, and so is fully considered (All znodes ported or expired)
    nConsidered = true;
#endif
    return false;
}

// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2019 The PIVX developers
// Copyright (c) 2018-2019 The Zenon developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "chainparams.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "masternode-budget.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "rpc/server.h"
#include "utilmoneystr.h"

#include <univalue.h>

#include <fstream>

void budgetToJSON(CBudgetProposal* pbudgetProposal, UniValue& bObj)
{
    bObj.push_back(Pair("Name", pbudgetProposal->GetName()));
    bObj.push_back(Pair("URL", pbudgetProposal->GetURL()));
    bObj.push_back(Pair("Hash", pbudgetProposal->GetHash().ToString()));
    bObj.push_back(Pair("FeeHash", pbudgetProposal->nFeeTXHash.ToString()));
    bObj.push_back(Pair("BlockStart", (int64_t)pbudgetProposal->GetBlockStart()));
    bObj.push_back(Pair("BlockEnd", (int64_t)pbudgetProposal->GetBlockEnd()));
    bObj.push_back(Pair("Ratio", pbudgetProposal->GetRatio()));
    bObj.push_back(Pair("Yeas", (int64_t)pbudgetProposal->GetYeas()));
    bObj.push_back(Pair("Nays", (int64_t)pbudgetProposal->GetNays()));
    bObj.push_back(Pair("Abstains", (int64_t)pbudgetProposal->GetAbstains()));
    bObj.push_back(Pair("IsEstablished", pbudgetProposal->IsEstablished()));

    std::string strError = "";
    bObj.push_back(Pair("IsValid", pbudgetProposal->IsValid(strError)));
    bObj.push_back(Pair("IsValidReason", strError.c_str()));
    bObj.push_back(Pair("fValid", pbudgetProposal->fValid));
}

void checkBudgetInputs(const UniValue& params, std::string &strProposalName, std::string &strURL,
                       int &nBlockStart, int &nBlockEnd)
{
    strProposalName = SanitizeString(params[0].get_str());
    if (strProposalName.size() > 60)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid proposal name, limit of 20 characters.");

    strURL = SanitizeString(params[1].get_str());
    std::string strErr;
    if (!validateURL(strURL, strErr))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strErr);

    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev)
        throw JSONRPCError(RPC_IN_WARMUP, "Try again after active chain is loaded");

    // Start must be in the next budget cycle or later
    int pHeight = pindexPrev->nHeight;

    nBlockStart = params[2].get_int();
    if (nBlockStart < pHeight) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block start - must be greater than current block.");
    }

    nBlockEnd = params[3].get_int();
    if (nBlockEnd <= nBlockStart) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block end - must be greater than start block.");
    }
}

UniValue preparebudget(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 4)
        throw std::runtime_error(
            "preparebudget \"proposal-name\" \"url\" block-start block-end\n"
            "\nPrepare proposal for network by signing and creating tx\n"

            "\nArguments:\n"
            "1. \"proposal-name\":  (string, required) Desired proposal name (20 character limit)\n"
            "2. \"url\":            (string, required) URL of proposal details (64 character limit)\n"
            "3. block-start:      (numeric, required) Starting block height\n"
            "4. block-end:      (numeric, required) Ending block height\n"

            "\nResult:\n"
            "\"xxxx\"       (string) proposal fee hash (if successful) or error message (if failed)\n"

            "\nExamples:\n" +
            HelpExampleCli("preparebudget", "\"test-proposal\" \"https://forum.zenon.network/t/test-proposal\" 820800 830800") +
            HelpExampleRpc("preparebudget", "\"test-proposal\" \"https://forum.zenon.network/t/test-proposal\" 820800 830800"));

    if (!pwalletMain) {
        throw JSONRPCError(RPC_IN_WARMUP, "Try again after active chain is loaded");
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    std::string strProposalName;
    std::string strURL;
    int nBlockStart;
    int nBlockEnd;

    checkBudgetInputs(params, strProposalName, strURL, nBlockStart, nBlockEnd);


    // create transaction 15 minutes into the future, to allow for confirmation time
    CBudgetProposalBroadcast budgetProposalBroadcast(strProposalName, strURL, nBlockStart, nBlockEnd, 0);

    if(budgetProposalBroadcast.Sign()) {
        return "Spork key valid, skip to submitbudget.";
    }

    std::string strError = "";
    if (!budgetProposalBroadcast.IsValid(strError, false)) {
        throw std::runtime_error("Proposal is not valid - " + budgetProposalBroadcast.GetHash().ToString() + " - " + strError);
    }

    bool useIX = false;

    CWalletTx wtx;
    if (!pwalletMain->GetBudgetSystemCollateralTX(wtx, budgetProposalBroadcast.GetHash(), useIX)) {
        throw std::runtime_error("Error making collateral transaction for proposal. Please check your wallet balance.");
    }

    // make our change address
    CReserveKey reservekey(pwalletMain);
    //send the tx to the network
    pwalletMain->CommitTransaction(wtx, reservekey, useIX ? "ix" : "tx");

    return wtx.GetHash().ToString();
}

UniValue submitbudget(const UniValue& params, bool fHelp)
{
    if (fHelp || (mapArgs.count("-sporkkey") && params.size() != 4) || (!mapArgs.count("-sporkkey") && params.size() != 5))
        throw std::runtime_error(
            "submitbudget \"proposal-name\" \"url\" payment-count block-start block-end \"fee-tx\"\n"
            "\nSubmit proposal to the network\n"

            "\nArguments:\n"
            "1. \"proposal-name\":  (string, required) Desired proposal name (20 character limit)\n"
            "2. \"url\":            (string, required) URL of proposal details (64 character limit)\n"
            "3. block-start:      (numeric, required) Starting block height\n"
            "4. block-end:      (numeric, required) Ending block height\n"
            "5. \"fee-tx\":         (string, required) Transaction hash from preparebudget command\n"

            "\nResult:\n"
            "\"xxxx\"       (string) proposal hash (if successful) or error message (if failed)\n"

            "\nExamples:\n" +
            HelpExampleCli("submitbudget", "\"test-proposal\" \"https://forum.zenon.network/t/test-proposal\" 820800 830800") +
            HelpExampleRpc("submitbudget", "\"test-proposal\" \"https://forum.zenon.network/t/test-proposal\" 820800 830800"));

    std::string strProposalName;
    std::string strURL;
    int nBlockStart;
    int nBlockEnd;

    checkBudgetInputs(params, strProposalName, strURL, nBlockStart, nBlockEnd);

    uint256 hash = 0;
    if(!mapArgs.count("-sporkkey")) {
        hash = ParseHashV(params[6], "parameter 1");
    }

    //create the proposal incase we're the first to make it
    CBudgetProposalBroadcast budgetProposalBroadcast(strProposalName, strURL, nBlockStart, nBlockEnd, hash);

    std::string strError = "";
    int nConf = 0;
    if (!budgetProposalBroadcast.Sign() && !IsBudgetCollateralValid(hash, budgetProposalBroadcast.GetHash(), strError, budgetProposalBroadcast.nTime, nConf)) {
        throw std::runtime_error("Proposal FeeTX is not valid - " + hash.ToString() + " - " + strError);
    }

    if (!masternodeSync.IsBlockchainSynced()) {
        throw std::runtime_error("Must wait for client to sync with masternode network. Try again in a minute or so.");
    }

    if(!budgetProposalBroadcast.SignatureValid() && !budgetProposalBroadcast.IsValid(strError)){
        return "Proposal is not valid - " + budgetProposalBroadcast.GetHash().ToString() + " - " + strError;
    }

    budget.mapSeenMasternodeBudgetProposals.insert(std::make_pair(budgetProposalBroadcast.GetHash(), budgetProposalBroadcast));
    budgetProposalBroadcast.Relay();

    if(budget.AddProposal(budgetProposalBroadcast)) {
        return budgetProposalBroadcast.GetHash().ToString();
    }

    throw std::runtime_error("Invalid proposal, see debug.log for details.");
}

UniValue mnbudgetvote(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();

        // Backwards compatibility with legacy `mnbudget` command
        if (strCommand == "vote") strCommand = "local";
        if (strCommand == "vote-many") strCommand = "many";
        if (strCommand == "vote-alias") strCommand = "alias";
    }

    if (fHelp || (params.size() == 3 && (strCommand != "local" && strCommand != "many")) || (params.size() == 4 && strCommand != "alias") ||
        params.size() > 4 || params.size() < 3)
        throw std::runtime_error(
            "mnbudgetvote \"local|many|alias\" \"votehash\" \"yes|no\" ( \"alias\" )\n"
            "\nVote on a budget proposal\n"

            "\nArguments:\n"
            "1. \"mode\"      (string, required) The voting mode. 'local' for voting directly from a masternode, 'many' for voting with a MN controller and casting the same vote for each MN, 'alias' for voting with a MN controller and casting a vote for a single MN\n"
            "2. \"votehash\"  (string, required) The vote hash for the proposal\n"
            "3. \"votecast\"  (string, required) Your vote. 'yes' to vote for the proposal, 'no' to vote against\n"
            "4. \"alias\"     (string, required for 'alias' mode) The MN alias to cast a vote for.\n"

            "\nResult:\n"
            "{\n"
            "  \"overall\": \"xxxx\",      (string) The overall status message for the vote cast\n"
            "  \"detail\": [\n"
            "    {\n"
            "      \"node\": \"xxxx\",      (string) 'local' or the MN alias\n"
            "      \"result\": \"xxxx\",    (string) Either 'Success' or 'Failed'\n"
            "      \"error\": \"xxxx\",     (string) Error message, if vote failed\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("mnbudgetvote", "\"local\" \"ed2f83cedee59a91406f5f47ec4d60bf5a7f9ee6293913c82976bd2d3a658041\" \"yes\"") +
            HelpExampleRpc("mnbudgetvote", "\"local\" \"ed2f83cedee59a91406f5f47ec4d60bf5a7f9ee6293913c82976bd2d3a658041\" \"yes\""));

    uint256 hash = ParseHashV(params[1], "parameter 1");
    std::string strVote = params[2].get_str();

    if (strVote != "yes" && strVote != "no" && strVote != "delete") return "You can only vote 'yes' or 'no'";
    int nVote = VOTE_ABSTAIN;
    if (strVote == "yes") nVote = VOTE_YES;
    if (strVote == "no") nVote = VOTE_NO;
    if (strVote == "delete") {
        nVote = VOTE_DELETE;
        if(strCommand != "local")
            return "Not possible";
    }    

    int success = 0;
    int failed = 0;

    UniValue resultsObj(UniValue::VARR);

    CBudgetProposal* current_proposal = budget.FindProposal(hash);
    if(current_proposal != NULL){
        if(current_proposal -> nBlockStart > chainActive.Height() || current_proposal -> nBlockEnd < chainActive.Height()){
            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("error", "Proposal can only be voted between block-start and block-end."));
            resultsObj.push_back(statusObj);
            UniValue returnObj(UniValue::VOBJ);
            returnObj.push_back(Pair("error", resultsObj));
            return returnObj;
        }
    }

    if (strCommand == "local") {
        CPubKey pubKeyMasternode;
        CKey keyMasternode;
        std::string errorMessage;

        UniValue statusObj(UniValue::VOBJ);

        while (true) {
            if (!obfuScationSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMasternode, pubKeyMasternode)) {
                failed++;
                statusObj.push_back(Pair("node", "local"));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Masternode signing error, could not set key correctly: " + errorMessage));
                resultsObj.push_back(statusObj);
                break;
            }

            CMasternode* pmn = mnodeman.Find(activeMasternode.vin);
            if (pmn == NULL || mnodeman.IsPillar(pmn -> vin.prevout) == 0) {
                failed++;
                statusObj.push_back(Pair("node", "local"));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Failure to find pillar in list : " + activeMasternode.vin.ToString()));
                resultsObj.push_back(statusObj);
                break;
            }

            CBudgetVote vote(activeMasternode.vin, hash, nVote);

            if(strVote == "delete"){
                if (!mapArgs.count("-sporkkey")){
                    LogPrint("mnbudget","CBudgetProposal::Sign - Error missing sporkkey");
                    return false;
                }

                std::string spork_key = GetArg("-sporkkey", "");
                CKey key;
                CPubKey pub_key(ParseHex(Params().SporkKey()));

                std::string errorMessage;

                if (!obfuScationSigner.SetKey(spork_key, errorMessage, key, pub_key)) {
                    LogPrint("mnbudget","CBudgetProposal::Sign - Error upon calling SetKey: %s\n", errorMessage);
                    return false;
                }
                if(!vote.Sign(key, pub_key)){
                    failed++;
                    statusObj.push_back(Pair("node", "local"));
                    statusObj.push_back(Pair("result", "failed"));
                    statusObj.push_back(Pair("error", "Failure to sign."));
                    resultsObj.push_back(statusObj);
                    break;
                }
            }else{
                if (!vote.Sign(keyMasternode, pubKeyMasternode)) {
                    failed++;
                    statusObj.push_back(Pair("node", "local"));
                    statusObj.push_back(Pair("result", "failed"));
                    statusObj.push_back(Pair("error", "Failure to sign."));
                    resultsObj.push_back(statusObj);
                    break;
                }
            }
            std::string strError = "";
            if (budget.UpdateProposal(vote, NULL, strError)) {
                success++;
                budget.mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
                vote.Relay();
                statusObj.push_back(Pair("node", "local"));
                statusObj.push_back(Pair("result", "success"));
                statusObj.push_back(Pair("error", ""));
            } else {
                failed++;
                statusObj.push_back(Pair("node", "local"));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Error voting : " + strError));
            }
            resultsObj.push_back(statusObj);
            break;
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if (strCommand == "many") {
        for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
            std::string errorMessage;
            std::vector<unsigned char> vchMasterNodeSignature;
            std::string strMasterNodeSignMessage;

            CPubKey pubKeyCollateralAddress;
            CKey keyCollateralAddress;
            CPubKey pubKeyMasternode;
            CKey keyMasternode;

            UniValue statusObj(UniValue::VOBJ);

            if (!obfuScationSigner.SetKey(mne.getPrivKey(), errorMessage, keyMasternode, pubKeyMasternode)) {
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Masternode signing error, could not set key correctly: " + errorMessage));
                resultsObj.push_back(statusObj);
                continue;
            }

            CMasternode* pmn = mnodeman.Find(pubKeyMasternode);
            if (pmn == NULL || mnodeman.IsPillar(pmn -> vin.prevout) == 0) {
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Can't find pillar by pubkey"));
                resultsObj.push_back(statusObj);
                continue;
            }

            CBudgetVote vote(pmn->vin, hash, nVote);
            if (!vote.Sign(keyMasternode, pubKeyMasternode)) {
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Failure to sign."));
                resultsObj.push_back(statusObj);
                continue;
            }

            std::string strError = "";
            if (budget.UpdateProposal(vote, NULL, strError)) {
                budget.mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
                vote.Relay();
                success++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "success"));
                statusObj.push_back(Pair("error", ""));
            } else {
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", strError.c_str()));
            }

            resultsObj.push_back(statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if (strCommand == "alias") {
        std::string strAlias = params[3].get_str();
        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masternodeConfig.getEntries();

        for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {

            if( strAlias != mne.getAlias()) continue;

            std::string errorMessage;
            std::vector<unsigned char> vchMasterNodeSignature;
            std::string strMasterNodeSignMessage;

            CPubKey pubKeyCollateralAddress;
            CKey keyCollateralAddress;
            CPubKey pubKeyMasternode;
            CKey keyMasternode;

            UniValue statusObj(UniValue::VOBJ);

            if(!obfuScationSigner.SetKey(mne.getPrivKey(), errorMessage, keyMasternode, pubKeyMasternode)){
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Masternode signing error, could not set key correctly: " + errorMessage));
                resultsObj.push_back(statusObj);
                continue;
            }

            CMasternode* pmn = mnodeman.Find(pubKeyMasternode);
            if(pmn == NULL || mnodeman.IsPillar(pmn -> vin.prevout) == 0)
            {
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Can't find masternode by pubkey"));
                resultsObj.push_back(statusObj);
                continue;
            }

            CBudgetVote vote(pmn->vin, hash, nVote);
            if(!vote.Sign(keyMasternode, pubKeyMasternode)){
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Failure to sign."));
                resultsObj.push_back(statusObj);
                continue;
            }

            std::string strError = "";
            if(budget.UpdateProposal(vote, NULL, strError)) {
                budget.mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
                vote.Relay();
                success++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "success"));
                statusObj.push_back(Pair("error", ""));
            } else {
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", strError.c_str()));
            }

            resultsObj.push_back(statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    return NullUniValue;
}

UniValue getbudgetvotes(const UniValue& params, bool fHelp)
{
    if (params.size() != 1)
        throw std::runtime_error(
            "getbudgetvotes \"proposal-name\"\n"
            "\nPrint vote information for a budget proposal\n"

            "\nArguments:\n"
            "1. \"proposal-name\":      (string, required) Name of the proposal\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"mnId\": \"xxxx\",        (string) Hash of the masternode's collateral transaction\n"
            "    \"nHash\": \"xxxx\",       (string) Hash of the vote\n"
            "    \"Vote\": \"YES|NO\",      (string) Vote cast ('YES' or 'NO')\n"
            "    \"nTime\": xxxx,         (numeric) Time in seconds since epoch the vote was cast\n"
            "    \"fValid\": true|false,  (boolean) 'true' if the vote is valid, 'false' otherwise\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getbudgetvotes", "\"test-proposal\"") + HelpExampleRpc("getbudgetvotes", "\"test-proposal\""));

    std::string strProposalName = SanitizeString(params[0].get_str());

    UniValue ret(UniValue::VARR);

    CBudgetProposal* pbudgetProposal = budget.FindProposal(strProposalName);

    if (pbudgetProposal == NULL) throw std::runtime_error("Unknown proposal name");

    std::map<uint256, CBudgetVote>::iterator it = pbudgetProposal->mapVotes.begin();
    while (it != pbudgetProposal->mapVotes.end()) {
        UniValue bObj(UniValue::VOBJ);
        bObj.push_back(Pair("mnId", (*it).second.vin.prevout.hash.ToString()));
        bObj.push_back(Pair("nHash", (*it).first.ToString().c_str()));
        bObj.push_back(Pair("Vote", (*it).second.GetVoteString()));
        bObj.push_back(Pair("nTime", (int64_t)(*it).second.nTime));
        bObj.push_back(Pair("fValid", (*it).second.fValid));

        ret.push_back(bObj);

        it++;
    }

    return ret;
}

UniValue getnextsuperblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getnextsuperblock\n"
            "\nPrint the next super block height\n"

            "\nResult:\n"
            "n      (numeric) Block height of the next super block\n"

            "\nExamples:\n" +
            HelpExampleCli("getnextsuperblock", "") + HelpExampleRpc("getnextsuperblock", ""));

    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return "unknown";

    int nNext = pindexPrev->nHeight - pindexPrev->nHeight % Params().GetBudgetCycleBlocks() + Params().GetBudgetCycleBlocks();
    return nNext;
}

UniValue getbudgetinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "getbudgetinfo ( \"proposal\" )\n"
            "\nShow current masternode budgets\n"

            "\nArguments:\n"
            "1. \"proposal\"    (string, optional) Proposal name\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"Name\": \"xxxx\",               (string) Proposal Name\n"
            "    \"URL\": \"xxxx\",                (string) Proposal URL\n"
            "    \"Hash\": \"xxxx\",               (string) Proposal vote hash\n"
            "    \"FeeHash\": \"xxxx\",            (string) Proposal fee hash\n"
            "    \"BlockStart\": n,              (numeric) Proposal starting block\n"
            "    \"BlockEnd\": n,                (numeric) Proposal ending block\n"
            "    \"Ratio\": x.xxx,               (numeric) Ratio of yeas vs nays\n"
            "    \"Yeas\": n,                    (numeric) Number of yea votes\n"
            "    \"Nays\": n,                    (numeric) Number of nay votes\n"
            "    \"Abstains\": n,                (numeric) Number of abstains\n"
            "    \"IsEstablished\": true|false,  (boolean) Established (true) or (false)\n"
            "    \"IsValid\": true|false,        (boolean) Valid (true) or Invalid (false)\n"
            "    \"IsValidReason\": \"xxxx\",      (string) Error message, if any\n"
            "    \"fValid\": true|false,         (boolean) Valid (true) or Invalid (false)\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getbudgetprojection", "") + HelpExampleRpc("getbudgetprojection", ""));

    UniValue ret(UniValue::VARR);

    std::string strShow = "valid";
    if (params.size() == 1) {
        std::string strProposalName = SanitizeString(params[0].get_str());
        CBudgetProposal* pbudgetProposal = budget.FindProposal(strProposalName);
        if (pbudgetProposal == NULL) throw std::runtime_error("Unknown proposal name");
        UniValue bObj(UniValue::VOBJ);
        budgetToJSON(pbudgetProposal, bObj);
        ret.push_back(bObj);
        return ret;
    }

    std::vector<CBudgetProposal*> winningProps = budget.GetAllProposals();
    for (CBudgetProposal* pbudgetProposal : winningProps) {
        if (strShow == "valid" && !pbudgetProposal->fValid) continue;

        UniValue bObj(UniValue::VOBJ);
        budgetToJSON(pbudgetProposal, bObj);

        ret.push_back(bObj);
    }

    return ret;
}

UniValue mnbudgetrawvote(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 6)
        throw std::runtime_error(
            "mnbudgetrawvote \"masternode-tx-hash\" masternode-tx-index \"proposal-hash\" yes|no time \"vote-sig\"\n"
            "\nCompile and relay a proposal vote with provided external signature instead of signing vote internally\n"

            "\nArguments:\n"
            "1. \"masternode-tx-hash\"  (string, required) Transaction hash for the masternode\n"
            "2. masternode-tx-index   (numeric, required) Output index for the masternode\n"
            "3. \"proposal-hash\"       (string, required) Proposal vote hash\n"
            "4. yes|no                (boolean, required) Vote to cast\n"
            "5. time                  (numeric, required) Time since epoch in seconds\n"
            "6. \"vote-sig\"            (string, required) External signature\n"

            "\nResult:\n"
            "\"status\"     (string) Vote status or error message\n"

            "\nExamples:\n" +
            HelpExampleCli("mnbudgetrawvote", "") + HelpExampleRpc("mnbudgetrawvote", ""));

    uint256 hashMnTx = ParseHashV(params[0], "mn tx hash");
    int nMnTxIndex = params[1].get_int();
    CTxIn vin = CTxIn(hashMnTx, nMnTxIndex);

    uint256 hashProposal = ParseHashV(params[2], "Proposal hash");
    std::string strVote = params[3].get_str();

    if (strVote != "yes" && strVote != "no") return "You can only vote 'yes' or 'no'";
    int nVote = VOTE_ABSTAIN;
    if (strVote == "yes") nVote = VOTE_YES;
    if (strVote == "no") nVote = VOTE_NO;

    int64_t nTime = params[4].get_int64();
    std::string strSig = params[5].get_str();
    bool fInvalid = false;
    std::vector<unsigned char> vchSig = DecodeBase64(strSig.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CMasternode* pmn = mnodeman.Find(vin);
    if (pmn == NULL || mnodeman.IsPillar(pmn -> vin.prevout) == 0) {
        return "Failure to find pillar in list : " + vin.ToString();
    }

    CBudgetVote vote(vin, hashProposal, nVote);
    vote.nTime = nTime;
    vote.vchSig = vchSig;

    if (!vote.SignatureValid(true)) {
        return "Failure to verify signature.";
    }

    std::string strError = "";
    if (budget.UpdateProposal(vote, NULL, strError)) {
        budget.mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
        vote.Relay();
        return "Voted successfully";
    } else {
        return "Error voting : " + strError;
    }
}

UniValue checkbudgets(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "checkbudgets\n"
            "\nInitiates a buddget check cycle manually\n"

            "\nExamples:\n" +
            HelpExampleCli("checkbudgets", "") + HelpExampleRpc("checkbudgets", ""));

    budget.CheckAndRemove();

    return NullUniValue;
}

// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2019 The PIVX developers
// Copyright (c) 2018-2019 The Zenon developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "init.h"
#include "main.h"

#include "addrman.h"
#include "chainparams.h"
#include "masternode-budget.h"
#include "masternode-sync.h"
#include "masternode.h"
#include "masternodeman.h"
#include "obfuscation.h"
#include "spork.h"
#include "util.h"
#include <boost/filesystem.hpp>

CBudgetManager budget;
CCriticalSection cs_budget;

std::map<uint256, int64_t> askedForSourceProposalOrBudget;
std::vector<CBudgetProposalBroadcast> vecImmatureBudgetProposals;


bool IsBudgetCollateralValid(uint256 nTxCollateralHash, uint256 nExpectedHash, std::string& strError, int64_t& nTime, int& nConf)
{
    CTransaction txCollateral;
    uint256 nBlockHash;
    if (!GetTransaction(nTxCollateralHash, txCollateral, nBlockHash, true)) {
        strError = strprintf("Can't find collateral tx %s", txCollateral.ToString());
        LogPrint("mnbudget","CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
        return false;
    }

    if (txCollateral.vout.size() < 1) return false;
    if (txCollateral.nLockTime != 0) return false;

    CScript findScript;
    findScript << OP_RETURN << ToByteVector(nExpectedHash);

    bool foundOpReturn = false;
    for (const CTxOut &o : txCollateral.vout) {
        if (!o.scriptPubKey.IsNormalPaymentScript() && !o.scriptPubKey.IsUnspendable()) {
            strError = strprintf("Invalid Script %s", txCollateral.ToString());
            LogPrint("mnbudget","CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
            return false;
        }
        // Collateral for normal budget proposal
        LogPrint("mnbudget", "Normal Budget: o.scriptPubKey(%s) == findScript(%s) ?\n", o.scriptPubKey.ToString(), findScript.ToString());
        if (o.scriptPubKey == findScript) {
            LogPrint("mnbudget", "Normal Budget: o.nValue(%ld) >= PROPOSAL_FEE_TX(%ld) ?\n", o.nValue, PROPOSAL_FEE_TX);
            if(o.nValue >= PROPOSAL_FEE_TX) {
                foundOpReturn = true;
            }
        }
    }
    if (!foundOpReturn) {
        strError = strprintf("Couldn't find opReturn %s in %s", nExpectedHash.ToString(), txCollateral.ToString());
        LogPrint("mnbudget","CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
        return false;
    }

    // RETRIEVE CONFIRMATIONS AND NTIME
    /*
        - nTime starts as zero and is passed-by-reference out of this function and stored in the external proposal
        - nTime is never validated via the hashing mechanism and comes from a full-validated source (the blockchain)
    */

    int conf = GetIXConfirmations(nTxCollateralHash);
    if (nBlockHash != uint256(0)) {
        BlockMap::iterator mi = mapBlockIndex.find(nBlockHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                conf += chainActive.Height() - pindex->nHeight + 1;
                nTime = pindex->nTime;
            }
        }
    }

    nConf = conf;

    //if we're syncing we won't have swiftTX information, so accept 1 confirmation
    if (conf >= Params().Budget_Fee_Confirmations()) {
        return true;
    } else {
        strError = strprintf("Collateral requires at least %d confirmations - %d confirmations", Params().Budget_Fee_Confirmations(), conf);
        LogPrint("mnbudget","CBudgetProposalBroadcast::IsBudgetCollateralValid - %s - %d confirmations\n", strError, conf);
        return false;
    }
}

void CBudgetManager::CheckOrphanVotes()
{
    LOCK(cs);


    std::string strError = "";
    std::map<uint256, CBudgetVote>::iterator it1 = mapOrphanMasternodeBudgetVotes.begin();
    while (it1 != mapOrphanMasternodeBudgetVotes.end()) {
        if (budget.UpdateProposal(((*it1).second), NULL, strError)) {
            LogPrint("mnbudget","CBudgetManager::CheckOrphanVotes - Proposal/Budget is known, activating and removing orphan vote\n");
            mapOrphanMasternodeBudgetVotes.erase(it1++);
        } else {
            ++it1;
        }
    }
    LogPrint("mnbudget","CBudgetManager::CheckOrphanVotes - Done\n");
}

//
// CBudgetDB
//

CBudgetDB::CBudgetDB()
{
    pathDB = GetDataDir() / "budget.dat";
    strMagicMessage = "MasternodeBudget";
}

bool CBudgetDB::Write(const CBudgetManager& objToSave)
{
    LOCK(objToSave.cs);

    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // masternode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint("mnbudget","Written info to budget.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CBudgetDB::ReadResult CBudgetDB::Read(CBudgetManager& objToLoad, bool fDryRun)
{
    LOCK(objToLoad.cs);

    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    std::vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }


    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masternode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid masternode cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CBudgetManager object
        ssObj >> objToLoad;
    } catch (std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("mnbudget","Loaded info from budget.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("mnbudget","  %s\n", objToLoad.ToString());
    if (!fDryRun) {
        LogPrint("mnbudget","Budget manager - cleaning....\n");
        objToLoad.CheckAndRemove();
        LogPrint("mnbudget","Budget manager - result:\n");
        LogPrint("mnbudget","  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpBudgets()
{
    int64_t nStart = GetTimeMillis();

    CBudgetDB budgetdb;
    CBudgetManager tempBudget;

    LogPrint("mnbudget","Verifying budget.dat format...\n");
    CBudgetDB::ReadResult readResult = budgetdb.Read(tempBudget, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CBudgetDB::FileError)
        LogPrint("mnbudget","Missing budgets file - budget.dat, will try to recreate\n");
    else if (readResult != CBudgetDB::Ok) {
        LogPrint("mnbudget","Error reading budget.dat: ");
        if (readResult == CBudgetDB::IncorrectFormat)
            LogPrint("mnbudget","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("mnbudget","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("mnbudget","Writting info to budget.dat...\n");
    budgetdb.Write(budget);

    LogPrint("mnbudget","Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool CBudgetManager::AddProposal(CBudgetProposal& budgetProposal)
{
    LOCK(cs);
    std::string strError = "";
    if (!budgetProposal.IsValid(strError)) {
        LogPrint("mnbudget","CBudgetManager::AddProposal - invalid budget proposal - %s\n", strError);
        return false;
    }

    if (mapProposals.count(budgetProposal.GetHash())) {
        return false;
    }

    mapProposals.insert(std::make_pair(budgetProposal.GetHash(), budgetProposal));
    LogPrint("mnbudget","CBudgetManager::AddProposal - proposal %s added\n", budgetProposal.GetName ().c_str ());
    return true;
}

void CBudgetManager::CheckAndRemove()
{
    int nHeight = 0;

    // Add some verbosity once loading blocks from files has finished
    {
        TRY_LOCK(cs_main, locked);
        if ((locked) && (chainActive.Tip() != NULL)) {
            CBlockIndex* pindexPrev = chainActive.Tip();
            if (pindexPrev) {
                nHeight = pindexPrev->nHeight;
            }
        }
    }

    LogPrint("mnbudget", "CBudgetManager::CheckAndRemove at Height=%d\n", nHeight);

    std::map<uint256, CBudgetProposal> tmpMapProposals;

    std::string strError = "";

    LogPrint("mnbudget", "CBudgetManager::CheckAndRemove - mapProposals cleanup - size before: %d\n", mapProposals.size());
    std::map<uint256, CBudgetProposal>::iterator it2 = mapProposals.begin();
    while (it2 != mapProposals.end()) {
        CBudgetProposal* pbudgetProposal = &((*it2).second);
        pbudgetProposal->fValid = pbudgetProposal->IsValid(strError);
        if (!strError.empty ()) {
            LogPrint("mnbudget","CBudgetManager::CheckAndRemove - Invalid budget proposal - %s\n", strError);
            strError = "";
        }
        else {
             LogPrint("mnbudget","CBudgetManager::CheckAndRemove - Found valid budget proposal: %s %s\n",
                      pbudgetProposal->strProposalName.c_str(), pbudgetProposal->nFeeTXHash.ToString().c_str());
        }
        if (pbudgetProposal->fValid) {
            tmpMapProposals.insert(std::make_pair(pbudgetProposal->GetHash(), *pbudgetProposal));
        }

        ++it2;
    }
    // Remove invalid entries by overwriting complete map
    mapProposals.swap(tmpMapProposals);

    // clang doesn't accept copy assignemnts :-/
    // mapProposals = tmpMapProposals;

    LogPrint("mnbudget", "CBudgetManager::CheckAndRemove - mapProposals cleanup - size after: %d\n", mapProposals.size());
    LogPrint("mnbudget","CBudgetManager::CheckAndRemove - PASSED\n");

}

CBudgetProposal* CBudgetManager::FindProposal(const std::string& strProposalName)
{
    //find the prop with the highest yes count

    int nYesCount = -99999;
    CBudgetProposal* pbudgetProposal = NULL;

    std::map<uint256, CBudgetProposal>::iterator it = mapProposals.begin();
    while (it != mapProposals.end()) {
        if ((*it).second.strProposalName == strProposalName && (*it).second.GetYeas() > nYesCount) {
            pbudgetProposal = &((*it).second);
            nYesCount = pbudgetProposal->GetYeas();
        }
        ++it;
    }

    if (nYesCount == -99999) return NULL;

    return pbudgetProposal;
}

CBudgetProposal* CBudgetManager::FindProposal(uint256 nHash)
{
    LOCK(cs);

    if (mapProposals.count(nHash))
        return &mapProposals[nHash];

    return NULL;
}

std::vector<CBudgetProposal*> CBudgetManager::GetAllProposals()
{
    LOCK(cs);

    std::vector<CBudgetProposal*> vBudgetProposalRet;

    std::map<uint256, CBudgetProposal>::iterator it = mapProposals.begin();
    while (it != mapProposals.end()) {
        (*it).second.CleanAndRemove(false);

        CBudgetProposal* pbudgetProposal = &((*it).second);
        vBudgetProposalRet.push_back(pbudgetProposal);

        ++it;
    }

    return vBudgetProposalRet;
}

//
// Sort by votes, if there's a tie sort by their feeHash TX
//
struct sortProposalsByVotes {
    bool operator()(const std::pair<CBudgetProposal*, int>& left, const std::pair<CBudgetProposal*, int>& right)
    {
        if (left.second != right.second)
            return (left.second > right.second);
        return (left.first->nFeeTXHash > right.first->nFeeTXHash);
    }
};

//Need to review this function
std::vector<CBudgetProposal*> CBudgetManager::GetBudget()
{
    LOCK(cs);

    // ------- Sort budgets by Yes Count

    std::vector<std::pair<CBudgetProposal*, int> > vBudgetPorposalsSort;

    std::map<uint256, CBudgetProposal>::iterator it = mapProposals.begin();
    while (it != mapProposals.end()) {
        (*it).second.CleanAndRemove(false);
        vBudgetPorposalsSort.push_back(std::make_pair(&((*it).second), (*it).second.GetYeas() - (*it).second.GetNays()));
        ++it;
    }

    std::sort(vBudgetPorposalsSort.begin(), vBudgetPorposalsSort.end(), sortProposalsByVotes());

    // ------- Grab The Budgets In Order

    std::vector<CBudgetProposal*> vBudgetProposalsRet;

    CAmount nBudgetAllocated = 0;

    CBlockIndex* pindexPrev;
    {
        LOCK(cs_main);
        pindexPrev = chainActive.Tip();
    }
    if (pindexPrev == NULL) return vBudgetProposalsRet;

    int nBlockStart = pindexPrev->nHeight;
    int nBlockEnd = pindexPrev->nHeight;
    int mnCount = mnodeman.CountEnabled(ActiveProtocol());

    std::vector<std::pair<CBudgetProposal*, int> >::iterator it2 = vBudgetPorposalsSort.begin();
    while (it2 != vBudgetPorposalsSort.end()) {
        CBudgetProposal* pbudgetProposal = (*it2).first;

        LogPrint("mnbudget","CBudgetManager::GetBudget() - Processing Budget %s\n", pbudgetProposal->strProposalName.c_str());
        //prop start/end should be inside this period
        if (pbudgetProposal->IsPassing(pindexPrev, nBlockStart, nBlockEnd, mnCount)) {
            LogPrint("mnbudget","CBudgetManager::GetBudget() -   Check 1 passed: valid=%d | %ld <= %ld | %ld >= %ld | Yeas=%d Nays=%d Count=%d | established=%d\n",
                      pbudgetProposal->fValid, pbudgetProposal->nBlockStart, nBlockStart, pbudgetProposal->nBlockEnd,
                      nBlockEnd, pbudgetProposal->GetYeas(), pbudgetProposal->GetNays(), mnCount / 10,
                      pbudgetProposal->IsEstablished());

            vBudgetProposalsRet.push_back(pbudgetProposal);
        }
        else {
            LogPrint("mnbudget","CBudgetManager::GetBudget() -   Check 1 failed: valid=%d | %ld <= %ld | %ld >= %ld | Yeas=%d Nays=%d Count=%d | established=%d\n",
                      pbudgetProposal->fValid, pbudgetProposal->nBlockStart, nBlockStart, pbudgetProposal->nBlockEnd,
                      nBlockEnd, pbudgetProposal->GetYeas(), pbudgetProposal->GetNays(), mnodeman.CountEnabled(ActiveProtocol()) / 10,
                      pbudgetProposal->IsEstablished());
        }

        ++it2;
    }

    return vBudgetProposalsRet;
}

void CBudgetManager::NewBlock()
{
    TRY_LOCK(cs, fBudgetNewBlock);
    if (!fBudgetNewBlock) {
        return;
    }

    if (masternodeSync.RequestedMasternodeAssets <= MASTERNODE_SYNC_BUDGET) return;

    //this function should be called 1/14 blocks, allowing up to 100 votes per day on all proposals
    if (chainActive.Height() % 14 != 0) {
        return;
    }

    // incremental sync with our peers
    if (masternodeSync.IsSynced()) {
        LogPrint("mnbudget","CBudgetManager::NewBlock - incremental sync started\n");
        if (chainActive.Height() % 1440 == rand() % 1440) {
            ClearSeen();
            ResetSync();
        }

        LOCK(cs_vNodes);
        for (CNode* pnode : vNodes)
            if (pnode->nVersion >= ActiveProtocol()) {
                Sync(pnode, 0, true);
            }

        MarkSynced();
    }


    CheckAndRemove();

    //remove invalid votes once in a while (we have to check the signatures and validity of every vote, somewhat CPU intensive)
    LogPrint("mnbudget","CBudgetManager::NewBlock - askedForSourceProposalOrBudget cleanup - size: %d\n", askedForSourceProposalOrBudget.size());
    std::map<uint256, int64_t>::iterator it = askedForSourceProposalOrBudget.begin();
    while (it != askedForSourceProposalOrBudget.end()) {
        if ((*it).second > GetTime() - (60 * 60 * 24)) {
            ++it;
        } else {
            askedForSourceProposalOrBudget.erase(it++);
        }
    }

    LogPrint("mnbudget","CBudgetManager::NewBlock - mapProposals cleanup - size: %d\n", mapProposals.size());
    std::map<uint256, CBudgetProposal>::iterator it2 = mapProposals.begin();
    while (it2 != mapProposals.end()) {
        (*it2).second.CleanAndRemove(false);
        ++it2;
    }

    LogPrint("mnbudget","CBudgetManager::NewBlock - vecImmatureBudgetProposals cleanup - size: %d\n", vecImmatureBudgetProposals.size());
    std::vector<CBudgetProposalBroadcast>::iterator it4 = vecImmatureBudgetProposals.begin();
    while (it4 != vecImmatureBudgetProposals.end()) {
        std::string strError = "";
        int nConf = 0;
        if (!(*it4).SignatureValid() && !IsBudgetCollateralValid((*it4).nFeeTXHash, (*it4).GetHash(), strError, (*it4).nTime, nConf)) {
            ++it4;
            continue;
        }

        if (!(*it4).IsValid(strError)) {
            LogPrint("mnbudget","mprop (immature) - invalid budget proposal - %s\n", strError);
            it4 = vecImmatureBudgetProposals.erase(it4);
            continue;
        }

        CBudgetProposal budgetProposal((*it4));
        if (AddProposal(budgetProposal)) {
            (*it4).Relay();
        }

        LogPrint("mnbudget","mprop (immature) - new budget - %s\n", (*it4).GetHash().ToString());
        it4 = vecImmatureBudgetProposals.erase(it4);
    }

    LogPrint("mnbudget","CBudgetManager::NewBlock - PASSED\n");
}

void CBudgetManager::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    // lite mode is not supported
    if (fLiteMode) return;
    if (!masternodeSync.IsBlockchainSynced()) return;

    LOCK(cs_budget);

    if (strCommand == "mnvs") { //Masternode vote sync
        uint256 nProp;
        vRecv >> nProp;

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (nProp == 0) {
                if (pfrom->HasFulfilledRequest("mnvs")) {
                    LogPrint("mnbudget","mnvs - peer already asked me for the list\n");
                    Misbehaving(pfrom->GetId(), 20);
                    return;
                }
                pfrom->FulfilledRequest("mnvs");
            }
        }

        Sync(pfrom, nProp);
        LogPrint("mnbudget", "mnvs - Sent Masternode votes to peer %i\n", pfrom->GetId());
    }

    if (strCommand == "mprop") { //Masternode Proposal
        CBudgetProposalBroadcast budgetProposalBroadcast;
        vRecv >> budgetProposalBroadcast;

        if (mapSeenMasternodeBudgetProposals.count(budgetProposalBroadcast.GetHash())) {
            masternodeSync.AddedBudgetItem(budgetProposalBroadcast.GetHash());
            return;
        }

        std::string strError = "";
        int nConf = 0;
        if (!budgetProposalBroadcast.SignatureValid() && !IsBudgetCollateralValid(budgetProposalBroadcast.nFeeTXHash, budgetProposalBroadcast.GetHash(), strError, budgetProposalBroadcast.nTime, nConf)) {
            LogPrint("mnbudget","Proposal FeeTX is not valid - %s - %s\n", budgetProposalBroadcast.nFeeTXHash.ToString(), strError);
            if (nConf >= 1) vecImmatureBudgetProposals.push_back(budgetProposalBroadcast);
            return;
        }

        mapSeenMasternodeBudgetProposals.insert(std::make_pair(budgetProposalBroadcast.GetHash(), budgetProposalBroadcast));

        if (!budgetProposalBroadcast.IsValid(strError)) {
            LogPrint("mnbudget","mprop - invalid budget proposal - %s\n", strError);
            return;
        }

        CBudgetProposal budgetProposal(budgetProposalBroadcast);
        if (AddProposal(budgetProposal)) {
            budgetProposalBroadcast.Relay();
        }
        masternodeSync.AddedBudgetItem(budgetProposalBroadcast.GetHash());

        LogPrint("mnbudget","mprop - new budget - %s\n", budgetProposalBroadcast.GetHash().ToString());

        //We might have active votes for this proposal that are valid now
        CheckOrphanVotes();
    }

    if (strCommand == "mvote") { //Masternode Vote
        CBudgetVote vote;
        vRecv >> vote;
        vote.fValid = true;

        if (mapSeenMasternodeBudgetVotes.count(vote.GetHash())) {
            masternodeSync.AddedBudgetItem(vote.GetHash());
            return;
        }

        CMasternode* pmn = mnodeman.Find(vote.vin);
        if (pmn == NULL) {
            LogPrint("mnbudget","mvote - unknown masternode - vin: %s\n", vote.vin.prevout.hash.ToString());
            mnodeman.AskForMN(pfrom, vote.vin);
            return;
        }else if(mnodeman.IsPillar(pmn -> vin.prevout) == 0){
            return;
        }

        mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
        if (!vote.SignatureValid(true)) {
            if (masternodeSync.IsSynced()) {
                LogPrintf("CBudgetManager::ProcessMessage() : mvote - signature invalid\n");
                Misbehaving(pfrom->GetId(), 20);
            }
            // it could just be a non-synced masternode
            mnodeman.AskForMN(pfrom, vote.vin);
            return;
        }

        std::string strError = "";
        if (UpdateProposal(vote, pfrom, strError)) {
            vote.Relay();
            masternodeSync.AddedBudgetItem(vote.GetHash());
        }

        LogPrint("mnbudget","mvote - new budget vote for budget %s - %s\n", vote.nProposalHash.ToString(),  vote.GetHash().ToString());
    }
}

bool CBudgetManager::PropExists(uint256 nHash)
{
    if (mapProposals.count(nHash)) return true;
    return false;
}

//mark that a full sync is needed
void CBudgetManager::ResetSync()
{
    LOCK(cs);


    std::map<uint256, CBudgetProposalBroadcast>::iterator it1 = mapSeenMasternodeBudgetProposals.begin();
    while (it1 != mapSeenMasternodeBudgetProposals.end()) {
        CBudgetProposal* pbudgetProposal = FindProposal((*it1).first);
        if (pbudgetProposal && pbudgetProposal->fValid) {
            //mark votes
            std::map<uint256, CBudgetVote>::iterator it2 = pbudgetProposal->mapVotes.begin();
            while (it2 != pbudgetProposal->mapVotes.end()) {
                (*it2).second.fSynced = false;
                ++it2;
            }
        }
        ++it1;
    }
}

void CBudgetManager::MarkSynced()
{
    LOCK(cs);

    /*
        Mark that we've sent all valid items
    */

    std::map<uint256, CBudgetProposalBroadcast>::iterator it1 = mapSeenMasternodeBudgetProposals.begin();
    while (it1 != mapSeenMasternodeBudgetProposals.end()) {
        CBudgetProposal* pbudgetProposal = FindProposal((*it1).first);
        if (pbudgetProposal && pbudgetProposal->fValid) {
            //mark votes
            std::map<uint256, CBudgetVote>::iterator it2 = pbudgetProposal->mapVotes.begin();
            while (it2 != pbudgetProposal->mapVotes.end()) {
                if ((*it2).second.fValid)
                    (*it2).second.fSynced = true;
                ++it2;
            }
        }
        ++it1;
    }
}


void CBudgetManager::Sync(CNode* pfrom, uint256 nProp, bool fPartial)
{
    LOCK(cs);

    /*
        Sync with a client on the network

        --

        This code checks each of the hash maps for all known budget proposals, then checks them against the
        budget object to see if they're OK. If all checks pass, we'll send it to the peer.

    */

    int nInvCount = 0;

    std::map<uint256, CBudgetProposalBroadcast>::iterator it1 = mapSeenMasternodeBudgetProposals.begin();
    while (it1 != mapSeenMasternodeBudgetProposals.end()) {
        CBudgetProposal* pbudgetProposal = FindProposal((*it1).first);
        if (pbudgetProposal && pbudgetProposal->fValid && (nProp == 0 || (*it1).first == nProp)) {
            pfrom->PushInventory(CInv(MSG_BUDGET_PROPOSAL, (*it1).second.GetHash()));
            nInvCount++;

            //send votes
            std::map<uint256, CBudgetVote>::iterator it2 = pbudgetProposal->mapVotes.begin();
            while (it2 != pbudgetProposal->mapVotes.end()) {
                if ((*it2).second.fValid) {
                    if ((fPartial && !(*it2).second.fSynced) || !fPartial) {
                        pfrom->PushInventory(CInv(MSG_BUDGET_VOTE, (*it2).second.GetHash()));
                        nInvCount++;
                    }
                }
                ++it2;
            }
        }
        ++it1;
    }

    pfrom->PushMessage("ssc", MASTERNODE_SYNC_BUDGET, nInvCount);

    LogPrint("mnbudget", "CBudgetManager::Sync - sent %d items\n", nInvCount);
}

bool CBudgetManager::UpdateProposal(CBudgetVote& vote, CNode* pfrom, std::string& strError)
{
    LOCK(cs);

    if (!mapProposals.count(vote.nProposalHash)) {
        if (pfrom) {
            // only ask for missing items after our syncing process is complete --
            //   otherwise we'll think a full sync succeeded when they return a result
            if (!masternodeSync.IsSynced()) return false;

            LogPrint("mnbudget","CBudgetManager::UpdateProposal - Unknown proposal %d, asking for source proposal\n", vote.nProposalHash.ToString());
            mapOrphanMasternodeBudgetVotes[vote.nProposalHash] = vote;

            if (!askedForSourceProposalOrBudget.count(vote.nProposalHash)) {
                pfrom->PushMessage("mnvs", vote.nProposalHash);
                askedForSourceProposalOrBudget[vote.nProposalHash] = GetTime();
            }
        }

        strError = "Proposal not found!";
        return false;
    }

    if(vote.nVote == 3){
        mapProposals.erase(vote.nProposalHash);
        return true;
    }

    return mapProposals[vote.nProposalHash].AddOrUpdateVote(vote, strError);
}

CBudgetProposal::CBudgetProposal()
{
    strProposalName = "unknown";
    nBlockStart = 0;
    nBlockEnd = 1;
    nTime = 0;
    fValid = true;
}

CBudgetProposal::CBudgetProposal(std::string strProposalNameIn, std::string strURLIn, int nBlockStartIn, int nBlockEndIn, uint256 nFeeTXHashIn)
{
    strProposalName = strProposalNameIn;
    strURL = strURLIn;
    nBlockStart = nBlockStartIn;
    nBlockEnd = nBlockEndIn;
    nFeeTXHash = nFeeTXHashIn;
    fValid = true;
}

CBudgetProposal::CBudgetProposal(const CBudgetProposal& other)
{
    strProposalName = other.strProposalName;
    strURL = other.strURL;
    nBlockStart = other.nBlockStart;
    nBlockEnd = other.nBlockEnd;
    nTime = other.nTime;
    nFeeTXHash = other.nFeeTXHash;
    vchSig = other.vchSig;
    mapVotes = other.mapVotes;
    fValid = true;
}

bool CBudgetProposal::Sign()
{
    if (!mapArgs.count("-sporkkey")){
        LogPrint("mnbudget","CBudgetProposal::Sign - Error missing sporkkey");
        return false;
    }

    std::string spork_key = GetArg("-sporkkey", "");
    CKey key;
    CPubKey pub_key(ParseHex(Params().SporkKey()));

    std::string errorMessage;
    std::string strMessage = GetHash().GetHex();

    if (!obfuScationSigner.SetKey(spork_key, errorMessage, key, pub_key)) {
        LogPrint("mnbudget","CBudgetProposal::Sign - Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig, key)) {
        LogPrint("mnbudget","CBudgetProposal::Sign - Error upon calling SignMessage: %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pub_key, vchSig, strMessage, errorMessage)) {
        LogPrint("mnbudget","CBudgetProposal::Sign - Error upon calling VerifyMessage: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CBudgetProposal::SignatureValid()
{
    CPubKey pubKeyMasternode(ParseHex(Params().SporkKey()));

    std::string errorMessage;
    std::string strMessage = GetHash().GetHex();

    if (!obfuScationSigner.VerifyMessage(pubKeyMasternode, vchSig, strMessage, errorMessage)) {
        LogPrint("mnbudget","CBudgetVote::SignatureValid() - Verify message failed: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CBudgetProposal::IsValid(std::string& strError, bool fCheckCollateral)
{
    if (GetNays() - GetYeas() > mnodeman.CountEnabled(ActiveProtocol()) / 10) {
        strError = "Proposal " + strProposalName + ": Active removal";
        return false;
    }

    if (nBlockStart < 0) {
        strError = "Invalid Proposal";
        return false;
    }

    if (nBlockEnd < nBlockStart) {
        strError = "Proposal " + strProposalName + ": Invalid nBlockEnd (end before start)";
        return false;
    }

    if (fCheckCollateral) {
        int nConf = 0;
        if (!SignatureValid() && !IsBudgetCollateralValid(nFeeTXHash, GetHash(), strError, nTime, nConf)) {
            strError = "Proposal " + strProposalName + ": Invalid collateral";
            return false;
        }
    }

    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) {
        strError = "Proposal " + strProposalName + ": Tip is NULL";
        return true;
    }

    return true;
}

bool CBudgetProposal::IsEstablished()
{
    return nTime < GetAdjustedTime() - Params().GetProposalEstablishmentTime();
}

bool CBudgetProposal::IsPassing(const CBlockIndex* pindexPrev, int nBlockStartBudget, int nBlockEndBudget, int mnCount)
{
    if (!fValid)
        return false;

    if (!pindexPrev)
        return false;

    if (this->nBlockStart > nBlockStartBudget)
        return false;

    if (this->nBlockEnd < nBlockEndBudget)
        return false;

    if (GetYeas() - GetNays() <= mnCount / 10)
        return false;

    if (!IsEstablished())
        return false;

    return true;
}

bool CBudgetProposal::AddOrUpdateVote(CBudgetVote& vote, std::string& strError)
{
    std::string strAction = "New vote inserted:";
    LOCK(cs);

    uint256 hash = vote.vin.prevout.GetHash();

    if (mapVotes.count(hash)) {
        if (mapVotes[hash].nTime > vote.nTime) {
            strError = strprintf("new vote older than existing vote - %s\n", vote.GetHash().ToString());
            LogPrint("mnbudget", "CBudgetProposal::AddOrUpdateVote - %s\n", strError);
            return false;
        }
        if (vote.nTime - mapVotes[hash].nTime < BUDGET_VOTE_UPDATE_MIN) {
            strError = strprintf("time between votes is too soon - %s - %lli sec < %lli sec\n", vote.GetHash().ToString(), vote.nTime - mapVotes[hash].nTime,BUDGET_VOTE_UPDATE_MIN);
            LogPrint("mnbudget", "CBudgetProposal::AddOrUpdateVote - %s\n", strError);
            return false;
        }
        strAction = "Existing vote updated:";
    }

    if (vote.nTime > GetTime() + (60 * 60)) {
        strError = strprintf("new vote is too far ahead of current time - %s - nTime %lli - Max Time %lli\n", vote.GetHash().ToString(), vote.nTime, GetTime() + (60 * 60));
        LogPrint("mnbudget", "CBudgetProposal::AddOrUpdateVote - %s\n", strError);
        return false;
    }

    mapVotes[hash] = vote;
    LogPrint("mnbudget", "CBudgetProposal::AddOrUpdateVote - %s %s\n", strAction.c_str(), vote.GetHash().ToString().c_str());

    return true;
}

// If masternode voted for a proposal, but is now invalid -- remove the vote
void CBudgetProposal::CleanAndRemove(bool fSignatureCheck)
{
    std::map<uint256, CBudgetVote>::iterator it = mapVotes.begin();

    while (it != mapVotes.end()) {
        (*it).second.fValid = (*it).second.SignatureValid(fSignatureCheck);
        ++it;
    }
}

int CBudgetProposal::GetYeas() const
{
    int ret = 0;

    std::map<uint256, CBudgetVote>::const_iterator it = mapVotes.begin();
    while (it != mapVotes.end()) {
        if ((*it).second.nVote == VOTE_YES && (*it).second.fValid) {
            ret++;
        }

        ++it;
    }

    return ret;
}

int CBudgetProposal::GetNays() const
{
    int ret = 0;

    std::map<uint256, CBudgetVote>::const_iterator it = mapVotes.begin();
    while (it != mapVotes.end()) {
        if ((*it).second.nVote == VOTE_NO && (*it).second.fValid) {
            ret++;
        }
        ++it;
    }

    return ret;
}

int CBudgetProposal::GetAbstains() const
{
    int ret = 0;

    std::map<uint256, CBudgetVote>::const_iterator it = mapVotes.begin();
    while (it != mapVotes.end()) {
        if ((*it).second.nVote == VOTE_ABSTAIN && (*it).second.fValid) {
            ret++;
        }
        ++it;
    }

    return ret;
}

double CBudgetProposal::GetRatio()
{
    int yeas = GetYeas();
    int nays = GetNays();

    if (yeas + nays == 0) return 0.0f;

    return ((double)(yeas) / (double)(yeas + nays)) * 100;
}


CBudgetProposalBroadcast::CBudgetProposalBroadcast(std::string strProposalNameIn, std::string strURLIn, int nBlockStartIn, int nBlockEndIn, uint256 nFeeTXHashIn)
{
    strProposalName = strProposalNameIn;
    strURL = strURLIn;

    nBlockStart = nBlockStartIn;
    nBlockEnd = nBlockEndIn;

    // Right now single payment proposals have nBlockEnd have a cycle too early!
    // switch back if it break something else
    // calculate the end of the cycle for this vote, add half a cycle (vote will be deleted after that block)
    // nBlockEnd = nCycleStart + GetBudgetPaymentCycleBlocks() * nPaymentCount + GetBudgetPaymentCycleBlocks() / 2;

    nFeeTXHash = nFeeTXHashIn;
}

void CBudgetProposalBroadcast::Relay()
{
    CInv inv(MSG_BUDGET_PROPOSAL, GetHash());
    RelayInv(inv);
}

CBudgetVote::CBudgetVote()
{
    vin = CTxIn();
    nProposalHash = 0;
    nVote = VOTE_ABSTAIN;
    nTime = 0;
    fValid = true;
    fSynced = false;
}

CBudgetVote::CBudgetVote(CTxIn vinIn, uint256 nProposalHashIn, int nVoteIn)
{
    vin = vinIn;
    nProposalHash = nProposalHashIn;
    nVote = nVoteIn;
    nTime = GetAdjustedTime();
    fValid = true;
    fSynced = false;
}

void CBudgetVote::Relay()
{
    CInv inv(MSG_BUDGET_VOTE, GetHash());
    RelayInv(inv);
}

bool CBudgetVote::Sign(CKey& keyMasternode, CPubKey& pubKeyMasternode)
{
    std::string errorMessage = "";
    std::string strMessage;
    if(nVote == 3)
        strMessage = nProposalHash.ToString() + std::to_string(nVote) + std::to_string(nTime);
    else
        strMessage = vin.prevout.ToStringShort() + nProposalHash.ToString() + std::to_string(nVote) + std::to_string(nTime);

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig, keyMasternode)) {
        LogPrint("mnbudget","CBudgetVote::Sign - Error upon calling SignMessage");
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyMasternode, vchSig, strMessage, errorMessage)) {
        LogPrint("mnbudget","CBudgetVote::Sign - Error upon calling VerifyMessage");
        return false;
    }

    return true;
}

bool CBudgetVote::SignatureValid(bool fSignatureCheck)
{
    if(nVote == 3){
        std::string strMessage = nProposalHash.ToString() + std::to_string(nVote) + std::to_string(nTime);
        CPubKey pubkeynew(ParseHex(Params().SporkKey()));
        std::string errorMessage = "";
        if (obfuScationSigner.VerifyMessage(pubkeynew, vchSig, strMessage, errorMessage)) {
            return true;
        }

        return false;
    }

    std::string errorMessage;
    std::string strMessage = vin.prevout.ToStringShort() + nProposalHash.ToString() + std::to_string(nVote) + std::to_string(nTime);

    CMasternode* pmn = mnodeman.Find(vin);

    if (pmn == NULL) {
        if (fDebug){
            LogPrint("mnbudget","CBudgetVote::SignatureValid() - Unknown Masternode - %s\n", vin.prevout.hash.ToString());
        }
        return false;
    }else if(mnodeman.IsPillar(pmn -> vin.prevout) == 0){
        return false;
    }

    if (!fSignatureCheck) return true;

    if (!obfuScationSigner.VerifyMessage(pmn->pubKeyMasternode, vchSig, strMessage, errorMessage)) {
        LogPrint("mnbudget","CBudgetVote::SignatureValid() - Verify message failed\n");
        return false;
    }

    return true;
}

// Sort budget proposals by hash
struct sortProposalsByHash  {
    bool operator()(const CBudgetProposal* left, const CBudgetProposal* right)
    {
        return (left->GetHash() < right->GetHash());
    }
};

std::string CBudgetManager::ToString() const
{
    std::ostringstream info;

    info << "Proposals: " << (int)mapProposals.size() << (int)mapSeenMasternodeBudgetProposals.size() << ", Seen Budget Votes: " << (int)mapSeenMasternodeBudgetVotes.size();

    return info.str();
}

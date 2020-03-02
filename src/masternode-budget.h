// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2019 The PIVX developers
// Copyright (c) 2018-2019 The Zenon developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_BUDGET_H
#define MASTERNODE_BUDGET_H

#include "base58.h"
#include "init.h"
#include "key.h"
#include "main.h"
#include "masternode.h"
#include "net.h"
#include "sync.h"
#include "util.h"


extern CCriticalSection cs_budget;

class CBudgetManager;
class CBudgetProposal;
class CBudgetProposalBroadcast;

#define VOTE_ABSTAIN 0
#define VOTE_YES 1
#define VOTE_NO 2
#define VOTE_DELETE 3

enum class TrxValidationStatus {
    InValid,         /** Transaction verification failed */
    Valid,           /** Transaction successfully verified */
    DoublePayment,   /** Transaction successfully verified, but includes a double-budget-payment */
    VoteThreshold    /** If not enough masternodes have voted on a finalized budget */
};

static const CAmount PROPOSAL_FEE_TX = (5000 * COIN);

static const int64_t BUDGET_VOTE_UPDATE_MIN = 60 * 60;

extern std::vector<CBudgetProposalBroadcast> vecImmatureBudgetProposals;

extern CBudgetManager budget;
void DumpBudgets();

//Check the collateral transaction for the budget proposal budget
bool IsBudgetCollateralValid(uint256 nTxCollateralHash, uint256 nExpectedHash, std::string& strError, int64_t& nTime, int& nConf);

//
// CBudgetVote - Allow a masternode node to vote and broadcast throughout the network
//

class CBudgetVote
{
public:
    bool fValid;  //if the vote is currently valid / counted
    bool fSynced; //if we've sent this to our peers
    CTxIn vin;
    uint256 nProposalHash;
    int nVote;
    int64_t nTime;
    std::vector<unsigned char> vchSig;

    CBudgetVote();
    CBudgetVote(CTxIn vin, uint256 nProposalHash, int nVoteIn);

    bool Sign(CKey& keyMasternode, CPubKey& pubKeyMasternode);
    bool SignatureValid(bool fSignatureCheck);
    void Relay();

    std::string GetVoteString()
    {
        std::string ret = "ABSTAIN";
        if (nVote == VOTE_YES) ret = "YES";
        if (nVote == VOTE_NO) ret = "NO";
        if (nVote == VOTE_DELETE) ret = "DELETE";
        return ret;
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << nProposalHash;
        ss << nVote;
        ss << nTime;
        return ss.GetHash();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(nProposalHash);
        READWRITE(nVote);
        READWRITE(nTime);
        READWRITE(vchSig);
    }
};


/** Save Budget Manager (budget.dat)
 */
class CBudgetDB
{
private:
    boost::filesystem::path pathDB;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CBudgetDB();
    bool Write(const CBudgetManager& objToSave);
    ReadResult Read(CBudgetManager& objToLoad, bool fDryRun = false);
};


//
// Budget Manager : Contains all proposals for the budget
//
class CBudgetManager
{
private:
    //hold txes until they mature enough to use
    // XX42    std::map<uint256, CTransaction> mapCollateral;
    std::map<uint256, uint256> mapCollateralTxids;

public:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // keep track of the scanning errors I've seen
    std::map<uint256, CBudgetProposal> mapProposals;

    std::map<uint256, CBudgetProposalBroadcast> mapSeenMasternodeBudgetProposals;
    std::map<uint256, CBudgetVote> mapSeenMasternodeBudgetVotes;
    std::map<uint256, CBudgetVote> mapOrphanMasternodeBudgetVotes;

    CBudgetManager()
    {
        mapProposals.clear();
    }

    void ClearSeen()
    {
        mapSeenMasternodeBudgetProposals.clear();
        mapSeenMasternodeBudgetVotes.clear();
    }

    int sizeProposals() { return (int)mapProposals.size(); }

    void ResetSync();
    void MarkSynced();
    void Sync(CNode* node, uint256 nProp, bool fPartial = false);

    void Calculate();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void NewBlock();
    CBudgetProposal* FindProposal(const std::string& strProposalName);
    CBudgetProposal* FindProposal(uint256 nHash);
    std::pair<std::string, std::string> GetVotes(std::string strProposalName);

    CAmount GetTotalBudget(int nHeight);
    std::vector<CBudgetProposal*> GetBudget();
    std::vector<CBudgetProposal*> GetAllProposals();
    bool AddProposal(CBudgetProposal& budgetProposal);

    bool UpdateProposal(CBudgetVote& vote, CNode* pfrom, std::string& strError);
    bool PropExists(uint256 nHash);

    void CheckOrphanVotes();
    void Clear()
    {
        LOCK(cs);

        LogPrintf("Budget object cleared\n");
        mapProposals.clear();
        mapSeenMasternodeBudgetProposals.clear();
        mapSeenMasternodeBudgetVotes.clear();
        mapOrphanMasternodeBudgetVotes.clear();
    }
    void CheckAndRemove();
    std::string ToString() const;


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapSeenMasternodeBudgetProposals);
        READWRITE(mapSeenMasternodeBudgetVotes);
        READWRITE(mapOrphanMasternodeBudgetVotes);

        READWRITE(mapProposals);
    }
};

//
// Budget Proposal : Contains the masternode votes for each budget
//

class CBudgetProposal
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    CAmount nAlloted;

public:
    bool fValid;
    std::string strProposalName;

    /*
        json object with name, short-description, long-description, pdf-url and any other info
        This allows the proposal website to stay 100% decentralized
    */
    std::string strURL;
    int nBlockStart;
    int nBlockEnd;
    int64_t nTime;
    uint256 nFeeTXHash;
    std::vector<unsigned char> vchSig;

    std::map<uint256, CBudgetVote> mapVotes;
    //cache object

    CBudgetProposal();
    CBudgetProposal(const CBudgetProposal& other);
    CBudgetProposal(std::string strProposalNameIn, std::string strURLIn, int nBlockStartIn, int nBlockEndIn, uint256 nFeeTXHashIn);

    void Calculate();
    bool AddOrUpdateVote(CBudgetVote& vote, std::string& strError);
    bool HasMinimumRequiredSupport();
    std::pair<std::string, std::string> GetVotes();

    bool Sign();
    bool SignatureValid();

    bool IsValid(std::string& strError, bool fCheckCollateral = true);

    bool IsEstablished();
    bool IsPassing(const CBlockIndex* pindexPrev, int nBlockStartBudget, int nBlockEndBudget, int mnCount);

    std::string GetName() { return strProposalName; }
    std::string GetURL() { return strURL; }
    int GetBlockStart() { return nBlockStart; }
    int GetBlockEnd() { return nBlockEnd; }
    double GetRatio();
    int GetYeas() const;
    int GetNays() const;
    int GetAbstains() const;

    void CleanAndRemove(bool fSignatureCheck);

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << strProposalName;
        ss << strURL;
        ss << nBlockStart;
        ss << nBlockEnd;
        uint256 h1 = ss.GetHash();

        return h1;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        //for syncing with other clients
        READWRITE(LIMITED_STRING(strProposalName, 60));
        READWRITE(LIMITED_STRING(strURL, 250));
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(nBlockEnd);
        READWRITE(nTime);
        READWRITE(vchSig);
        READWRITE(nFeeTXHash);

        //for saving to the serialized db
        READWRITE(mapVotes);
    }
};

// Proposals are cast then sent to peers with this object, which leaves the votes out
class CBudgetProposalBroadcast : public CBudgetProposal
{
public:
    CBudgetProposalBroadcast() : CBudgetProposal() {}
    CBudgetProposalBroadcast(const CBudgetProposal& other) : CBudgetProposal(other) {}
    CBudgetProposalBroadcast(const CBudgetProposalBroadcast& other) : CBudgetProposal(other) {}
    CBudgetProposalBroadcast(std::string strProposalNameIn, std::string strURLIn, int nBlockStartIn, int nBlockEndIn, uint256 nFeeTXHashIn);

    void swap(CBudgetProposalBroadcast& first, CBudgetProposalBroadcast& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.strProposalName, second.strProposalName);
        swap(first.strURL, second.strURL);
        swap(first.nBlockStart, second.nBlockStart);
        swap(first.nBlockEnd, second.nBlockEnd);
        swap(first.nTime, second.nTime);
        swap(first.vchSig, second.vchSig);
        swap(first.nFeeTXHash, second.nFeeTXHash);

        first.mapVotes.swap(second.mapVotes);
    }

    CBudgetProposalBroadcast& operator=(CBudgetProposalBroadcast from)
    {
        swap(*this, from);
        return *this;
    }

    void Relay();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        //for syncing with other clients

        READWRITE(LIMITED_STRING(strProposalName, 60));
        READWRITE(LIMITED_STRING(strURL, 250));
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(nBlockEnd);
        READWRITE(vchSig);
        READWRITE(nFeeTXHash);
    }
};


#endif

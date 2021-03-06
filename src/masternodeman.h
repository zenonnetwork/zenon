// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2019 The Zenon developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEMAN_H
#define MASTERNODEMAN_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "masternode.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#define MASTERNODES_DUMP_SECONDS (15 * 60)

class CMasternodeMan;
extern int GetMasternodeDsegSeconds();
extern CMasternodeMan mnodeman;
void DumpMasternodes();

/** Access to the MN database (mncache.dat)
 */
class CMasternodeDB
{
private:
    boost::filesystem::path pathMN;
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

    CMasternodeDB();
    bool Write(const CMasternodeMan& mnodemanToSave);
    ReadResult Read(CMasternodeMan& mnodemanToLoad, bool fDryRun = false);
};

class hash_by_outpoint{
    public:
        size_t operator() (const COutPoint& to_hash) const {
            return (std::hash<std::string>()(to_hash.ToString()));
        }
};

class CMasternodeMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs and Pillars
    std::vector<CMasternode> vMasternodes;
    // who's asked for the Masternode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForMasternodeList;
    // who we asked for the Masternode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForMasternodeList;
    // which Masternodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForMasternodeListEntry;

    // the number of pillar utxo made in the pli stage
    unsigned int MAX_PILLARS_ALLOWED;
    // array for all pillar utxo
    std::vector<std::pair<COutPoint, std::pair<int, int> > > vPillarCollaterals;
    // map for all pillar utxo
    std::unordered_map<COutPoint, std::pair<int, int>, hash_by_outpoint> mPillarCollaterals;
    // checkpoint for pillar utxo scan
    unsigned int last_block_scanned;
    // hold the last 10 ratios for node voting
    std::vector<double> vLastRatios;
public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, CMasternodeBroadcast> mapSeenMasternodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CMasternodePing> mapSeenMasternodePing;

    // keep track of dsq count to prevent masternodes from gaming obfuscation queue
    int64_t nDsqCount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);
        READWRITE(vMasternodes);
        READWRITE(mAskedUsForMasternodeList);
        READWRITE(mWeAskedForMasternodeList);
        READWRITE(mWeAskedForMasternodeListEntry);
        READWRITE(nDsqCount);

        READWRITE(mapSeenMasternodeBroadcast);
        READWRITE(mapSeenMasternodePing);

        READWRITE(MAX_PILLARS_ALLOWED);
        READWRITE(vPillarCollaterals);
        READWRITE(last_block_scanned);
    }

    CMasternodeMan();
    CMasternodeMan(CMasternodeMan& other);

    /// Add an entry
    bool Add(CMasternode& mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode* pnode, CTxIn& vin);

    /// Check all Masternodes
    void Check();

    /// Check all Masternodes and remove inactive
    void CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear Masternode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    void CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CMasternode* Find(const CScript& payee);
    CMasternode* Find(const CTxIn& vin);
    CMasternode* Find(const CPubKey& pubKeyMasternode);

    /// Find an entry in the masternode list that is next to be paid
    CMasternode* GetNextMasternodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CMasternode* FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion = -1);

    /// Get the current winner for this block
    CMasternode* GetCurrentMasterNode(int mod = 1, int64_t nBlockHeight = 0, int minProtocol = 0);

    std::vector<CMasternode> GetFullMasternodeVector()
    {
        Check();
        return vMasternodes;
    }

    std::vector<std::pair<int, CMasternode> > GetMasternodeRanks(int64_t nBlockHeight, int minProtocol = 0);
    int GetMasternodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);
    CMasternode* GetMasternodeByRank(int nRank, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);

    void ProcessMasternodeConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    void UpdateLastScannedBlock(int nHeight){
        last_block_scanned = nHeight;
    }

    // return the number of active pillars
    int PillarCount(int protocolVersion = -1);
    
    int PillarSlots(){
        return vPillarCollaterals.size() < MAX_PILLARS_ALLOWED ? MAX_PILLARS_ALLOWED - vPillarCollaterals.size() : 0;
    }
    
    int PillarQueueSize(){
        return vPillarCollaterals.size() > MAX_PILLARS_ALLOWED ? vPillarCollaterals.size() - MAX_PILLARS_ALLOWED : 0;
    }

    std::vector<std::pair<int, std::string> > PillarQueuePositions();

    bool IsPillar(const COutPoint& utxo){
        return mPillarCollaterals.count(utxo) > 0;
    }

    bool CanBePillar(const COutPoint& utxo);

    // bootstrap for pillar utxos
    bool InitPillars();

    void InitRatios();

    void IncrementMaxPillarsAllowed(){
        MAX_PILLARS_ALLOWED++;
    }

    void DecrementMaxPillarsAllowed(){
        MAX_PILLARS_ALLOWED--;
    }

    void AddPillarUtxo(const COutPoint& first, std::pair<int, int> second);

    void DeletePillarUtxo(const COutPoint& outpoint);

    /// Return the number of (unique) Masternodes and Pillars
    int size() {
        return (int)vMasternodes.size();
    }

    /// Return the number of Masternodes older than (default) 8000 seconds
    int stable_size ();

    std::string ToString() const;

    void Remove(CTxIn vin);

    int GetEstimatedMasternodes(int nBlock);

    /// Update masternode list and maps using provided CMasternodeBroadcast
    void UpdateMasternodeList(CMasternodeBroadcast mnb);
};


#endif

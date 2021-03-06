// Copyright 2009-2015 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2015, Sandia Corporation
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef VAULT_H
#define VAULT_H

#include <cstring>
#include <string>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <set>
#include <vector>
#include <queue>
#include <list>
#include <sstream>
#include <fstream>
#include <boost/algorithm/string.hpp>

#include <sst/core/subcomponent.h>
#include <sst/core/output.h>
#include <sst/core/params.h>

#include "globals.h"
#include "transaction.h"
#include "callback_hmc.h"

#include <DRAMSim.h>
#include <AddressMapping.h>

using namespace std;
using namespace SST;

#define ON_FLY_HMC_OP_OPTIMUM_SIZE 10
#define TRANS_Q_OPTIMUM_SIZE 10

//#define MAX_BANK_SIZE                   // FIXME: used for mapping (rank,bank) pair to a single number

class Vault : public SubComponent {
private:
    typedef CallbackBase<void, uint64_t, uint64_t, uint64_t> callback_t;
    typedef unordered_map<uint64_t, transaction_c> id2TransactionMap_t;
    typedef unordered_map<unsigned, bool> bank2BoolMap_t;
    typedef unordered_map<unsigned, uint64_t> bank2CycleMap_t;
    typedef unordered_map<unsigned, uint64_t> bank2IdMap_t;
    typedef vector<transaction_c> transQ_t; // FIXME: use more efficient container
    typedef queue<transaction_c> transFIFO_t;

public:
    /**
     * Constructor
     */
    Vault();

    /**
     * Constructor
     * @param id Vault id
     * @param dbg VaultSimC() class wrapper sst debuger
     */
    Vault(Component *comp, Params &params);

    /**
     * finish
     * Callback function that gets to be called when simulation finishes
     */
    void finish();

    /**
     * update
     * Vaultsim handle to update DRAMSIM, it also increases the cycle
     * main function called from VaultSimC
     */
    void update();

    /**
     * registerCallback
     */
    void registerCallback(callback_t *rc, callback_t *wc) { readCallback = rc; writeCallback = wc; }

    /**
     * addTransaction
     * @param transaction
     * Adds a transaction to the transaction queue, with some local initialization for transaction
     */
    bool addTransaction(transaction_c transaction);

    /**
     * readComplete
     * DRAMSim calls this function when it is done with a read
     */
    void readComplete(unsigned idSys, uint64_t addr, uint64_t idTrans, uint64_t cycle);

    /**
     * writeComplete
     * DRAMSim calls this function when it is done with a write
     */
    void writeComplete(unsigned idSys, uint64_t addr, uint64_t idTrans, uint64_t cycle);

    /**
     * getId
     */
    unsigned getId() { return id; }

private:
    /**
     * updateQueue
     * update transaction queue and issue read/write to DRAM
     */
    void updateQueue();

    /**
     * updateComputePhase
     * will take care of bank locking and functional units number limit
     * Note: HmcFunctionalUnit_Num is per vault
     */
    void updateComputePhase();

    /**
     * issueAtomicPhases
     */
    void issueAtomicFirstMemoryPhase(id2TransactionMap_t::iterator mi);
    void issueAtomicSecondMemoryPhase(id2TransactionMap_t::iterator mi);
    void skipAtomicSecondMemoryPhase(id2TransactionMap_t::iterator mi);
    void initiateAtomicComputePhase(id2TransactionMap_t::iterator mi);
    void issueAtomicComputePhase(id2TransactionMap_t::iterator mi);


    /**
     * Bank BusyMap Functions
     */
    inline bool getBankState(unsigned bankId) { return bankBusyMap[bankId]; }
    inline void unlockBank(unsigned bankId) { bankBusyMap[bankId] = false; }
    inline void lockBank(unsigned bankId) { bankBusyMap[bankId] = true; }
    inline void unlockAllBanks() {
        for (unsigned i = 0; i < BANK_SIZE_OPTIMUM; i++) {
            bankBusyMap[i] = false;
        }
    }

    /**
     * Compute Phase Functions
     */
    inline void setComputeDoneCycle(unsigned bankId, uint64_t cycle) { computeDoneCycleMap[bankId] = cycle; }
    inline uint64_t getComputeDoneCycle(unsigned bankId) { return computeDoneCycleMap[bankId]; }
    inline void eraseComputeDoneCycle(unsigned bankId) { computeDoneCycleMap.erase(bankId); }

    inline void setIdCompute(unsigned bankId, uint64_t id) { idComputeMap[bankId] = id; }
    inline uint64_t getIdCompute(unsigned bankId) { return idComputeMap[bankId]; }
    inline void eraseIdCompute(unsigned bankId) { idComputeMap.erase(bankId); }

    /**
     *  Stats
     */
    // Helper function for printing statistics in MacSim format
    template<typename T>
    void writeTo(ofstream &ofs, string suffix, string name, T count)
    {
        #define FILED1_LENGTH 45
        #define FILED2_LENGTH 20
        #define FILED3_LENGTH 30

        ofs.setf(ios::left, ios::adjustfield);
        string capitalized_suffixed_name = boost::to_upper_copy(name + "_" + suffix);
        ofs << setw(FILED1_LENGTH) << capitalized_suffixed_name;

        ofs.setf(ios::right, ios::adjustfield);
        ofs << setw(FILED2_LENGTH) << count << setw(FILED3_LENGTH) << count << endl << endl;
    }

    void printStatsForMacSim();

public:
    int id;
    uint64_t currentClockCycle;

    callback_t *readCallback;
    callback_t *writeCallback;



private:
    DRAMSim::MultiChannelMemorySystem *memorySystem;
    int numDramBanksPerRank;

    //Debugs
    Output dbg;                                  // VaulSimC wrapper dbg, for printing debuging commands
    Output out;                                  // VaulSimC wrapper output, for printing always printed info and stats
    Output dbgOnFlyHmcOps;                       // For debugging long lasting hmc_ops in queue
    bool dbgOnFlyHmcOpsIsOn;                      // For debuggung late onFlyHMC ops (bool variable)
    int dbgOnFlyHmcOpsThresh;                    // For debuggung late onFlyHMC ops (threshhold Value)

    //Stat Format
    int statsFormat;                             // Type of Stat output 0:Defualt 1:Macsim (Default Value is set to 0)

    transQ_t transQ;                             // Transaction Queue
    id2TransactionMap_t onFlyHmcOps;             // Currently issued atomic ops

    bank2BoolMap_t bankBusyMap;                  // Current Busy Banks
    list<unsigned> computePhaseEnabledBanks;     // Current Compute Phase Insturctions (same size as bankBusyMap)
    bank2CycleMap_t computeDoneCycleMap;         // Current Compute Done Cycle ((same size as bankBusyMap)
    bank2IdMap_t idComputeMap;

    id2TransactionMap_t onFlyComputeHmcOps;      // Currently issued atomic ops in compute phase
    transFIFO_t waitListComputeHmcOps;           // Waitlist compute phase

    // Limits
    int HMCOpsIssueLimitPerWindow;
    int HMCOpsIssueLimitWindowSize;
    int currentHMCOpsIssueBudget;
    int currentHMCOpsIssueLimitWindowNum;

    int DRAMSimUpdatePerWindow;
    int DRAMSimUpdateWindowSize;
    int currentDRAMSimUpdateBudget;
    int currentDRAMSimUpdateWindowNum;

    int HmcFunctionalUnitNum;

    // Atomic RMW - Write Enable
    bool HMCAtomicSendWrToMemEn;
    int HMCCostWr;

    // HMC ops Cost in Cycles
    int HMCCostLogicalOps;
    int HMCCostCASOps;
    int HMCCostCompOps;
    int HMCCostAdd8;
    int HMCCostAdd16;
    int HMCCostAddDual;
    int HMCCostFPAdd;
    int HMCCostSwap;
    int HMCCostBitW;

    // stats
    Statistic<uint64_t>* statTotalTransactions;
    Statistic<uint64_t>* statTotalHmcOps;
    Statistic<uint64_t>* statTotalNonHmcOps;
    Statistic<uint64_t>* statTotalHmcCandidate;
    Statistic<uint64_t>* statTotalHmcConfilictHappened;

    Statistic<uint64_t>* statCyclesFUFullForHMCIssue;

    Statistic<uint64_t>* statTotalNonHmcRead;
    Statistic<uint64_t>* statTotalNonHmcWrite;

    Statistic<uint64_t>* statTotalHmcLatency;
    Statistic<uint64_t>* statIssueHmcLatency;
    Statistic<uint64_t>* statReadHmcLatency;
    Statistic<uint64_t>* statWriteHmcLatency;

    /* internal stats */
    // HMC Latency
    uint64_t statTotalHmcLatencyInt;   //statapi does not provide any non-collection type addData (ORno documentation)
    uint64_t statIssueHmcLatencyInt;
    uint64_t statReadHmcLatencyInt;
    uint64_t statWriteHmcLatencyInt;

};
#endif

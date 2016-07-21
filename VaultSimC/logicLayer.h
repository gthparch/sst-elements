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


#ifndef _LOGICLAYER_H
#define _LOGICLAYER_H

#include <sst/core/event.h>
#include <sst/core/introspectedComponent.h>
#include <sst/core/output.h>
#include <sst/core/statapi/stataccumulator.h>
#include <sst/core/statapi/stathistogram.h>
#include <sst/elements/memHierarchy/memEvent.h>

#include <set>
#include <queue>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <fstream>
#include <boost/algorithm/string.hpp>

#include <DRAMSim.h>
#include <AddressMapping.h>

#include "globals.h"
#include "transaction.h"

using namespace std;
using namespace SST;

class logicLayer : public IntrospectedComponent {
private:
    typedef SST::Link memChan_t;
    typedef vector<memChan_t*> memChans_t;

    #ifdef USE_VAULTSIM_HMC
    typedef unordered_map<uint64_t, vector<MemHierarchy::MemEvent*> > tIdQueue_t;
    #endif

public:
    /**
     * Constructor
     */
    logicLayer(ComponentId_t id, Params& params);

    /**
     * finish
     * Callback function that gets to be called when simulation finishes
     */
    void finish();

private:
    /**
     * Constructor
     */
    logicLayer(const logicLayer& c);

    /**
     * Step call for LogicLayer
     */
    bool clock(Cycle_t);

    // Determine if we 'own' a given address
    inline bool isOurs(unsigned int addr);

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

private:
    bool haveQuad;
    int numVaultPerQuad;
    int numVaultPerQuad2;
    int numDramBanksPerRank;
    int numVaults;
    int numVaults2;

    // BW control
    int reqLimitPerWindow;
    int reqLimitWindowSize;
    int currentLimitReqBudgetCPU[2];    // {recv, send}
    int currentLimitReqBudgetMem[2];    // {recv, send}
    int currentLimitWindowNum;

    unsigned int llID;

    // SST Links
    int numOfOutBus;
    memChans_t outChans;                 // SST links to each Quad/Vault
    SST::Link *toMem;
    SST::Link *toCPU;
    memChans_t toXBar;

    // Mapping
    uint64_t sendAddressMask;
    int sendAddressShift;
    uint64_t quadIDAddressMask;
    int quadIDAddressShift;

    //Quad support
    unsigned int currentSendID;

    // cacheLineSize (For Main Memroy accesses)
    uint64_t CacheLineSize;             // it is used to determine VaultIDs
    unsigned CacheLineSizeLog2;         // bits of CacheLineSize

    // cacheSim Vars
    bool cacheSim;

    // Multi logicLayer support (FIXME)
    unsigned int LL_MASK;

    // Transaction Support
    // #ifdef USE_VAULTSIM_HMC
    // tIdQueue_t tIdQueue;
    // unordered_map<uint64_t, uint64_t> transSize;
    // queue<uint64_t> transReadyQueue;
    // queue<uint64_t> transRetireQueue;
    // set<uint64_t> transConflictQueue;
    // unsigned activeTransactionsLimit;       //FIXME: Not used now
    // tIdQueue_t tIdReadyForRetire;
    // unordered_set<uint64_t> activeTransactions;
    // #endif

    // Statistics
    Statistic<uint64_t>* memOpsProcessed;
    Statistic<uint64_t>* HMCCandidateProcessed;
    Statistic<uint64_t>* HMCOpsProcessed;
    Statistic<uint64_t>* HMCTransOpsProcessed;

    Statistic<uint64_t>* reqUsedToCpu[2];
    Statistic<uint64_t>* reqUsedToMem[2];

    // Output
    Output dbg;                 // Output, for printing debuging commands
    Output out;                 // Output, for printing always printed info and stats
    int statsFormat;            // Type of Stat output 0:Defualt 1:Macsim (Default Value is set to 0)
};

#endif

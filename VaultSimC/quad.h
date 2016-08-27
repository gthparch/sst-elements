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


#ifndef _QUAD_H
#define _QUAD_H

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

class quad : public IntrospectedComponent {
private:
    typedef SST::Link memChan_t;
    typedef vector<memChan_t*> memChans_t;

public:
    /**
     * Constructor
     */
    quad(ComponentId_t id, Params& params);

    /**
     * finish
     * Callback function that gets to be called when simulation finishes
     */
    void finish();

private:
    /**
     * Step call for quad
     */
    bool clock(Cycle_t);

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
    unsigned int quadID;
    int numVaultPerQuad;
    int numVaultPerQuad2;
    int numTotalVaults;
    int numTotalVaults2;

    // Mapping
    uint64_t sendAddressMask;
    int sendAddressShift;
    uint64_t quadIDAddressMask;
    int quadIDAddressShift;

    // SST Links
    memChans_t outChans;
    SST::Link *toLogicLayer;
    SST::Link *toXBar;

    // cacheLineSize
    uint64_t CacheLineSize;             // it is used to determine VaultIDs
    unsigned CacheLineSizeLog2;         // bits of CacheLineSize

    // stats
    Statistic<uint64_t>* statTotalTransactionsRecv;
    Statistic<uint64_t>* statTransactionsSentToXbar;
    Statistic<uint64_t>* statTotalTransactionsRecvFromVaults;

    // Output
    Output dbg;                 // Output, for printing debuging commands
    Output out;                 // Output, for printing always printed info and stats
    int statsFormat;            // Type of Stat output 0:Defualt 1:Macsim (Default Value is set to 0)
};

#endif

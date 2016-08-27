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


#ifndef _VAULTSIMC_H
#define _VAULTSIMC_H

#include <sst/core/event.h>
#include <sst/core/introspectedComponent.h>
#include <sst/core/output.h>

#include <sst/elements/memHierarchy/memEvent.h>

#include "globals.h"
#include "transaction.h"
#include "Vault.h"

using namespace std;
using namespace SST;

class VaultSimC : public IntrospectedComponent {
private:
    typedef SST::Link memChan_t;
    typedef unordered_map<uint64_t, MemHierarchy::MemEvent*> t2MEMap_t;      // Why multimap? a single address could be associatet to multiple events

public:
    /**
     * Constructor
     */
    VaultSimC(ComponentId_t id, Params& params);

    /**
     * finish
     * Callback function that gets to be called when simulation finishes
     */
    void finish();


private:
    /**
     * Constructor
     */
    VaultSimC(const VaultSimC& c);

    /**
     * Step call for VaultSimC
     */
    bool clock(Cycle_t currentCycle);

    /**
     * readData
     * Vault calls this function when it is done with a read
     */
    void readData(uint64_t id, uint64_t addr, uint64_t clockcycle);

    /**
     * writeData
     * Vault calls this function when it is done with a write
     */
    void writeData(uint64_t id, uint64_t addr, uint64_t clockcycle);

    /**
     *
     */

private:
    deque<transaction_c> transQ;
    t2MEMap_t transactionToMemEventMap; // maps original MemEvent to a Vault transaction ID

    Vault *memorySystem;
    uint64_t CacheLineSize;             // it is used to send stripped address to DRAMSim2
    int numBitShiftAddressDRAM;

    uint8_t *memBuffer;
    memChan_t *memChan;
    int numOutstanding; //number of mem requests outstanding (non-phx)
    unsigned vaultID;

    // Statistics

    // Output
    Output dbg;
    Output out;
    int statsFormat;            // Type of Stat output 0:Defualt 1:Macsim (Default Value is set to 0)
};


#endif

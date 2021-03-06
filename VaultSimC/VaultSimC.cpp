// Copyright 2009-2015 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2015 Sandia Corporation
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sys/mman.h>

#include <sst_config.h>
#include <sst/core/serialization.h>
#include <sst/core/link.h>
#include <sst/core/params.h>

#include "VaultSimC.h"

static size_t MEMSIZE = size_t(4096)*size_t(1024*1024);

using namespace SST::MemHierarchy;

VaultSimC::VaultSimC(ComponentId_t id, Params& params) : IntrospectedComponent( id ), numOutstanding(0)
{
    out.init("", 0, 0, Output::STDOUT);

    int debugLevel = params.find<int>("debug_level", 0);
    if (debugLevel < 0 || debugLevel > 10)
        dbg.fatal(CALL_INFO, -1, "Debugging level must be between 0 and 10. \n");
    dbg.init("@R:VaultSim::@p():@l " + getName() + ": ", debugLevel, 0, (Output::output_location_t)(int)params.find<bool>("debug", 0));

    statsFormat = params.find<int>("statistics_format", 0);

    std::string frequency = "1.0 GHz";
    frequency = params.find<string>("clock", "1.0 Ghz");

    memChan = configureLink("bus", "1ps");     //link delay is configurable by python scripts

    int vid = params.find<int>("vault.id", -1);
    if (-1 == vid)
        dbg.fatal(CALL_INFO, -1, "vault.id not set\n");
    vaultID = vid;

    registerClock(frequency, new Clock::Handler<VaultSimC>(this, &VaultSimC::clock));

    Params vaultParams = params.find_prefix_params("vault.");
    memorySystem = dynamic_cast<Vault*>(loadSubComponent("VaultSimC.Vault", this, vaultParams));
    if (!memorySystem)
        dbg.fatal(CALL_INFO, -1, "Unable to load Vault as sub-component\n");

    CallbackBase<void, uint64_t, uint64_t, uint64_t> *readDataCB =
        new Callback<VaultSimC, void, uint64_t, uint64_t, uint64_t>(this, &VaultSimC::readData);
    CallbackBase<void, uint64_t, uint64_t, uint64_t> *writeDataCB =
        new Callback<VaultSimC, void, uint64_t, uint64_t, uint64_t>(this, &VaultSimC::writeData);

    memorySystem->registerCallback(readDataCB, writeDataCB);
    dbg.output(CALL_INFO, "VaultSimC %u: made vault %u\n", vaultID, vaultID);

    CacheLineSize = params.find<uint64_t>("cacheLineSize", 64);

    // Address sent to DRAMSim
    numBitShiftAddressDRAM = params.find<int>("num_bit_shift_address_dram", 0);
    dbg.debug(_WARNING_, "*VaultSim%u: Number of bits shift for address that is sent to DRAMSim is %d. "\
        "Consider vaultID/quadID bit locations\n", vaultID, numBitShiftAddressDRAM);
}

void VaultSimC::finish()
{
    dbg.debug(_INFO_, "VaultSim %d finished\n", vaultID);
    memorySystem->finish();
}

void VaultSimC::readData(uint64_t id, uint64_t addr, uint64_t clockcycle)
{
    t2MEMap_t::iterator mi = transactionToMemEventMap.find(id);
    if (mi == transactionToMemEventMap.end()) {
        dbg.fatal(CALL_INFO, -1, "Vault %d can't find transaction %p (id:%" PRIu64 ")\n", vaultID, (void*)addr, id);
    }

    MemEvent *parentEvent = mi->second;
    MemEvent *event = parentEvent->makeResponse();

    memChan->send(event);
    dbg.debug(_L6_, "VaultSimC %d: read req %p (id:%" PRIu64 ") answered @%lu\n", vaultID, (void*)addr, id, clockcycle);

    // delete old event
    delete parentEvent;

    transactionToMemEventMap.erase(mi);
}

void VaultSimC::writeData(uint64_t id, uint64_t addr, uint64_t clockcycle)
{
    t2MEMap_t::iterator mi = transactionToMemEventMap.find(id);
    if (mi == transactionToMemEventMap.end()) {
        dbg.fatal(CALL_INFO, -1, "Vault %d can't find transaction %p (id:%" PRIu64 ")\n", vaultID,(void*)addr, id);
    }

    MemEvent *parentEvent = mi->second;
    MemEvent *event = parentEvent->makeResponse();

    // send event
    memChan->send(event);
    dbg.debug(_L6_, "VaultSimC %d: write req %p (%" PRIu64 ") answered @%lu\n", vaultID, (void*)addr, id, clockcycle);

    // delete old event
    delete parentEvent;

    transactionToMemEventMap.erase(mi);
}

bool VaultSimC::clock(Cycle_t currentCycle)
{
    memorySystem->update();

    SST::Event *ev = NULL;
    while ((ev = memChan->recv())) {
        // process incoming events
        MemEvent *event  = dynamic_cast<MemEvent*>(ev);
        if (NULL == event) {
            dbg.fatal(CALL_INFO, -1, "Vault %d got bad event\n", vaultID);
        }

        // new address for omitting quadID and vaultID bits / also setting lower bits zero to remove DRAMSim Warnings - anyway these bits is never used for mapping inside DRAMSim
        uint64_t new_addr = (event->getAddr() >> numBitShiftAddressDRAM) & ~((uint64_t)CacheLineSize-1);
        uint64_t new_id = event->getID().first;

        dbg.debug(_L6_, "VaultSimC %d: got a req %p (internal Addr: %p) id:%" PRIu64 " @%lu\n", vaultID, (void*)event->getAddr(), (void*)new_addr, (void*)new_id, currentCycle);

        //TransactionType transType = convertType( event->getCmd() );
        //dbg.output(CALL_INFO, "transType=%d addr=%p\n", transType, (void*)event->getAddr());

        // save the memEvent eventID based on addr so we can respond to it correctly
        transactionToMemEventMap.insert(pair<uint64_t, MemHierarchy::MemEvent*>(new_id, event));

        bool isWrite = false;
        switch(event->getCmd()) {
        case SST::MemHierarchy::GetS:
            isWrite = false;
            break;
        case SST::MemHierarchy::GetSEx:
        case SST::MemHierarchy::GetX:
            isWrite = true;
            break;
        default:
            // _abort(VaultSimC,"Tried to convert unknown memEvent request type (%d) to PHXSim transaction type \n", event->getCmd());
            break;
        }

        // add to the Q
        transaction_c transaction (isWrite, new_addr & ~((uint64_t)CacheLineSize-1), new_id);

        #ifdef USE_VAULTSIM_HMC
        uint32_t HMCTypeEvent = (event->getMemFlags()) & 0x00ff; //only lower 8bits are used for hmc flags
        transaction.setHmcOpType(HMCTypeEvent);
        if (HMCTypeEvent == HMC_NONE || HMCTypeEvent == HMC_CANDIDATE) {
            transaction.resetAtomic();
            dbg.debug(_L7_, "VaultSimC %d got a transaction for %p (id:%" PRIu64 ") of type %s @%lu\n",
                    vaultID, (void *)transaction.getAddr(), transaction.getId() ,transaction.getHmcOpTypeStr(), currentCycle);
        }
        else {
            transaction.setAtomic();
            transaction.setIsWrite();   //all hmc ops treat as write
            dbg.debug(_L7_, "VaultSimC %d got an atomic req for %p (id:%" PRIu64 ") of type %s @%lu\n",
                    vaultID, (void *)transaction.getAddr(), transaction.getId(), transaction.getHmcOpTypeStr(), currentCycle);
        }
        #else
        transaction.resetAtomic();
        dbg.debug(_L7_, "VaultSimC %d got a transaction for %p (id:%" PRIu64 ") @%lu\n",
                vaultID, (void*)transaction.getAddr(), transaction.getId(), currentCycle);
        #endif

        transQ.push_back(transaction);
    }

    bool ret = true;
    while (!transQ.empty() && ret) {
        // send events off for processing
        transaction_c transaction = transQ.front();
        if ((ret = memorySystem->addTransaction(transaction))) {
            dbg.debug(_L7_, "VaultSimC %d AddTransaction %s succeeded %p (id:%" PRIu64 ") @%lu\n",
                    vaultID, transaction.getIsWrite() ? "write" : "read", (void *)transaction.getAddr(), transaction.getId(), currentCycle);
            transQ.pop_front();
        } else {
            dbg.debug(_L7_, "VaultSimC %d AddTransaction %s  failed %p (id:%" PRIu64 ") @%lu\n",
                    vaultID, transaction.getIsWrite() ? "write" : "read", (void *)transaction.getAddr(), transaction.getId(), currentCycle);
            ret = false;
        }
    }

    return false;
}


/*
 * libVaultSimGen Functions
 */

extern "C" VaultSimC* create_VaultSimC(SST::ComponentId_t id,  SST::Params& params)
{
    return new VaultSimC(id, params);
}

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

#include <sst_config.h>
#include <sst/core/serialization.h>
#include <sst/core/interfaces/stringEvent.h>
#include <sst/core/link.h>
#include <sst/core/params.h>

#include "logicLayer.h"

using namespace SST::Interfaces;
using namespace SST::MemHierarchy;

logicLayer::logicLayer(ComponentId_t id, Params& params) : IntrospectedComponent( id )
{
    // Debug and Output Initialization
    out.init("", 0, 0, Output::STDOUT);

    int debugLevel = params.find_integer("debug_level", 0);
    dbg.init("@R:LogicLayer::@p():@l " + getName() + ": ", debugLevel, 0, (Output::output_location_t)params.find_integer("debug", 0));
    if(debugLevel < 0 || debugLevel > 10)
        dbg.fatal(CALL_INFO, -1, "Debugging level must be between 0 and 10. \n");

    // logicLayer Params Initialization
    int ident = params.find_integer("llID", -1);
    if (-1 == ident)
        dbg.fatal(CALL_INFO, -1, "llID not defined\n");
    llID = ident;

    // request limit
    reqLimitPerWindow = params.find_integer("req_LimitPerWindow", -1);
    if (0 >= reqLimitPerWindow)
        dbg.fatal(CALL_INFO, -1, " req_LimitPerWindow not defined well\n");

    reqLimitWindowSize = params.find_integer("req_LimitWindowSize", 1);
    if (0 >= reqLimitWindowSize)
        dbg.fatal(CALL_INFO, -1, " req_LimitWindowSize not defined well\n");

    int test = params.find_integer("req_LimitPerCycle", -1);
    if (test != -1)
        dbg.fatal(CALL_INFO, -1, "req_LimitPerCycle ***DEPRECATED** kept for compatibility: use req_LimitPerWindow & req_LimitWindowSize in your configuration\n");

    dbg.debug(_INFO_, "req_LimitPerWindow %d, req_LimitWindowSize: %d\n", reqLimitPerWindow, reqLimitWindowSize);
    currentLimitReqBudgetCPU[0] = reqLimitPerWindow;
    currentLimitReqBudgetCPU[1] = reqLimitPerWindow;
    currentLimitReqBudgetMem[0] = reqLimitPerWindow;
    currentLimitReqBudgetMem[1] = reqLimitPerWindow;
    currentLimitWindowNum = reqLimitWindowSize;

    //
    int mask = params.find_integer("LL_MASK", -1);
    if (-1 == mask)
        dbg.fatal(CALL_INFO, -1, " LL_MASK not defined\n");
    LL_MASK = mask;

    haveQuad = params.find_integer("have_quad", 0);

    numVaultPerQuad = params.find_integer("num_vault_per_quad", 4);
    numVaultPerQuad2 = log(numVaultPerQuad) / log(2);

    bool terminal = params.find_integer("terminal", 0);

    CacheLineSize = params.find_integer("cacheLineSize", 64);
    CacheLineSizeLog2 = log(CacheLineSize) / log(2);

    numVaults = params.find_integer("vaults", -1);
    numVaults2 = log(numVaults) / log(2);
    if (-1 == numVaults)
        dbg.fatal(CALL_INFO, -1, "numVaults not defined\n");

    // Mapping
    numOfOutBus = numVaults;
    sendAddressMask = (1LL << numVaults2) - 1;
    sendAddressShift = CacheLineSizeLog2;
    if (haveQuad) {
        numOfOutBus = numVaults/numVaultPerQuad;
        unsigned bitsForQuadID = log2(unsigned(numVaults/numVaultPerQuad));
        quadIDAddressMask = (1LL << bitsForQuadID ) - 1;
        quadIDAddressShift = CacheLineSizeLog2 + numVaultPerQuad2;
        currentSendID = 0;
    }


    // ** -----LINKS START-----**//
    // VaultSims Initializations (Links)
    for (int i = 0; i < numOfOutBus; ++i) {
        char bus_name[50];
        snprintf(bus_name, 50, "bus_%d", i);
        memChan_t *chan = configureLink(bus_name);      //link delay is configurable by python scripts
        if (chan) {
            outChans.push_back(chan);
            dbg.debug(_INFO_, "\tConnected %s\n", bus_name);
        }
        else
            dbg.fatal(CALL_INFO, -1, " could not find %s\n", bus_name);
    }

    // link to Xbar
    if (haveQuad)
        for (int i = 0; i < numOfOutBus; ++i) {
            char bus_name[50];
            snprintf(bus_name, 50, "toXBar_%d", i);
            memChan_t *chan = configureLink(bus_name);      //link delay is configurable by python scripts
            if (chan) {
                toXBar.push_back(chan);
                dbg.debug(_INFO_, "\tConnected %s\n", bus_name);
            }
            else
                dbg.fatal(CALL_INFO, -1, " could not find %s\n", bus_name);
        }

    // Connect Chain (cpu and other LL links (FIXME:multiple logiclayer support)
    toCPU = configureLink("toCPU");
    if (!toCPU)
        dbg.fatal(CALL_INFO, -1, " could not find toCPU\n");

    if (terminal)
        toMem = NULL;
    else
        toMem = configureLink("toMem");
    // ** -----LINKS END----- **//

    // Output to user
    if (haveQuad)
        out.output("*LogicLayer%d: Connected %d Quads\n", llID, numVaults/numVaultPerQuad);
    else
        out.output("*LogicLayer%d: Connected %d Vaults\n", llID, numVaults);


    #ifdef USE_VAULTSIM_HMC
    out.output("*LogicLayer%d: Flag USE_VAULTSIM_HMC set\n", llID);
    #endif

    dbg.debug(_INFO_, "Made LogicLayer %d toMem:%p toCPU:%p\n", llID, toMem, toCPU);

    // clock
    std::string frequency;
    frequency = params.find_string("clock", "2.0 Ghz");
    registerClock(frequency, new Clock::Handler<logicLayer>(this, &logicLayer::clock));
    dbg.debug(_INFO_, "Making LogicLayer with id=%d & clock=%s\n", llID, frequency.c_str());

    // Stats Initialization
    statsFormat = params.find_integer("statistics_format", 0);

    memOpsProcessed = registerStatistic<uint64_t>("Total_memory_ops_processed", "0");
    HMCOpsProcessed = registerStatistic<uint64_t>("HMC_ops_processed", "0");
    HMCCandidateProcessed = registerStatistic<uint64_t>("Total_HMC_candidate_processed", "0");
    HMCTransOpsProcessed = registerStatistic<uint64_t>("Total_HMC_transactions_processed", "0");

    reqUsedToCpu[0] = registerStatistic<uint64_t>("Req_recv_from_CPU", "0");
    reqUsedToCpu[1] = registerStatistic<uint64_t>("Req_send_to_CPU", "0");
    reqUsedToMem[0] = registerStatistic<uint64_t>("Req_recv_from_Mem", "0");
    reqUsedToMem[1] = registerStatistic<uint64_t>("Req_send_to_Mem", "0");
}

void logicLayer::finish()
{
    dbg.debug(_INFO_, "Logic Layer %d completed %lu ops\n", llID, memOpsProcessed->getCollectionCount());
    //Print Statistics
    if (statsFormat == 1)
        printStatsForMacSim();
}

bool logicLayer::clock(Cycle_t currentCycle)
{
    SST::Event* ev = NULL;


    // 1-c)
    /* Check For Events From CPU
     *     Check ownership, if owned send to internal vaults, if not send to another LogicLayer
     **/
    while ( currentLimitReqBudgetCPU[0] && (ev = toCPU->recv()) ) {
        MemEvent *event  = dynamic_cast<MemEvent*>(ev);
        if (NULL == event)
            dbg.fatal(CALL_INFO, -1, "LogicLayer%d got bad event\n", llID);
        dbg.debug(_L4_, "LogicLayer%d got req for %p (%" PRIu64 " %d)\n", llID, (void*)event->getAddr(), event->getID().first, event->getID().second);

        // HMC Type verifications and stats
        #ifdef USE_VAULTSIM_HMC
        uint8_t HMCTypeEvent = event->getHMCInstType();
        if (HMCTypeEvent >= NUM_HMC_TYPES)
            dbg.fatal(CALL_INFO, -1, "LogicLayer%d got bad HMC type %d for address %p\n", llID, event->getHMCInstType(), (void*)event->getAddr());
        if (HMCTypeEvent == HMC_CANDIDATE)
            HMCCandidateProcessed->addData(1);
        else if (HMCTypeEvent != HMC_NONE && HMCTypeEvent != HMC_CANDIDATE)
            HMCOpsProcessed->addData(1);
        #endif

        currentLimitReqBudgetCPU[0]--;
        reqUsedToCpu[0]->addData(1);

        // (Multi LogicLayer) Check if it is for this LogicLayer
        if (isOurs(event->getAddr())) {
            if (haveQuad) {
                // for quad, send it sequentially, without checking address (pg. 22 of Rosenfield thesis)
                outChans[currentSendID]->send(event);
                dbg.debug(_L4_, "LogicLayer%d sends %p to quad%u @ %" PRIu64 "\n", llID, (void*)event->getAddr(), currentSendID, currentCycle);
                currentSendID = (currentSendID + 1) % numOfOutBus;
            }
            else {
                unsigned int sendID = (event->getAddr() >>  sendAddressShift) & sendAddressMask;
                outChans[sendID]->send(event);
                dbg.debug(_L4_, "LogicLayer%d sends %p to vault%u @ %" PRIu64 "\n", llID, (void*)event->getAddr(), sendID, currentCycle);
            }
        }
        // This event is not for this LogicLayer
        else {
            if (NULL == toMem)
                dbg.fatal(CALL_INFO, -1, "LogicLayer%d not sure what to do with %p...\n", llID, event);
            toMem->send(event);
            reqUsedToMem[1]->addData(1);
            dbg.debug(_L4_, "LogicLayer%d sends %p to next\n", llID, event);
        }
    }

    // 2)
    /* Check For Events From Memory Chain
     *     and send them to CPU
     **/
    if (NULL != toMem) {
        while ( currentLimitReqBudgetMem[0] && (ev = toMem->recv()) ) {
            MemEvent *event  = dynamic_cast<MemEvent*>(ev);
            if (NULL == event)
                dbg.fatal(CALL_INFO, -1, "LogicLayer%d got bad event from another LogicLayer\n", llID);

            reqUsedToMem[0]->addData(1);

            toCPU->send(event);
            currentLimitReqBudgetMem[0]--;
            currentLimitReqBudgetCPU[1]--;
            reqUsedToCpu[1]->addData(1);
            dbg.debug(_L4_, "LogicLayer%d sends %p towards cpu (%" PRIu64 " %d)\n", llID, event, event->getID().first, event->getID().second);
        }
    }

    // 3)
    /* Check For Events From Quads
     *     and send them to CPU
     *     Transaction Support: save all transaction until we know what to do with them (dump or restart)
     **/
    unsigned j = 0;
    for (memChans_t::iterator it = outChans.begin(); it != outChans.end(); ++it, ++j) {
        memChan_t *m_memChan = *it;
        while ((ev = m_memChan->recv())) {
            MemEvent *event  = dynamic_cast<MemEvent*>(ev);
            if (event == NULL)
                dbg.fatal(CALL_INFO, -1, "LogicLayer%d got bad event from vaults\n", llID);
            memOpsProcessed->addData(1);
            toCPU->send(event);
            reqUsedToCpu[1]->addData(1);
            dbg.debug(_L4_, "LogicLayer%d got event %p from vault %u @%" PRIu64 ", sent towards cpu\n", llID, (void*)event->getAddr(), j, currentCycle);
        }
    }

    // 4)
    /* Check Xbar shared between Quads
     *     if any event, calculate quadID and send it
     **/
    if (haveQuad)
        for (int quadID=0; quadID < numOfOutBus; quadID++)
            while(ev = toXBar[quadID]->recv()) {
                MemEvent *event  = dynamic_cast<MemEvent*>(ev);
                if (NULL == event)
                    dbg.fatal(CALL_INFO, -1, "LogicLayer%d got bad event\n", llID);
                dbg.debug(_L4_, "LogicLayer%d XBar got req for %p (%" PRIu64 " %d)\n", llID, (void*)event->getAddr(), event->getID().first, event->getID().second);

                unsigned int evQuadID = (event->getAddr() >>  quadIDAddressShift) & quadIDAddressMask;
                outChans[evQuadID]->send(event);
                dbg.debug(_L4_, "LogicLayer%d sends %p to quad%u @ %" PRIu64 "\n", llID, (void*)event->getAddr(), evQuadID, currentCycle);
            }


    // Check for limits
    if (currentLimitReqBudgetCPU[0]==0 || currentLimitReqBudgetCPU[1]==0 || currentLimitReqBudgetMem[0]==0 || currentLimitReqBudgetMem[1]==0) {
        dbg.output(CALL_INFO, "logicLayer%d request budget saturated (%d %d %d %d) in window number %d @cycle=%lu\n",\
         llID, currentLimitReqBudgetCPU[0], currentLimitReqBudgetCPU[1],  currentLimitReqBudgetMem[0],  currentLimitReqBudgetMem[1], \
         currentLimitWindowNum, currentCycle);
    }

    //
    currentLimitWindowNum--;
    if (currentLimitWindowNum == 0) {
        currentLimitReqBudgetCPU[0] = reqLimitPerWindow;
        currentLimitReqBudgetCPU[1] = reqLimitPerWindow;
        currentLimitReqBudgetMem[0] = reqLimitPerWindow;
        currentLimitReqBudgetMem[1] = reqLimitPerWindow;

        currentLimitWindowNum = reqLimitWindowSize;
        dbg.debug(_L5_, "LogicLayer%d request budget restored (every %d cycles) @cycle=%lu\n", llID, reqLimitWindowSize, currentCycle);
    }

    return false;
}


/*
 * libVaultSimGen Functions
 */

extern "C" Component* create_logicLayer( SST::ComponentId_t id,  SST::Params& params )
{
    return new logicLayer( id, params );
}


/*
 *   Other Functions
 */


// Determine if we 'own' a given address
bool logicLayer::isOurs(unsigned int addr)
{
        return ((((addr >> LL_SHIFT) & LL_MASK) == llID) || (LL_MASK == 0));
}


/*
 *  Print Macsim style output in a file
 **/

void logicLayer::printStatsForMacSim() {
    string suffix = "logiclayer_" + to_string(llID);
    stringstream ss;
    ss << suffix.c_str() << ".stat.out";
    string filename = ss.str();

    ofstream ofs;
    ofs.exceptions(std::ofstream::eofbit | std::ofstream::failbit | std::ofstream::badbit);
    ofs.open(filename.c_str(), std::ios_base::out);

    writeTo(ofs, suffix, string("total_memory_ops_processed"), memOpsProcessed->getCollectionCount());
    writeTo(ofs, suffix, string("total_hmc_ops_processed"), HMCOpsProcessed->getCollectionCount());
    ofs << "\n";
    writeTo(ofs, suffix, string("req_recv_from_CPU"), reqUsedToCpu[0]->getCollectionCount());
    writeTo(ofs, suffix, string("req_send_to_CPU"),   reqUsedToCpu[1]->getCollectionCount());
    writeTo(ofs, suffix, string("req_recv_from_Mem"), reqUsedToMem[0]->getCollectionCount());
    writeTo(ofs, suffix, string("req_send_to_Mem"),   reqUsedToMem[1]->getCollectionCount());
    ofs << "\n";
    writeTo(ofs, suffix, string("total_HMC_candidate_ops"),   HMCCandidateProcessed->getCollectionCount());
}

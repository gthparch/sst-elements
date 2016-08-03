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

logicLayer::logicLayer(ComponentId_t id, Params& params) : IntrospectedComponent( id )
{
    // Debug and Output Initialization
    out.init("", 0, 0, Output::STDOUT);

    int debugLevel = params.find<int>("debug_level", 0);
    dbg.init("@R:LogicLayer::@p():@l " + getName() + ": ", debugLevel, 0, (Output::output_location_t)(int)params.find<bool>("debug", 0));
    if(debugLevel < 0 || debugLevel > 10)
        dbg.fatal(CALL_INFO, -1, "Debugging level must be between 0 and 10. \n");

    // logicLayer Params Initialization
    int ident = params.find<int>("llID", -1);
    if (-1 == ident)
        dbg.fatal(CALL_INFO, -1, "llID not defined\n");
    llID = ident;

    // request limit (BW control)
    reqLimitPerWindow = params.find<int>("req_LimitPerWindow", -1);
    if (0 >= reqLimitPerWindow)
        dbg.fatal(CALL_INFO, -1, " req_LimitPerWindow not defined well (defined in FLITs)\n");

    reqLimitWindowSize = params.find<int>("req_LimitWindowSize", 1);
    if (0 >= reqLimitWindowSize)
        dbg.fatal(CALL_INFO, -1, " req_LimitWindowSize not defined well\n");

    int test = params.find<int>("req_LimitPerCycle", -1);
    if (test != -1)
        dbg.fatal(CALL_INFO, -1, "req_LimitPerCycle ***DEPRECATED** kept for compatibility: " \
        "use req_LimitPerWindow & req_LimitWindowSize in your configuration\n");


    dbg.debug(_INFO_, "req_LimitPerWindow %d, req_LimitWindowSize: %d\n", reqLimitPerWindow, reqLimitWindowSize);
    currentLimitReqBudgetCPU[0] = reqLimitPerWindow;
    currentLimitReqBudgetCPU[1] = reqLimitPerWindow;
    currentLimitReqBudgetMemChain[0] = reqLimitPerWindow;
    currentLimitReqBudgetMemChain[1] = reqLimitPerWindow;
    currentLimitWindowNum = reqLimitWindowSize;

    //
    int mask = params.find<int>("LL_MASK", -1);
    if (-1 == mask)
        dbg.fatal(CALL_INFO, -1, " LL_MASK not defined\n");
    LL_MASK = mask;

    haveQuad = params.find<bool>("have_quad", 0);

    numVaultPerQuad = params.find<int>("num_vault_per_quad", 4);
    numVaultPerQuad2 = log(numVaultPerQuad) / log(2);

    bool terminal = params.find<int>("terminal", 0);

    CacheLineSize = params.find<uint64_t>("cacheLineSize", 64);
    CacheLineSizeLog2 = log(CacheLineSize) / log(2);

    numVaults = params.find<int>("vaults", -1);
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

    // ** -----cacheSim START-----**//
    // cacheSim for VaultSim
    isCacheSimEn = params.find<bool>("mem_cache_simulation", 0);
    if (isCacheSimEn) {
        cacheSimulator = new cacheSim;

        uint64_t cacheC; /* Size in bytes is 2^C */
        uint64_t cacheB; /* Bytes per block is 2^B */
        uint64_t cacheS; /* Blocks per set is 2^S */
        uint64_t cacheV; /* Blocks in victim cache */
        uint64_t cacheK; /* Preferch distance */

        cacheC = params.find<uint64_t>("mem_cache_size", 15);
        cacheB = params.find<uint64_t>("mem_cache_block_size", 6);
        cacheS = params.find<uint64_t>("mem_cache_blocks_per_set", 3);
        cacheV = params.find<uint64_t>("mem_cache_blocks_in_victim", 0);
        cacheK = params.find<uint64_t>("mem_cache_prefetch_distance", 0);

        // Setup the cache
        cacheSimulator->setupCache(cacheC, cacheB, cacheS, cacheV, cacheK);

        // Debug info`
        out.output("*LogicLayer%d: Made Memory Cache C:%" PRIu64 " B:%" PRIu64 " S:%" PRIu64 " V:%" PRIu64 " K:%" PRIu64 "\n", \
                    llID, cacheC, cacheB, cacheS, cacheV, cacheK);
    }


    // ** -----cacheSim END-----**//

    // clock
    std::string frequency;
    frequency = params.find<string>("clock", "2.0 Ghz");
    registerClock(frequency, new Clock::Handler<logicLayer>(this, &logicLayer::clock));
    dbg.debug(_INFO_, "Making LogicLayer with id=%d & clock=%s\n", llID, frequency.c_str());

    // Stats Initialization
    statsFormat = params.find<int>("statistics_format", 0);

    totalClocks = registerStatistic<uint64_t>("Clocks", "0");

    memOpsProcessed = registerStatistic<uint64_t>("Total_memory_ops_processed", "0");
    HMCOpsProcessed = registerStatistic<uint64_t>("HMC_ops_processed", "0");
    HMCCandidateProcessed = registerStatistic<uint64_t>("Total_HMC_candidate_processed", "0");
    HMCTransOpsProcessed = registerStatistic<uint64_t>("Total_HMC_transactions_processed", "0");

    reqUsedToCpu[0] = registerStatistic<uint64_t>("Req_recv_from_CPU", "0");
    reqUsedToCpu[1] = registerStatistic<uint64_t>("Req_send_to_CPU", "0");
    reqUsedToMem[0] = registerStatistic<uint64_t>("Req_recv_from_Mem", "0");
    reqUsedToMem[1] = registerStatistic<uint64_t>("Req_send_to_Mem", "0");

    bwFromCpuFull = registerStatistic<uint64_t>("Bw_From_CPU_is_Full", "0");
    bwToCpuFull = registerStatistic<uint64_t>("Bw_To_CPU_is_Full", "0");

    statFLITtoCPU = 0;
    statFLITfromCPU = 0;
    statFLITtoMem = 0;
    statFLITfromMem = 0;
}

void logicLayer::finish()
{
    dbg.debug(_INFO_, "Logic Layer %d completed %lu ops\n", llID, memOpsProcessed->getCollectionCount());
    //Print Statistics
    if (statsFormat == 1)
        printStatsForMacSim();
    if (isCacheSimEn) {
        dbg.debug(_L4_, "Memory Cache done\n");
        cacheSimulator->completeCache();
        //cacheSimulator->printStatistics();
        printCacheStatsForMacSim();
    }
}

bool logicLayer::clock(Cycle_t currentCycle)
{
    totalClocks->addData(1);

    SST::Event* ev = NULL;


    // 1-c)
    /* Check For Events From CPU
     *     Check ownership, if owned send to internal vaults, if not send to another LogicLayer
     **/
    while (1) {
        if (inEventsQ.empty()) { ev = toCPU->recv(); if (ev==NULL) break; }
        else { ev = inEventsQ.front(); inEventsQ.pop(); }

        MemEvent *event  = dynamic_cast<MemEvent*>(ev);
        if (NULL == event) dbg.fatal(CALL_INFO, -1, "LogicLayer%d got bad event\n", llID);
        dbg.debug(_L4_, "LogicLayer%d got req for %p (%" PRIu64 " %d)\n", llID, (void*)event->getAddr(), event->getID().first, event->getID().second);

        // Check for BW
        int reqFLITs = getReqFLITs(event, true);
        if (currentLimitReqBudgetCPU[0] < reqFLITs) {
            bwFromCpuFull->addData(1);
            dbg.debug(_L4_, "LogicLayer%d bwFromCpuFull is %d reqFLITs is %d for eventAddr %p @%" PRIu64 "\n", \
                llID, currentLimitReqBudgetCPU[0], reqFLITs, (void*)event->getAddr(), currentCycle);
                inEventsQ.push(event);
            break;
        }

        // BW Stat
        currentLimitReqBudgetCPU[0] -= reqFLITs;
        statFLITfromCPU += reqFLITs;
        reqUsedToCpu[0]->addData(1);

        // HMC Type verifications and stats
        #ifdef USE_VAULTSIM_HMC
        uint32_t HMCTypeEvent = event->getMemFlags();
        if (HMCTypeEvent >= NUM_HMC_TYPES)
            dbg.fatal(CALL_INFO, -1, "LogicLayer%d got bad HMC type %d for address %p\n", llID, event->getMemFlags(), (void*)event->getAddr());
        if (HMCTypeEvent == HMC_CANDIDATE)
            HMCCandidateProcessed->addData(1);
        else if (HMCTypeEvent != HMC_NONE && HMCTypeEvent != HMC_CANDIDATE)
            HMCOpsProcessed->addData(1);
        #endif


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
            //if we have cache, send access to it
            if (isCacheSimEn) {
                #ifdef USE_VAULTSIM_HMC
                if (HMCTypeEvent != HMC_NONE)
                    accessCache(event);
                #else
                accessCache(event);
                #endif
            }
        }
        // This event is not for this LogicLayer
        else {
            if (NULL == toMem)
                dbg.fatal(CALL_INFO, -1, "LogicLayer%d not sure what to do with %p...\n", llID, event);
            reqUsedToMem[1]->addData(1);
            currentLimitReqBudgetMemChain[1] -= reqFLITs;
            statFLITtoMem += reqFLITs;
            toMem->send(event);
            dbg.debug(_L4_, "LogicLayer%d sends %p to next\n", llID, event);
        }
    }

    // 2)
    /* Check For Events From Memory Chain
     *     and send them to CPU
     *     NOT EFFECTIVE CURRENTLY
     **/
    if (NULL != toMem) {
        while ( currentLimitReqBudgetMemChain[0] && (ev = toMem->recv()) ) {
            MemEvent *event  = dynamic_cast<MemEvent*>(ev);
            if (NULL == event)
                dbg.fatal(CALL_INFO, -1, "LogicLayer%d got bad event from another LogicLayer\n", llID);

            int reqFLITs = getReqFLITs(event, false);
            currentLimitReqBudgetMemChain[0] -= reqFLITs;
            statFLITfromMem += reqFLITs;
            currentLimitReqBudgetCPU[1] -= reqFLITs;
            statFLITtoCPU += reqFLITs;
            reqUsedToCpu[1]->addData(1);
            reqUsedToMem[0]->addData(1);

            toCPU->send(event);
            dbg.debug(_L4_, "LogicLayer%d sends %p towards cpu (%" PRIu64 " %d)\n", llID, event, event->getID().first, event->getID().second);
        }
    }

    // 3)
    /* Check For Events From Quads (or Vaults)
     *     and send them to CPU
     *     Transaction Support: save all transaction until we know what to do with them (dump or restart)
     **/
    unsigned j = 0;
    bool BwFull = false;
    for (memChans_t::iterator it = outChans.begin(); it != outChans.end(); ++it, ++j) {
        memChan_t *m_memChan = *it;
        while (1) {
            if (outEventsQ.empty()) { ev = m_memChan->recv(); if (ev==NULL) break; }
            else { ev = outEventsQ.front(); outEventsQ.pop(); }

            MemEvent *event  = dynamic_cast<MemEvent*>(ev);
            if (event == NULL) dbg.fatal(CALL_INFO, -1, "LogicLayer%d got bad event from vaults\n", llID);

            // Check for BW
            int reqFLITs = getReqFLITs(event, false);
            if (currentLimitReqBudgetCPU[1] < reqFLITs) {
                bwToCpuFull->addData(1); BwFull=true;
                dbg.debug(_L4_, "LogicLayer%d bwToCpuFull is %d reqFLITs is %d for eventAddr %p @%" PRIu64 "\n", \
                    llID, currentLimitReqBudgetCPU[1], reqFLITs, (void*)event->getAddr(), currentCycle);
                outEventsQ.push(event);
                break;
            }

            // BW Stat
            currentLimitReqBudgetCPU[1] -= reqFLITs;
            statFLITtoCPU += reqFLITs;
            reqUsedToCpu[1]->addData(1);

            memOpsProcessed->addData(1);

            toCPU->send(event);
            dbg.debug(_L4_, "LogicLayer%d got event %p from vault %u @%" PRIu64 ", sent towards cpu\n", llID, (void*)event->getAddr(), j, currentCycle);
        }
        if (BwFull) break;
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
    // if (currentLimitReqBudgetCPU[0]==0 || currentLimitReqBudgetCPU[1]==0 || currentLimitReqBudgetMemChain[0]==0 || currentLimitReqBudgetMemChain[1]==0) {
    //     dbg.debug(_L4_, "logicLayer%d request budget saturated (%d %d %d %d) in window number %d @cycle=%lu\n",\
    //      llID, currentLimitReqBudgetCPU[0], currentLimitReqBudgetCPU[1],  currentLimitReqBudgetMemChain[0],  currentLimitReqBudgetMemChain[1], \
    //      currentLimitWindowNum, currentCycle);
    // }

    //
    currentLimitWindowNum--;
    if (currentLimitWindowNum == 0) {
        currentLimitReqBudgetCPU[0] = reqLimitPerWindow;
        currentLimitReqBudgetCPU[1] = reqLimitPerWindow;
        currentLimitReqBudgetMemChain[0] = reqLimitPerWindow;
        currentLimitReqBudgetMemChain[1] = reqLimitPerWindow;

        currentLimitWindowNum = reqLimitWindowSize;
        //dbg.debug(_L4_, "LogicLayer%d request budget restored (every %d cycles) @cycle=%lu\n", llID, reqLimitWindowSize, currentCycle);
    }

    return false;
}


/*
 * returns number of FLITs per request
 */
int logicLayer::getReqFLITs(MemEvent *event, bool isReq)
{
    uint32_t HMCTypeEvent = event->getMemFlags();
    assert(HMCTypeEvent < NUM_HMC_TYPES);

    if (HMCTypeEvent == HMC_NONE || HMCTypeEvent == HMC_CANDIDATE) {
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
            break;
        }

        if (isWrite) {
            if (isReq) return 5;
            else return 1;
        }
        else {
            if (isReq) return 1;
            else return 5;
        }
    }


    else { //HMC Atomics
        if (HMCTypeEvent == HMC_FP_ADD || HMCTypeEvent == HMC_ADD_DUAL || \
            HMCTypeEvent == HMC_ADD_8B || HMCTypeEvent == HMC_ADD_16B || \
            HMC_COMP_equal ) {
                if (isReq) return 2;
                else return 1;
        }
        else return 2;

    }

}

/*
 * cacheSim Functions
 */

void logicLayer::accessCache(MemEvent *event)
{
    bool isWrite = false;

    #ifndef USE_VAULTSIM_HMC
    switch(event->getCmd()) {
    case SST::MemHierarchy::GetS:
        isWrite = false;
        break;
    case SST::MemHierarchy::GetSEx:
    case SST::MemHierarchy::GetX:
        isWrite = true;
        break;
    default:
        break;
    }
    #endif

    char rw = isWrite ? cacheSim::WRITE : cacheSim::READ;
    cacheSimulator->cacheAccess(rw, event->getAddr());

    dbg.debug(_L4_, "LogicLayer%d cache access for %p\n", llID, (void*)event->getAddr());
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

    writeTo(ofs, suffix, string("clocks"), totalClocks->getCollectionCount());
    ofs << "\n";
    writeTo(ofs, suffix, string("total_memory_ops_processed"), memOpsProcessed->getCollectionCount());
    writeTo(ofs, suffix, string("total_hmc_ops_processed"), HMCOpsProcessed->getCollectionCount());
    ofs << "\n";
    writeTo(ofs, suffix, string("req_recv_from_CPU"), reqUsedToCpu[0]->getCollectionCount());
    writeTo(ofs, suffix, string("req_send_to_CPU"),   reqUsedToCpu[1]->getCollectionCount());
    writeTo(ofs, suffix, string("req_recv_from_Mem_chain"), reqUsedToMem[0]->getCollectionCount());
    writeTo(ofs, suffix, string("req_send_to_Mem_chain"),   reqUsedToMem[1]->getCollectionCount());
    ofs << "\n";
    writeTo(ofs, suffix, string("FLITs_send_to_CPU"), statFLITtoCPU);
    writeTo(ofs, suffix, string("FLITs_recv_from_CPU"), statFLITfromCPU);
    writeTo(ofs, suffix, string("FLITs_send_to_Mem_chain"), statFLITtoMem);
    writeTo(ofs, suffix, string("FLITs_recv_from_Mem_chain"), statFLITfromMem);
    ofs << "\n";
    writeTo(ofs, suffix, string("cycles_BW_from_CPU_was_full"), bwFromCpuFull->getCollectionCount());
    writeTo(ofs, suffix, string("cycles_BW_to_CPU_was_full"), bwToCpuFull->getCollectionCount());
    ofs << "\n";
    writeTo(ofs, suffix, string("total_HMC_candidate_ops"),   HMCCandidateProcessed->getCollectionCount());
}

/*
 *  Print Macsim style output in a file for cache
 **/

void logicLayer::printCacheStatsForMacSim() {
    string suffix = "logiclayer_cache_" + to_string(llID);
    stringstream ss;
    ss << suffix.c_str() << ".stat.out";
    string filename = ss.str();

    ofstream ofs;
    ofs.exceptions(std::ofstream::eofbit | std::ofstream::failbit | std::ofstream::badbit);
    ofs.open(filename.c_str(), std::ios_base::out);

    writeTo(ofs, suffix, string("accesses"), cacheSimulator->p_stats->accesses);
    writeTo(ofs, suffix, string("reads"), cacheSimulator->p_stats->reads);
    writeTo(ofs, suffix, string("read_misses"), cacheSimulator->p_stats->read_misses);
    writeTo(ofs, suffix, string("read_misses_combined"), cacheSimulator->p_stats->read_misses_combined);
    ofs << "\n";
    writeTo(ofs, suffix, string("writes"), cacheSimulator->p_stats->writes);
    writeTo(ofs, suffix, string("write_misses"), cacheSimulator->p_stats->write_misses);
    writeTo(ofs, suffix, string("write_misses_combined"), cacheSimulator->p_stats->write_misses_combined);
    ofs << "\n";
    writeTo(ofs, suffix, string("misses"), cacheSimulator->p_stats->misses);
    writeTo(ofs, suffix, string("write_backs"), cacheSimulator->p_stats->write_backs);
    ofs << "\n";
    writeTo(ofs, suffix, string("miss_rate"), cacheSimulator->p_stats->miss_rate);
    writeTo(ofs, suffix, string("avg_access_time"), cacheSimulator->p_stats->avg_access_time);
    writeTo(ofs, suffix, string("hit_time"), cacheSimulator->p_stats->hit_time);
    writeTo(ofs, suffix, string("miss_penalty"), cacheSimulator->p_stats->miss_penalty);
    ofs << "\n";
    writeTo(ofs, suffix, string("vc_misses"), cacheSimulator->p_stats->vc_misses);
    writeTo(ofs, suffix, string("prefetched_blocks"), cacheSimulator->p_stats->prefetched_blocks);
    writeTo(ofs, suffix, string("useful_prefetches"), cacheSimulator->p_stats->useful_prefetches);
}

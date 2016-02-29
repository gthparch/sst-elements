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

#include "quad.h"

using namespace SST::Interfaces;
using namespace SST::MemHierarchy;

quad::quad(ComponentId_t id, Params& params) : IntrospectedComponent( id ) 
{
    // Debug and Output Initialization
    out.init("", 0, 0, Output::STDOUT);

    int debugLevel = params.find_integer("debug_level", 0);
    dbg.init("@R:Quad::@p():@l " + getName() + ": ", debugLevel, 0, (Output::output_location_t)params.find_integer("debug", 0));
    if(debugLevel < 0 || debugLevel > 10) 
        dbg.fatal(CALL_INFO, -1, "Debugging level must be between 0 and 10. \n");

    // quad Params Initialization
    int ident = params.find_integer("quadID", -1);
    if (-1 == ident)
        dbg.fatal(CALL_INFO, -1, "quadID not defined\n");
    quadID = ident;

    numVaultPerQuad = params.find_integer("num_vault_per_quad", 4);
    numVaultPerQuad2 = log(numVaultPerQuad) / log(2);
    
    CacheLineSize = params.find_integer("cacheLineSize", 64);
    CacheLineSizeLog2 = log(CacheLineSize) / log(2);

    numTotalVaults = params.find_integer("num_all_vaults", -1);
    numTotalVaults2 = log(numTotalVaults) / log(2);
    if (-1 == numTotalVaults) 
        dbg.fatal(CALL_INFO, -1, "num_all_vaults not defined\n");

    statsFormat = params.find_integer("statistics_format", 0);

    // clock
    std::string frequency;
    frequency = params.find_string("clock", "2.0 Ghz");
    registerClock(frequency, new Clock::Handler<quad>(this, &quad::clock));
    dbg.debug(_INFO_, "Making quad with id=%d & clock=%s\n", quadID, frequency.c_str());

    // link to LogicLayer
    toLogicLayer = configureLink("toLogicLayer");
    if (!toLogicLayer)
        dbg.fatal(CALL_INFO, -1, " could not find toLogicLayer\n");
    dbg.debug(_INFO_, "\tConnected toLogicLayer\n");

    // link to Xbar
    toXBar = configureLink("toXBar");
    if (!toXBar)
        dbg.fatal(CALL_INFO, -1, " could not find toXbar\n");
    dbg.debug(_INFO_, "\tConnected toXBar\n");

    // links to Vaults
    for (int i = 0; i < numVaultPerQuad; ++i) {
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

    // Mapping
    sendAddressMask = (1LL << numVaultPerQuad2) - 1;
    sendAddressShift = CacheLineSizeLog2;

    unsigned bitsForQuadID = log2(unsigned(numTotalVaults/numVaultPerQuad));
    quadIDAddressMask = (1LL << bitsForQuadID) - 1;
    quadIDAddressShift = CacheLineSizeLog2 + numVaultPerQuad2;

    // Stats
    statTotalTransactionsRecv = registerStatistic<uint64_t>("Total_transactions_recv", "0");
    statTransactionsSentToXbar = registerStatistic<uint64_t>("Transactions_sent_to_xbar", "0");
    statTotalTransactionsRecvFromVaults = registerStatistic<uint64_t>("Transactions_recv_from_vaults", "0");
}

bool quad::clock(Cycle_t currentCycle) {
    SST::Event* ev = NULL;

    // Check for events from LogicLayer
    while(ev=toLogicLayer->recv()) {
        MemEvent *event  = dynamic_cast<MemEvent*>(ev);
        if (NULL == event)
            dbg.fatal(CALL_INFO, -1, "Quad%d got bad event\n", quadID);
        dbg.debug(_L5_, "Quad%d got req for %p (%" PRIu64 " %d) @%" PRIu64 "\n", quadID, (void*)event->getAddr(), event->getID().first, event->getID().second, currentCycle);
        statTotalTransactionsRecv->addData(1);

        unsigned int evQuadID = (event->getAddr() >>  quadIDAddressShift) & quadIDAddressMask;

        // if event Quad ID matches Quad ID send it
        if (evQuadID == quadID) {
            dbg.debug(_L5_, "Quad%d %p with quadID %u matches quad ID @ %" PRIu64 "\n", quadID, (void*)event->getAddr(), evQuadID, currentCycle); 
            unsigned int sendID = (event->getAddr() >>  sendAddressShift) & sendAddressMask;
            outChans[sendID]->send(event);
            dbg.debug(_L5_, "Quad%d sends %p to vault%u(%u) @ %" PRIu64 "\n", quadID, (void*)event->getAddr(), sendID+(quadID*numVaultPerQuad), sendID, currentCycle); 
        }

        // event Quad ID not matching Quad ID, send it to Xbar
        else {
            dbg.debug(_L5_, "Quad%d %p with quadID %u not matching quad ID, sending it to Xbar @ %" PRIu64 "\n", quadID, (void*)event->getAddr(), evQuadID, currentCycle);
            statTransactionsSentToXbar->addData(1);
            toXBar->send(event);
        }
    }

    // Check for events from Vaults, if any send directly to LogicLayer
    unsigned j = 0;
    for (memChans_t::iterator it = outChans.begin(); it != outChans.end(); ++it, ++j) {
        memChan_t *m_memChan = *it;
        while ((ev = m_memChan->recv())) {
            MemEvent *event  = dynamic_cast<MemEvent*>(ev);
            if (event == NULL)
                dbg.fatal(CALL_INFO, -1, "Quad%d got bad event from vaults\n", quadID);
            dbg.debug(_L5_, "Quad%d got event %p from vault %u @%" PRIu64 ", sent towards cpu\n", quadID, (void*)event->getAddr(), j, currentCycle);
            toLogicLayer->send(event);
            statTotalTransactionsRecvFromVaults->addData(1);
        }    
    }
}

void quad::finish() 
{
    dbg.debug(_INFO_, "Quad%d completed\n", quadID);
    //Print Statistics
    if (statsFormat == 1)
        printStatsForMacSim();

}

/*
 * libVaultSimGen Functions
 */

extern "C" Component* create_quad( SST::ComponentId_t id,  SST::Params& params ) 
{
    return new quad( id, params );
}

/*
    Other Functions
*/

/*
 *  Print Macsim style output in a file
 **/

void quad::printStatsForMacSim() {
    string suffix = "quad_" + to_string(quadID);
    stringstream ss;
    ss << suffix.c_str() << ".stat.out";
    string filename = ss.str();
    
    ofstream ofs;
    ofs.exceptions(std::ofstream::eofbit | std::ofstream::failbit | std::ofstream::badbit);
    ofs.open(filename.c_str(), std::ios_base::out);

    writeTo(ofs, suffix, string("total_trans_recv"),                 statTotalTransactionsRecv->getCollectionCount());
    writeTo(ofs, suffix, string("trans_sent_to_xbar"),               statTransactionsSentToXbar->getCollectionCount());
    writeTo(ofs, suffix, string("total_trans_recv_from_vaults"),     statTotalTransactionsRecvFromVaults->getCollectionCount()); 
}


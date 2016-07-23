// Copyright 2009-2015 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL95000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2015 Sandia Corporation
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <string>
#include "Vault.h"

using namespace std;

#define NO_STRING_DEFINED "N/A"

Vault::Vault(Component *comp, Params &params) : SubComponent(comp)
{
    // Debug and Output Initialization
    out.init("", 0, 0, Output::STDOUT);

    int debugLevel = params.find<int>("debug_level", 0);
    if (debugLevel < 0 || debugLevel > 10)
        dbg.fatal(CALL_INFO, -1, "Debugging level must be between 0 and 10. \n");
    dbg.init("@R:Vault::@p():@l: ", debugLevel, 0, (Output::output_location_t)(int)params.find<bool>("debug", 0));

    dbgOnFlyHmcOpsIsOn = params.find<bool>("debug_OnFlyHmcOps", 0);
    dbgOnFlyHmcOps.init("onFlyHmcOps: ", 0, 0, (Output::output_location_t)(int)dbgOnFlyHmcOpsIsOn);
    if (dbgOnFlyHmcOpsIsOn) {
        dbgOnFlyHmcOpsThresh = params.find<int>("debug_OnFlyHmcOpsThresh", -1);
        if (-1 == dbgOnFlyHmcOpsThresh)
            dbg.fatal(CALL_INFO, -1, "vault.debug_OnFlyHmcOpsThresh is set to 1, definition of vault.debug_OnFlyHmcOpsThresh is required as well");
    }

    statsFormat = params.find<int>("statistics_format", 0);

    // HMC Cost Initialization
    HMCCostLogicalOps = params.find<int>("HMCCost_LogicalOps", 0);
    HMCCostCASOps = params.find<int>("HMCCost_CASOps", 0);
    HMCCostCompOps = params.find<int>("HMCCost_CompOps", 0);
    HMCCostAdd8 = params.find<int>("HMCCost_Add8", 0);
    HMCCostAdd16 = params.find<int>("HMCCost_Add16", 0);
    HMCCostAddDual = params.find<int>("HMCCost_AddDual", 0);
    HMCCostFPAdd = params.find<int>("HMCCost_FPAdd", 0);
    HMCCostSwap = params.find<int>("HMCCost_Swap", 0);
    HMCCostBitW = params.find<int>("HMCCost_BitW", 0);

    // DRAMSim2 Initialization
    string deviceIniFilename = params.find<string>("device_ini", NO_STRING_DEFINED);
    if (NO_STRING_DEFINED == deviceIniFilename)
        dbg.fatal(CALL_INFO, -1, "Define a 'device_ini' file parameter\n");

    string systemIniFilename = params.find<string>("system_ini", NO_STRING_DEFINED);
    if (NO_STRING_DEFINED == systemIniFilename)
        dbg.fatal(CALL_INFO, -1, "Define a 'system_ini' file parameter\n");

    string pwd = params.find<string>("pwd", ".");
    string logFilename = params.find<string>("logfile", "log");

    unsigned int ramSize = params.find<unsigned int>("mem_size", 0);
    if (0 == ramSize)
        dbg.fatal(CALL_INFO, -1, "DRAMSim mem_size parameter set to zero. Not allowed, must be power of two in megs.\n");

    id = params.find<int>("id", -1);
    if (id == -1)
        dbg.fatal(CALL_INFO, -1, "Define 'id' for the Vault\n");
    string idStr = std::to_string(id);
    string traceFilename = "VAULT_" + idStr + "_EPOCHS";

    dbg.output(CALL_INFO, "Vault%u: deviceIniFilename = %s, systemIniFilename = %s, pwd = %s, traceFilename = %s, size=%u\n",
            id, deviceIniFilename.c_str(), systemIniFilename.c_str(), pwd.c_str(), traceFilename.c_str(), ramSize);

    memorySystem = DRAMSim::getMemorySystemInstance(deviceIniFilename, systemIniFilename, pwd, traceFilename, ramSize);

    DRAMSim::Callback<Vault, void, unsigned, uint64_t, uint64_t> *readDataCB =
        new DRAMSim::Callback<Vault, void, unsigned, uint64_t, uint64_t>(this, &Vault::readComplete);
    DRAMSim::Callback<Vault, void, unsigned, uint64_t, uint64_t> *writeDataCB =
        new DRAMSim::Callback<Vault, void, unsigned, uint64_t, uint64_t>(this, &Vault::writeComplete);

    memorySystem->RegisterCallbacks(readDataCB, writeDataCB, NULL);

    // DramSim Update frequency (Rough way of managing internal BW)
    DRAMSimUpdatePerWindow = params.find<int>("DRAMSim_UpdatePerWindow", 1);
    if (0 >= DRAMSimUpdatePerWindow)
        dbg.fatal(CALL_INFO, -1, "DRAMSim_UpdatePerWindow not defined well\n");

    DRAMSimUpdateWindowSize = params.find<int>("DRAMSim_UpdateWindowSize", 1);
    if (0 >= DRAMSimUpdateWindowSize)
        dbg.fatal(CALL_INFO, -1, "DRAMSim_UpdateWindowSize not defined well\n");

    dbg.output(CALL_INFO, "Vault%u: DRAMSim_UpdatePerWindow %d, DRAMSim_UpdateWindowSize: %d\n", id, DRAMSimUpdatePerWindow, DRAMSimUpdateWindowSize);
    currentDRAMSimUpdateBudget = DRAMSimUpdatePerWindow;
    currentDRAMSimUpdateWindowNum = DRAMSimUpdateWindowSize;

    // request limit
    HMCOpsIssueLimitPerWindow = params.find<int>("onFlyHmcOps_LimitPerWindow", 8);
    if (0 >= HMCOpsIssueLimitPerWindow)
        dbg.fatal(CALL_INFO, -1, "HmcOpsIssue_LimitPerWindow not defined well\n");

    HMCOpsIssueLimitWindowSize = params.find<int>("onFlyHmcOps_LimitWindowSize", 1);
    if (0 >= HMCOpsIssueLimitWindowSize)
        dbg.fatal(CALL_INFO, -1, "HmcOpsIssue_LimitWindowSize not defined well\n");

    dbg.output(CALL_INFO, "Vault%u: onFlyHmcOps_LimitPerWindow %d, onFlyHmcOps_LimitWindowSize: %d\n", \
        id, HMCOpsIssueLimitPerWindow, HMCOpsIssueLimitWindowSize);
    currentHMCOpsIssueBudget = HMCOpsIssueLimitPerWindow;
    currentHMCOpsIssueLimitWindowNum = HMCOpsIssueLimitWindowSize;

    // Functional Units
    HmcFunctionalUnitNum = params.find<int>("HmcFunctionalUnit_Num", 1);

    dbg.output(CALL_INFO, "Vault%u: HmcFunctionalUnit_Num %d\n", id, HmcFunctionalUnitNum);

    // Atomics Banks Mapping
    numDramBanksPerRank = 1;
    #ifdef USE_VAULTSIM_HMC
        numDramBanksPerRank = params.find<int>("num_dram_banks_per_rank", 1);
        dbg.output(CALL_INFO, "Vault%u: numDramBanksPerRank %d\n", id, numDramBanksPerRank);
        if (numDramBanksPerRank < 0)
            dbg.fatal(CALL_INFO, -1, "numDramBanksPerRank should be bigger than 0.\n");
    #endif

    // etc Initialization
    onFlyHmcOps.reserve(ON_FLY_HMC_OP_OPTIMUM_SIZE);
    bankBusyMap.reserve(BANK_SIZE_OPTIMUM);
    computeDoneCycleMap.reserve(BANK_SIZE_OPTIMUM);
    addrComputeMap.reserve(BANK_SIZE_OPTIMUM);
    unlockAllBanks();
    transQ.reserve(TRANS_Q_OPTIMUM_SIZE);

    currentClockCycle = 0;

    // Stats Initialization
    statTotalTransactions = registerStatistic<uint64_t>("Total_transactions", "0");
    statTotalHmcOps       = registerStatistic<uint64_t>("Total_hmc_ops", "0");
    statTotalNonHmcOps    = registerStatistic<uint64_t>("Total_non_hmc_ops", "0");
    statTotalHmcCandidate = registerStatistic<uint64_t>("Total_candidate_hmc_ops", "0");

    statCyclesFUFullForHMCIssue = registerStatistic<uint64_t>("Cycles_fu_full_for_hmc_issue", "0");

    statTotalHmcConfilictHappened = registerStatistic<uint64_t>("Total_hmc_confilict_happened", "0");

    statTotalNonHmcRead   = registerStatistic<uint64_t>("Total_non_hmc_read", "0");
    statTotalNonHmcWrite  = registerStatistic<uint64_t>("Total_non_hmc_write", "0");

    statTotalHmcLatency   = registerStatistic<uint64_t>("Hmc_ops_total_latency", "0");
    statIssueHmcLatency   = registerStatistic<uint64_t>("Hmc_ops_issue_latency", "0");
    statReadHmcLatency    = registerStatistic<uint64_t>("Hmc_ops_read_latency", "0");
    statWriteHmcLatency   = registerStatistic<uint64_t>("Hmc_ops_write_latency", "0");

    statTotalHmcLatencyInt = 0;
    statIssueHmcLatencyInt = 0;
    statReadHmcLatencyInt = 0;
    statWriteHmcLatencyInt = 0;
}

void Vault::finish()
{
    dbg.debug(_L8_, "Vault %d finished\n", id);
    //Print Statistics
    if (statsFormat == 1)
        printStatsForMacSim();
}

void Vault::readComplete(unsigned id, uint64_t addr, uint64_t cycle)
{
    // Check for atomic
    #ifdef USE_VAULTSIM_HMC
    addr2TransactionMap_t::iterator mi = onFlyHmcOps.find(addr);
    #else
    addr2TransactionMap_t::iterator mi = onFlyHmcOps.end();
    #endif

    // Not found in map, not atomic
    if (mi == onFlyHmcOps.end()) {
        // DRAMSim returns ID that is useless to us
        (*readCallback)(id, addr, cycle);
        dbg.debug(_L7_, "Vault %d:hmc: Atomic op %p callback(read) @cycle=%lu\n",
                id, (void*)addr, cycle);
    }
    else {
        // Found in atomic
        dbg.debug(_L9_, "Vault %d:hmc: Atomic op %p (bank%u) read req answer has been received @cycle=%lu\n",
                id, (void*)mi->second.getAddr(), mi->second.getBankNo(), cycle);

        // Now in Compute Phase, set cycle done
        issueAtomicComputePhase(mi);
        computePhaseEnabledBanks.push_back(mi->second.getBankNo());

        /* statistics */
        mi->second.readDoneCycle = currentClockCycle;
        // mi->second.setHmcOpState(READ_ANS_RECV);
    }
}

void Vault::writeComplete(unsigned id, uint64_t addr, uint64_t cycle)
{
    // Check for atomic
    #ifdef USE_VAULTSIM_HMC
    addr2TransactionMap_t::iterator mi = onFlyHmcOps.find(addr);
    #else
    addr2TransactionMap_t::iterator mi = onFlyHmcOps.end();
    #endif

    // Not found in map, not atomic
    if (mi == onFlyHmcOps.end()) {
        // DRAMSim returns ID that is useless to us
        (*writeCallback)(id, addr, cycle);
        dbg.debug(_L8_, "Vault %d:hmc: Atomic op %p callback(write) @cycle=%lu\n",
                id, (void*)addr, cycle);
    }
    else {
        // Found in atomic
        dbg.debug(_L9_, "Vault %d:hmc: Atomic op %p (bank%u) write answer has been received @cycle=%lu\n",
                id, (void*)mi->second.getAddr(),  mi->second.getBankNo(), cycle);

        // mi->second.setHmcOpState(WRITE_ANS_RECV);
        // return as a write since all hmc ops comes as read
        (*writeCallback)(id, addr, cycle);
        dbg.debug(_L8_, "Vault %d:hmc: Atomic op %p (bank%u) callback at cycle=%lu\n",
                id, (void*)mi->second.getAddr(), mi->second.getBankNo(), cycle);

        /* statistics */
        mi->second.writeDoneCycle = currentClockCycle;
        statTotalHmcLatency->addData(mi->second.writeDoneCycle - mi->second.inCycle);
        statIssueHmcLatency->addData(mi->second.issueCycle - mi->second.inCycle);
        statReadHmcLatency->addData(mi->second.readDoneCycle - mi->second.issueCycle);
        statWriteHmcLatency->addData(mi->second.writeDoneCycle - mi->second.readDoneCycle);

        statTotalHmcLatencyInt += (mi->second.writeDoneCycle - mi->second.inCycle);
        statIssueHmcLatencyInt += (mi->second.issueCycle - mi->second.inCycle);
        statReadHmcLatencyInt += (mi->second.readDoneCycle - mi->second.issueCycle);
        statWriteHmcLatencyInt += (mi->second.writeDoneCycle - mi->second.readDoneCycle);

        // unlock
        unlockBank(mi->second.getBankNo());
        onFlyHmcOps.erase(mi);
    }
}

void Vault::update()
{
    currentClockCycle++;

    //DRAMSim update
    if (currentDRAMSimUpdateBudget) {
        memorySystem->update();
        currentDRAMSimUpdateBudget--;
    }

    currentDRAMSimUpdateWindowNum--;
    if (currentDRAMSimUpdateWindowNum == 0) {
        currentDRAMSimUpdateBudget = DRAMSimUpdatePerWindow;
        currentDRAMSimUpdateWindowNum = DRAMSimUpdateWindowSize;
        dbg.debug(_L10_, "Vault %d: DRAMSim Update Budget restored to %d @cycle=%lu\n", id, DRAMSimUpdatePerWindow, currentClockCycle);
    }

    // If we are in compute phase, check for cycle compute done
    if (!computePhaseEnabledBanks.empty())
        for(list<unsigned>::iterator it = computePhaseEnabledBanks.begin(); it != computePhaseEnabledBanks.end(); NULL) {
            unsigned bankId = *it;
            if (currentClockCycle >= getComputeDoneCycle(bankId)) {
                uint64_t addrCompute = getAddrCompute(bankId);
                dbg.debug(_L9_, "Vault %d:hmc: Atomic op %p (bank%u) compute phase has been done @cycle=%lu\n", \
                        id, (void*)addrCompute, bankId, currentClockCycle);
                addr2TransactionMap_t::iterator mi = onFlyHmcOps.find(addrCompute);
                issueAtomicSecondMemoryPhase(mi);

                eraseAddrCompute(bankId);
                eraseComputeDoneCycle(bankId);
                it = computePhaseEnabledBanks.erase(it);
            }
            else
                it++;
        }

    // Debug long hmc ops in Queue
    if (dbgOnFlyHmcOpsIsOn)
        for (auto it = onFlyHmcOps.begin(); it != onFlyHmcOps.end(); it++)
            if ( !it->second.getFlagPrintDbgHMC() )
                if (currentClockCycle - it->second.inCycle > dbgOnFlyHmcOpsThresh) {
                    it->second.setFlagPrintDbgHMC();
                    dbgOnFlyHmcOps.output(CALL_INFO, "Vault %u: Warning HMC op %p is onFly for %d cycles @cycle %lu\n", \
                                         id, (void*)it->second.getAddr(), dbgOnFlyHmcOpsThresh, currentClockCycle);
                }

    // Process Queue
    updateQueue();

    //Limits Update
    currentHMCOpsIssueLimitWindowNum--;
    if (currentHMCOpsIssueLimitWindowNum==0) {
        currentHMCOpsIssueLimitWindowNum = HMCOpsIssueLimitWindowSize;
        currentHMCOpsIssueBudget = HMCOpsIssueLimitPerWindow;
        dbg.debug(_L10_, "Vault %d: onFlyHMC Budget restored to %d @cycle=%lu\n", id, currentHMCOpsIssueBudget, currentClockCycle);
    }

}

bool Vault::addTransaction(transaction_c transaction)
{
    unsigned newChan, newRank, newBank, newRow, newColumn;
    DRAMSim::addressMapping(transaction.getAddr(), newChan, newRank, newBank, newRow, newColumn);
    newBank = newRank * numDramBanksPerRank + newBank;

    transaction.setBankNo(newBank);
    transaction.inCycle = currentClockCycle;

    // transaction.setHmcOpState(QUEUED);

    /* statistics & insert to Queue*/
    statTotalTransactions->addData(1);
    transQ.push_back(transaction);

    updateQueue();

    return true;
}

void Vault::updateQueue()
{
    // Check transaction Queue and if bank is not lock, issue transactions
    for (unsigned i = 0; i < transQ.size(); i++) { //FIXME: unoptimized erase of vector (change container or change iteration mode)
        // Bank is unlock
        if (!getBankState(transQ[i].getBankNo())) {
            if (transQ[i].getAtomic()) {
                if (currentHMCOpsIssueBudget && onFlyHmcOps.size() < HmcFunctionalUnitNum) {
                    // Lock the bank
                    lockBank(transQ[i].getBankNo());

                    // Add to onFlyHmcOps
                    onFlyHmcOps[transQ[i].getAddr()] = transQ[i];
                    currentHMCOpsIssueBudget--;
                    addr2TransactionMap_t::iterator mi = onFlyHmcOps.find(transQ[i].getAddr());
                    dbg.debug(_L9_, "Vault %d:hmc: Atomic op %p (bank%u) of type %s issued @cycle=%lu\n",
                            id, (void*)transQ[i].getAddr(), transQ[i].getBankNo(), transQ[i].getHmcOpTypeStr(), currentClockCycle);

                    // Issue First Phase
                    issueAtomicFirstMemoryPhase(mi);
                    // Remove from Transction Queue
                    transQ.erase(transQ.begin() + i);

                    /* statistics */
                    statTotalHmcOps->addData(1);
                    mi->second.issueCycle = currentClockCycle;
                  }
                  else {
                      dbg.debug(_L9_, "Vault %d: onFlyHMC Budget full at window #%d(%d) --- " \
                                "concurrent HMC Ops size is %d, FU# is %d @cycle=%lu\n",\
                                id, currentHMCOpsIssueLimitWindowNum, HMCOpsIssueLimitWindowSize, \
                                onFlyHmcOps.size(), HmcFunctionalUnitNum, currentClockCycle);

                     statCyclesFUFullForHMCIssue->addData(1);
                  }
            }
            else { // Not atomic op
                // Issue to DRAM
                bool isWrite_ = transQ[i].getIsWrite();
                memorySystem->addTransaction(isWrite_, transQ[i].getAddr());
                dbg.debug(_L9_, "Vault %d: %s %p (bank%u) issued @cycle=%lu\n",
                        id, transQ[i].getIsWrite() ? "Write" : "Read", (void*)transQ[i].getAddr(), transQ[i].getBankNo(), currentClockCycle);

                // Remove from Transction Queue
                transQ.erase(transQ.begin() + i);

                /* statistics */
                statTotalNonHmcOps->addData(1);
                if (isWrite_)
                    statTotalNonHmcWrite->addData(1);
                else
                    statTotalNonHmcRead->addData(1);
                if (transQ[i].getHmcOpType() == HMC_CANDIDATE)
                    statTotalHmcCandidate->addData(1);

            }
        }
        else
            statTotalHmcConfilictHappened->addData(1);
    }
}

void Vault::issueAtomicFirstMemoryPhase(addr2TransactionMap_t::iterator mi)
{
    dbg.debug(_L9_, "Vault %d:hmc: Atomic op %p (bank%u) 1st_mem phase started @cycle=%lu\n",
            id, (void*)mi->second.getAddr(), mi->second.getBankNo(), currentClockCycle);

    switch (mi->second.getHmcOpType()) {
    case (HMC_CAS_equal_16B):
    case (HMC_CAS_zero_16B):
    case (HMC_CAS_greater_16B):
    case (HMC_CAS_less_16B):
    case (HMC_ADD_16B):
    case (HMC_ADD_8B):
    case (HMC_ADD_DUAL):
    case (HMC_SWAP):
    case (HMC_BIT_WR):
    case (HMC_AND):
    case (HMC_NAND):
    case (HMC_OR):
    case (HMC_XOR):
    case (HMC_FP_ADD):
    case (HMC_COMP_greater):
    case (HMC_COMP_less):
    case (HMC_COMP_equal):
        if (!mi->second.getIsWrite()) {
            dbg.fatal(CALL_INFO, -1, "Atomic operation write flag should be write\n");
        }

        memorySystem->addTransaction(false, mi->second.getAddr());
        dbg.debug(_L9_, "Vault %d:hmc: Atomic op %p (bank%u) read req has been issued @cycle=%lu\n",
                id, (void*)mi->second.getAddr(), mi->second.getBankNo(), currentClockCycle);
        // mi->second.setHmcOpState(READ_ISSUED);
        break;
    case (HMC_NONE):
    default:
        dbg.fatal(CALL_INFO, -1, "Vault Should not get a non HMC op in issue atomic\n");
        break;
    }
}

void Vault::issueAtomicSecondMemoryPhase(addr2TransactionMap_t::iterator mi)
{
    dbg.debug(_L9_, "Vault %d:hmc: Atomic op %p (bank%u) 2nd_mem phase started @cycle=%lu\n", \
        id, (void*)mi->second.getAddr(), mi->second.getBankNo(), currentClockCycle);

    switch (mi->second.getHmcOpType()) {
    case (HMC_CAS_equal_16B):
    case (HMC_CAS_zero_16B):
    case (HMC_CAS_greater_16B):
    case (HMC_CAS_less_16B):
    case (HMC_ADD_16B):
    case (HMC_ADD_8B):
    case (HMC_ADD_DUAL):
    case (HMC_SWAP):
    case (HMC_BIT_WR):
    case (HMC_AND):
    case (HMC_NAND):
    case (HMC_OR):
    case (HMC_XOR):
    case (HMC_FP_ADD):
    case (HMC_COMP_greater):
    case (HMC_COMP_less):
    case (HMC_COMP_equal):
        if (!mi->second.getIsWrite()) {
            dbg.fatal(CALL_INFO, -1, "Atomic operation write flag should be write (2nd phase)\n");
        }

        memorySystem->addTransaction(true, mi->second.getAddr());
        dbg.debug(_L9_, "Vault %d:hmc: Atomic op %p (bank%u) write has been issued (2nd phase) @cycle=%lu\n",
                id, (void*)mi->second.getAddr(), mi->second.getBankNo(), currentClockCycle);
        // mi->second.setHmcOpState(WRITE_ISSUED);
        break;
    case (HMC_NONE):
    default:
        dbg.fatal(CALL_INFO, -1, "Vault Should not get a non HMC op in issue atomic (2nd phase)\n");
        break;
    }
}


void Vault::issueAtomicComputePhase(addr2TransactionMap_t::iterator mi)
{
    dbg.debug(_L9_, "Vault %d:hmc: Atomic op %p (bank%u) compute phase started @cycle=%lu\n",
            id, (void*)mi->second.getAddr(), mi->second.getBankNo(), currentClockCycle);

    // mi->second.setHmcOpState(COMPUTE);
    unsigned bankNoCompute = mi->second.getBankNo();
    uint64_t addrCompute = mi->second.getAddr();
    setAddrCompute(bankNoCompute, addrCompute);

    switch (mi->second.getHmcOpType()) {
    case (HMC_CAS_equal_16B):
    case (HMC_CAS_zero_16B):
    case (HMC_CAS_greater_16B):
    case (HMC_CAS_less_16B):
        computeDoneCycleMap[bankNoCompute] = currentClockCycle + HMCCostCASOps;
        break;
    case (HMC_ADD_16B):
        computeDoneCycleMap[bankNoCompute] = currentClockCycle + HMCCostAdd16;
        break;
    case (HMC_ADD_8B):
        computeDoneCycleMap[bankNoCompute] = currentClockCycle + HMCCostAdd8;
        break;
    case (HMC_ADD_DUAL):
        computeDoneCycleMap[bankNoCompute] = currentClockCycle + HMCCostAddDual;
        break;
    case (HMC_SWAP):
        computeDoneCycleMap[bankNoCompute] = currentClockCycle + HMCCostSwap;
        break;
    case (HMC_BIT_WR):
        computeDoneCycleMap[bankNoCompute] = currentClockCycle + HMCCostBitW;
        break;
    case (HMC_AND):
    case (HMC_NAND):
    case (HMC_OR):
    case (HMC_XOR):
        computeDoneCycleMap[bankNoCompute] = currentClockCycle + HMCCostLogicalOps;
        break;
    case (HMC_FP_ADD):
        computeDoneCycleMap[bankNoCompute] = currentClockCycle + HMCCostFPAdd;
        break;
    case (HMC_COMP_greater):
    case (HMC_COMP_less):
    case (HMC_COMP_equal):
        computeDoneCycleMap[bankNoCompute] = currentClockCycle + HMCCostCompOps;
        break;
    case (HMC_NONE):
    default:
        dbg.fatal(CALL_INFO, -1, "Vault Should not get a non HMC op in issue atomic (compute phase)\n");
        break;
    }
}

/*
    Other Functions
*/

/*
 *  Print Macsim style output in a file
 **/

void Vault::printStatsForMacSim() {
    string name_ = "vault_" + to_string(id);
    stringstream ss;
    ss << name_.c_str() << ".stat.out";
    string filename = ss.str();

    ofstream ofs;
    ofs.exceptions(std::ofstream::eofbit | std::ofstream::failbit | std::ofstream::badbit);
    ofs.open(filename.c_str(), std::ios_base::out);

    float avgHmcOpsLatencyTotal = (float)statTotalHmcLatency->getCollectionCount() / statTotalHmcOps->getCollectionCount();     //FIXME: this is wrong (getCollectionCount return #of elements)
    float avgHmcOpsLatencyIssue = (float)statIssueHmcLatency->getCollectionCount() / statTotalHmcOps->getCollectionCount();     //FIXME: this is wrong (getCollectionCount return #of elements)
    float avgHmcOpsLatencyRead  = (float)statReadHmcLatency->getCollectionCount() / statTotalHmcOps->getCollectionCount();      //FIXME: this is wrong (getCollectionCount return #of elements)
    float avgHmcOpsLatencyWrite = (float)statWriteHmcLatency->getCollectionCount() / statTotalHmcOps->getCollectionCount();     //FIXME: this is wrong (getCollectionCount return #of elements)

    float avgHmcOpsLatencyTotalInt = (float)statTotalHmcLatencyInt / statTotalHmcOps->getCollectionCount();
    float avgHmcOpsLatencyIssueInt = (float)statIssueHmcLatencyInt / statTotalHmcOps->getCollectionCount();
    float avgHmcOpsLatencyReadInt = (float)statReadHmcLatencyInt / statTotalHmcOps->getCollectionCount();
    float avgHmcOpsLatencyWriteInt = (float)statWriteHmcLatencyInt / statTotalHmcOps->getCollectionCount();

    writeTo(ofs, name_, string("total_trans"),                      statTotalTransactions->getCollectionCount());
    writeTo(ofs, name_, string("total_HMC_ops"),                    statTotalHmcOps->getCollectionCount());
    writeTo(ofs, name_, string("total_non_HMC_ops"),                statTotalNonHmcOps->getCollectionCount());
    writeTo(ofs, name_, string("total_HMC_candidate_ops"),          statTotalHmcCandidate->getCollectionCount());
    ofs << "\n";
    writeTo(ofs, name_, string("cycles_FU_full_for_HMC_issue"),          statCyclesFUFullForHMCIssue->getCollectionCount());
    ofs << "\n";
    writeTo(ofs, name_, string("total_hmc_confilict_happened"),     statTotalHmcConfilictHappened->getCollectionCount());
    ofs << "\n";
    writeTo(ofs, name_, string("total_non_HMC_read"),               statTotalNonHmcRead->getCollectionCount());
    writeTo(ofs, name_, string("total_non_HMC_write"),              statTotalNonHmcWrite->getCollectionCount());
    ofs << "\n";
    writeTo(ofs, name_, string("avg_HMC_ops_latency_total"),        avgHmcOpsLatencyTotalInt);
    writeTo(ofs, name_, string("avg_HMC_ops_latency_issue"),        avgHmcOpsLatencyIssueInt);
    writeTo(ofs, name_, string("avg_HMC_ops_latency_read"),         avgHmcOpsLatencyReadInt);
    writeTo(ofs, name_, string("avg_HMC_ops_latency_write"),        avgHmcOpsLatencyWriteInt);
}

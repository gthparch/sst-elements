#!/usr/bin/env python
import sys
import sst
import os 

KB = 1024
MB = 1024*KB
GB = 1024*MB

maxInsts = 200000000
simCycleCount = 200000000

TotalCoreNum = 16
SmallCoreNum = 16
LargeCoreNum = TotalCoreNum-SmallCoreNum
PimCoreNum = 4

coreFreq = "1 GHz"
memFreq = "1 GHz"

stackSize = 4*GB
numStack = TotalCoreNum/PimCoreNum

cacheLineSize = 64
L1CoherenceProtocol = "NONE"
L2CoherenceProtocol = "NONE"
replacementPolicy = "LRU"
pageSize = 4096
interleaveSize = 4096
TLBEntry = 32
duplicatePage = 0
pageAllocPolicy = "RR"

l1Lat = "1ps" #"1000ps"
l2Lat = "1ps" #"10000ps"
memLat = "8ns"
membackend_accesstime="50ns"

debugAddr = -1
debugCore = 0
debugMyNetwork = 0
debugL1 = 0
debugL2 = 0
debugL3 = 0
debugMem = 0

L1IParams = {
  "access_latency_cycles" : 4,
  "cache_frequency" : coreFreq,
  "replacement_policy" : replacementPolicy,
  "coherence_protocol" : L1CoherenceProtocol,
  "associativity" : 8,
  "cache_line_size" : cacheLineSize,
  "cache_size" : "16 KB",
  "L1" : 1,
  "debug" : debugL1,
  "debug_addr" : debugAddr,
  "debug_level" : 10
}

L1DParams = {
  "access_latency_cycles" : 4,
  "cache_frequency" : coreFreq,
  "replacement_policy" : replacementPolicy,
  "coherence_protocol" : L1CoherenceProtocol,
  "associativity" : 8,
  "cache_line_size" : cacheLineSize,
  "cache_size" : "16 KB",
  "L1" : 1,
  "debug" : debugL1,
  "debug_addr" : debugAddr,
  "debug_level" : 10
}

L1CParams = {
  "access_latency_cycles" : 4,
  "cache_frequency" : coreFreq,
  "replacement_policy" : replacementPolicy,
  "coherence_protocol" : L1CoherenceProtocol,
  "associativity" : 8,
  "cache_line_size" : cacheLineSize,
  "cache_size" : "8 KB",
  "L1" : 1,
  "debug" : debugL1,
  "debug_addr" : debugAddr,
  "debug_level" : 10
}

L1TParams = {
  "access_latency_cycles" : 100,
  "cache_frequency" : coreFreq,
  "replacement_policy" : replacementPolicy,
  "coherence_protocol" : L1CoherenceProtocol,
  "associativity" : 8,
  "cache_line_size" : cacheLineSize,
  "cache_size" : "8 KB",
  "statistics_format" : 1, 
  "L1" : 1,
  "debug" : debugL1,
  "debug_addr" : debugAddr,
  "debug_level" : 10
}

L2Params = {
  "access_latency_cycles" : 10,
  "cache_frequency" : coreFreq,
  "cache_type" : "noninclusive",
  "replacement_policy" : replacementPolicy,
  "coherence_protocol" : L2CoherenceProtocol,
  "associativity" : 8,
  "cache_line_size" : cacheLineSize,
  "cache_size" : "128 KB",
  "LL" : 1,
  "debug" : debugL2,
  "debug_addr" : debugAddr,
  "debug_level" : 10
}

def main(argv):
  sst.setProgramOption("timebase", "1ps")
  sst.setProgramOption("stopAtCycle", "0ns")

  GPU = sst.Component("gpu", "macsimComponent.macsimComponent")
  GPU.addParams({
    "mem_size" : numStack * stackSize,
    "command_line" : "--use_memhierarchy=1 --perfect_icache=1 \
      --num_sim_cores=%d --num_sim_small_cores=%d --num_sim_large_cores=%d \
      --core_type=ptx --large_core_type=x86 --max_insts=%d \
      --forward_progress_limit=500000 --sim_cycle_count=%d \
      --enable_physical_mapping=1 --page_size=%d --interleaving_size=%d \
      --num_stack=%d --stack_size=%u --tlb_entry=%d --page_alloc_dupl=%d \
      --page_alloc_policy=%s"
      %(TotalCoreNum, SmallCoreNum, LargeCoreNum, maxInsts, simCycleCount, 
          pageSize, interleaveSize, numStack, stackSize, TLBEntry, duplicatePage, pageAllocPolicy),
    "param_file" : "params.in",
    "trace_file" : "trace_file_list",
    "ptx_core" : 1,
    "num_link" : TotalCoreNum,
    "frequency" : coreFreq,
    "output_dir" : ".",
    "debug" : debugCore,
    "debug_addr" : debugAddr,
    "debug_level" : 8,
  })

  L2MemBus = sst.Component("l2MemBus", "memHierarchy.MyNetwork")
  L2MemBus.addParams({
    "frequency" : coreFreq,
    "local_latency" : 1,
    "remote_latency" : 10,
    "num_stack" : numStack,
    "stack_size" : stackSize/(1024*1024), #MB
    "interleave_size" : interleaveSize,
    "packet_size" : 64,
    "pim_local_bandwidth" : "1024",
    "host_pim_bandwidth" : "512",
    "inter_pim_bandwidth" : "128",
    "debug" : debugMyNetwork,
    "debug_level" : 10,
    "debug_addr" : debugAddr
  })

  # L2MemBus = sst.Component("l2MemBus", "memHierarchy.Bus")
  # L2MemBus.addParams({"bus_frequency" : coreFreq})

  for pimIdx in range(numStack):

    L1L2Bus = sst.Component("p%dl1l2bus"%pimIdx, "memHierarchy.Bus")
    L1L2Bus.addParams({"bus_frequency" : coreFreq})

    for coreIdx in range(PimCoreNum): 

      # globalCoreIdx = PimCoreNum * pimIdx + coreIdx
      globalCoreIdx = numStack * coreIdx + pimIdx ## for round-robin CUDA block distribution

      L1I = sst.Component("p%d.c%d.l1i"%(pimIdx,coreIdx), "memHierarchy.Cache")
      L1I.addParams(L1IParams)
      core_l1i_link = sst.Link("link-p%d.c%d:l1i"%(pimIdx,coreIdx))
      core_l1i_link.connect((GPU,"core%d-icache"%globalCoreIdx,l1Lat), (L1I,"high_network_0",l1Lat))

      L1D = sst.Component("p%d.c%d.l1d"%(pimIdx,coreIdx), "memHierarchy.Cache")
      L1D.addParams(L1DParams)
      core_l1d_link = sst.Link("link-p%d.c%d:l1d"%(pimIdx,coreIdx))
      core_l1d_link.connect((GPU,"core%d-dcache"%globalCoreIdx,l1Lat), (L1D,"high_network_0",l1Lat))

      L1C = sst.Component("p%d.c%d.l1c"%(pimIdx,coreIdx), "memHierarchy.Cache")
      L1C.addParams(L1CParams)
      core_l1c_link = sst.Link("link-p%d.c%d:l1c"%(pimIdx,coreIdx))
      core_l1c_link.connect((GPU,"core%d-ccache"%globalCoreIdx,l1Lat), (L1C,"high_network_0",l1Lat))

      L1T = sst.Component("p%d.c%d.l1t"%(pimIdx,coreIdx), "memHierarchy.Cache")
      L1T.addParams(L1TParams)
      core_l1t_link = sst.Link("link-p%d.c%d:l1t"%(pimIdx,coreIdx))
      core_l1t_link.connect((GPU,"core%d-tcache"%globalCoreIdx,l1Lat), (L1T,"high_network_0",l1Lat))

      l1i_l1l2bus_link = sst.Link("link-p%d.c%d.l1i:l1l2bus"%(pimIdx,coreIdx))
      l1i_l1l2bus_link.connect((L1I,"low_network_0",l2Lat), (L1L2Bus,"high_network_%d"%(4*coreIdx+0),l2Lat))
      l1d_l1l2bus_link = sst.Link("link-p%d.c%d.l1d:l1l2bus"%(pimIdx,coreIdx))
      l1d_l1l2bus_link.connect((L1D,"low_network_0",l2Lat), (L1L2Bus,"high_network_%d"%(4*coreIdx+1),l2Lat))
      l1c_l1l2bus_link = sst.Link("link-p%d.c%d.l1c:l1l2bus"%(pimIdx,coreIdx))
      l1c_l1l2bus_link.connect((L1C,"low_network_0",l2Lat), (L1L2Bus,"high_network_%d"%(4*coreIdx+2),l2Lat))
      l1t_l1l2bus_link = sst.Link("link-p%d.c%d.l1t:l1l2bus"%(pimIdx,coreIdx))
      l1t_l1l2bus_link.connect((L1T,"low_network_0",l2Lat), (L1L2Bus,"high_network_%d"%(4*coreIdx+3),l2Lat))

    L2 = sst.Component("p%d.l2"%pimIdx, "memHierarchy.Cache")
    L2.addParams(L2Params)

    l1l2bus_l2_link = sst.Link("link-p%d.l1l2bus:l2"%pimIdx)
    l1l2bus_l2_link.connect((L1L2Bus,"low_network_0",l2Lat), (L2,"high_network_0",l2Lat))

    l2_l2MemBus_link = sst.Link("link-p%d.l2:l2MemBus"%pimIdx)
    l2_l2MemBus_link.connect((L2,"low_network_0",l2Lat), (L2MemBus,"high_network_%d"%pimIdx, l2Lat))

  for stackIdx in range(numStack):
    memctrl = sst.Component("memctrl%d"%stackIdx, "memHierarchy.MemController")
    memctrl.addParams({
      "clock" : memFreq,
      "coherence_protocol" : L2CoherenceProtocol,
      "debug" : debugMem,
      "backend.mem_size" : stackSize/(1024*1024), #MB
      "backend.access_time" : membackend_accesstime,
      "backend" : "memHierarchy.simpleMem",
      "do_not_back" : 1,
    })
    
    l2MemBus_memctrl_link = sst.Link("link.l3:memctrl%d"%stackIdx)
    l2MemBus_memctrl_link.connect((L2MemBus, "low_network_%d"%stackIdx, memLat), (memctrl, "direct_link", memLat))


  # Define the SST Component Statistics Information
  # Define SST Statistics Options:
  sst.setStatisticLoadLevel(7)
  sst.setStatisticOutput("sst.statoutputcsv", {
    "separator" : ",",
    "filepath" : "sst.stat.csv",
    "outputtopheader" : 1,
    "outputsimtime" : 1,
    "outputrank" : 1,
  })
  
  # Define Component Statistics Information:
  sst.enableAllStatisticsForComponentType("memHierarchy.Cache")
  sst.enableAllStatisticsForComponentType("memHierarchy.MemController")
  #comp_core0l1dcache.enableAllStatistics({"type":"sst.AccumulatorStatistic", "rate":"0ns", "startat":"0ns", "stopat":"0ns"})



if __name__ == "__main__":
    main(sys.argv[1:])

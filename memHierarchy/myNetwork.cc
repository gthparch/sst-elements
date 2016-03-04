#include <sstream>
#include <csignal>
#include <cassert>
#include <boost/variant.hpp>
#include <sst_config.h>
#include <sst/core/serialization.h>
#include <sst/core/params.h>
#include <sst/core/simulation.h>
#include <sst/core/interfaces/stringEvent.h>
#include "memEvent.h"
#include "myNetwork.h"

using namespace std;
using namespace SST;
using namespace SST::MemHierarchy;

MyNetwork::MyNetwork(ComponentId_t id, Params& params) : Component(id)
{
  m_name = this->Component::getName();
  m_currentCycle = 0;

  configureParameters(params);
  configureLinks();

  for (int i = 0; i < m_numStack; ++i) {
    MemoryCompInfo *mci = new MemoryCompInfo(i, m_numStack, m_stackSize,
        m_interleaveSize);
    m_memoryMap[m_lowNetPorts[i]->getId()] = mci;
    cout << *mci << endl;
  }

  for (int i = 0; i < m_numStack; ++i) {
    m_highNetIdxMap[m_highNetPorts[i]->getId()] = i;
    m_lowNetIdxMap[m_lowNetPorts[i]->getId()] = i;
    m_requestQueues.push_back(map<SST::Event*, uint64_t>());
    m_responseQueues.push_back(map<SST::Event*, uint64_t>());
  }

  initializePacketCounter();
}

MyNetwork::~MyNetwork() 
{
  for (map<LinkId_t, MemoryCompInfo*>::iterator it = m_memoryMap.begin(); 
      it != m_memoryMap.end(); ++it) {
    delete it->second;
  }
}

/** 
 * - Each PIM can send/receive up to m_maxPacketPIMInternal packets to/from its
 *   local memory every cycle
 * - Each PIM can send/receive up to m_maxPacketInterPIM packets to/from remote
 *   memory every cycle 
 * - Host can send/receive up to m_maxPacketHostPIM packets to/from each PIM
 *   memory every cycle
 *
 * - Response has more priority than request since the former is in the critical
 *   path
 */
bool MyNetwork::clockTick(Cycle_t time) 
{
  m_currentCycle++;

  resetPacketCounter();

  unsigned stackIdx = 0;
  unsigned numResponseSentThisCycle = 0;
  for (auto responseQueueIterator = m_responseQueues.begin();
      responseQueueIterator != m_responseQueues.end(); responseQueueIterator++,
      stackIdx++)
  {
    for (auto responseIterator = responseQueueIterator->begin();
        responseIterator != responseQueueIterator->end(); responseIterator++) {
      uint64_t readyCycle = responseIterator->second;
      if (readyCycle < m_currentCycle) { 
        SST::Event* ev = responseIterator->first;

        // This is for bandwidth control
        MemEvent *me = static_cast<MemEvent*>(ev);
        unsigned sourceIdx = m_lowNetIdxMap[me->getDeliveryLink()->getId()];
        unsigned destinationIdx = m_highNetIdxMap[lookupNode(me->getDst())];
        access_type accessType = access_type::max;
        if (sourceIdx == destinationIdx) 
          accessType = access_type::pl;
        else
          accessType = access_type::ip;

        // if saturated, don't send any more response to the corresponding
        // accessType
        if (m_maxPacketPerCycle[accessType] <=
            m_packetCounters[stackIdx][static_cast<unsigned>(accessType)])
          continue;

        sendResponse(ev);
        responseQueueIterator->erase(responseIterator);

        assert(stackIdx == sourceIdx);
        m_packetCounters[sourceIdx][static_cast<unsigned>(accessType)] += 1;
        if (sourceIdx != destinationIdx)
          m_packetCounters[destinationIdx][static_cast<unsigned>(accessType)] += 1;

        numResponseSentThisCycle++;
      }
    }

    if (numResponseSentThisCycle > 0) {
      m_dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %u PIM-local responses from PIM[%u]\n", 
          m_currentCycle, m_packetCounters[stackIdx][static_cast<int>(access_type::pl)], stackIdx);
      m_dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %u inter-PIM responses from PIM[%u]\n", 
          m_currentCycle, m_packetCounters[stackIdx][static_cast<int>(access_type::ip)], stackIdx);
      //m_dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %u host-PIM  responses from PIM[%u]\n", 
      //m_currentCycle, m_packetCounters[stackIdx][static_cast<int>(access_type::hp)], stackIdx);
    }
  }

  stackIdx = 0;
  unsigned numRequestSentThisCycle = 0;
  for (auto requestQueueIterator = m_requestQueues.begin();
      requestQueueIterator != m_requestQueues.end(); requestQueueIterator++,
      stackIdx++) {
    for (auto requestIterator = requestQueueIterator->begin(); 
        requestIterator != requestQueueIterator->end(); requestIterator++) {
      uint64_t readyCycle = requestIterator->second;
      if (readyCycle < m_currentCycle) { 
        SST::Event* ev = requestIterator->first;
        
        // This is for bandwidth control
        MemEvent *me = static_cast<MemEvent*>(ev);
        unsigned sourceIdx = m_highNetIdxMap[me->getDeliveryLink()->getId()];
        unsigned destinationIdx = m_lowNetIdxMap[lookupNode(me->getAddr())];
        access_type accessType = access_type::max;
        if (sourceIdx == destinationIdx) 
          accessType = access_type::pl;
        else
          accessType = access_type::ip;

        // if saturated, don't send any more response to the corresponding
        // accessType
        if (m_maxPacketPerCycle[accessType] <=
            m_packetCounters[stackIdx][static_cast<unsigned>(accessType)])
          continue;

        sendRequest(ev);
        requestQueueIterator->erase(requestIterator);

        assert(stackIdx == sourceIdx);
        m_packetCounters[stackIdx][static_cast<unsigned>(accessType)] += 1;
        if (sourceIdx != destinationIdx)
          m_packetCounters[destinationIdx][static_cast<unsigned>(accessType)] += 1;

        numRequestSentThisCycle++;
      }
    }

    if (numRequestSentThisCycle > 0) {
      m_dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %u PIM-local requests/responses to/from PIM[%u]\n", 
          m_currentCycle, m_packetCounters[stackIdx][static_cast<int>(access_type::pl)], stackIdx);
      m_dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %u inter-PIM requests/responses to/from PIM[%u]\n", 
          m_currentCycle, m_packetCounters[stackIdx][static_cast<int>(access_type::ip)], stackIdx);
      //m_dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %u host-PIM  requests/responses to/from PIM[%u]\n", 
      //m_currentCycle, m_packetCounters[stackIdx][static_cast<int>(access_type::hp)], stackIdx);
    }
  }

  for (unsigned stackIdx = 0; stackIdx < m_numStack; stackIdx++) {
    for (unsigned accessTypeIdx = 0; accessTypeIdx <
        static_cast<unsigned>(access_type::max); accessTypeIdx++) {
      m_bandwidthUtilizationHistogram[stackIdx][accessTypeIdx][m_packetCounters[stackIdx][accessTypeIdx]] += 1;
    }
  }

  return false;
}

void MyNetwork::processIncomingRequest(SST::Event *ev) 
{ 
  MemEvent *me = static_cast<MemEvent*>(ev);

  LinkId_t sourceLinkIdx = me->getDeliveryLink()->getId();
  unsigned sourceIdx = m_highNetIdxMap[sourceLinkIdx];
  LinkId_t destinationLinkIdx = lookupNode(me->getAddr());
  unsigned destinationIdx = m_lowNetIdxMap[destinationLinkIdx];

  unsigned latency = 0;

  if (sourceIdx == destinationIdx) {
    latency = m_localLatency;
    ++m_local_accesses[sourceIdx];
  } else {
    latency = m_remoteLatency;
    ++m_remote_accesses[sourceIdx];
  }

  m_latencyMaps[sourceIdx][me->getAddr()] = m_currentCycle;
  ++m_per_stack_accesses[sourceIdx][destinationIdx];

  uint64_t readyCycle = m_currentCycle + latency;

  if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
    m_dbg.debug(_L3_,"RECV %s for 0x%" PRIx64 " Src: %s Dst: %u currentCycle: %" PRIu64 " readyCycle: %" PRIu64 "\n",
      CommandString[me->getCmd()], me->getAddr(), me->getSrc().c_str(), destinationIdx, m_currentCycle, readyCycle);
  }

  m_requestQueues[sourceIdx][ev] = readyCycle; 
}

void MyNetwork::processIncomingResponse(SST::Event *ev) 
{ 
  MemEvent *me = static_cast<MemEvent*>(ev);

  LinkId_t sourceLinkIdx = me->getDeliveryLink()->getId();
  unsigned sourceIdx = m_lowNetIdxMap[sourceLinkIdx];
  LinkId_t destinationLinkIdx = lookupNode(me->getDst());
  unsigned destinationIdx = m_highNetIdxMap[destinationLinkIdx];
  uint64_t localAddress = me->getAddr();
  uint64_t flatAddress = convertToFlatAddress(localAddress,
      m_memoryMap[me->getDeliveryLink()->getId()]->getRangeStart());

  unsigned latency = 0;
  uint64_t roundTripTime = 0;

  if (sourceIdx == destinationIdx) {
    latency = m_localLatency;
    roundTripTime = m_currentCycle + latency -
      m_latencyMaps[destinationIdx][flatAddress];
    m_local_access_latencies[destinationIdx] += roundTripTime;
  } else {
    latency = m_remoteLatency;
    roundTripTime = m_currentCycle + latency -
      m_latencyMaps[destinationIdx][flatAddress];
    m_remote_access_latencies[destinationIdx] += roundTripTime;
  }

  m_latencyMaps[destinationIdx].erase(flatAddress);
  ++m_per_core_accesses[sourceIdx][destinationIdx];

  uint64_t readyCycle = m_currentCycle + latency;

  if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
    m_dbg.debug(_L3_,"RECV %s for 0x%" PRIx64 " Src: %s Dst: %s currentCycle: %" PRIu64 " readyCycle: %" PRIu64 "\n",
      CommandString[me->getCmd()], flatAddress, me->getSrc().c_str(), me->getDst().c_str(), m_currentCycle, readyCycle);
  }

  m_responseQueues[sourceIdx][ev] = readyCycle; 
}

void MyNetwork::sendRequest(SST::Event* ev) 
{
  MemEvent *me = static_cast<MemEvent*>(ev);

  LinkId_t destinationLinkId = lookupNode(me->getAddr());
  SST::Link* destinationLink = m_linkIdMap[destinationLinkId];

  MemEvent* forwardEvent = new MemEvent(*me);
  forwardEvent->setAddr(convertToLocalAddress(me->getAddr(),
        m_memoryMap[destinationLinkId]->getRangeStart()));
  forwardEvent->setBaseAddr(convertToLocalAddress(me->getAddr(),
        m_memoryMap[destinationLinkId]->getRangeStart()));

  if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
    unsigned sourceIdx = m_highNetIdxMap[me->getDeliveryLink()->getId()];
    unsigned destinationIdx = m_lowNetIdxMap[lookupNode(me->getAddr())];
    m_dbg.debug(_L3_,"SEND %s for 0x%" PRIx64 " at currentCycle: %" PRIu64 " from %u to %u\n", 
        CommandString[me->getCmd()], me->getAddr(), m_currentCycle, sourceIdx, destinationIdx);
  }

  destinationLink->send(forwardEvent);
  delete me;
}

void MyNetwork::sendResponse(SST::Event* ev) 
{
  MemEvent *me = static_cast<MemEvent*>(ev);

  LinkId_t destinationLinkId = lookupNode(me->getDst());
  SST::Link* destinationLink = m_linkIdMap[destinationLinkId];

  MemEvent* forwardEvent = new MemEvent(*me);
  forwardEvent->setAddr(convertToFlatAddress(me->getAddr(),
        m_memoryMap[me->getDeliveryLink()->getId()]->getRangeStart()));
  forwardEvent->setBaseAddr(convertToFlatAddress(me->getAddr(),
        m_memoryMap[me->getDeliveryLink()->getId()]->getRangeStart()));

  if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
    unsigned sourceIdx = m_lowNetIdxMap[me->getDeliveryLink()->getId()];
    unsigned destinationIdx = m_highNetIdxMap[lookupNode(me->getDst())];
    m_dbg.debug(_L3_,"SEND %s for 0x%" PRIx64 " at currentCycle: %" PRIu64 " from %u to %u\n", 
        CommandString[me->getCmd()], me->getAddr(), m_currentCycle, sourceIdx, destinationIdx);
  }

  destinationLink->send(forwardEvent);
  delete me;
}

void MyNetwork::mapNodeEntry(const string& name, LinkId_t id)
{
  std::map<std::string, LinkId_t>::iterator it = m_nameMap.find(name);
  if (m_nameMap.end() != it) {
    m_dbg.fatal(CALL_INFO, -1, "%s, Error: MyNetwork attempting to map node that has already been mapped\n", 
        getName().c_str());
  }
  m_nameMap[name] = id;
}

LinkId_t MyNetwork::lookupNode(const uint64_t addr) 
{
  LinkId_t destinationLinkId = -1;
  for (map<LinkId_t, MemoryCompInfo*>::iterator it = m_memoryMap.begin(); 
      it != m_memoryMap.end(); ++it) {
    if (it->second->contains(addr)) {
      destinationLinkId = it->first;
    }
  }

  if (destinationLinkId == -1) {
    m_dbg.fatal(CALL_INFO, -1, "%s, Error: MyNetwork lookup for address 0x%" PRIx64 "failed\n", 
        getName().c_str(), addr);
  }

  return destinationLinkId;
}

LinkId_t MyNetwork::lookupNode(const string& name)
{
	map<string, LinkId_t>::iterator it = m_nameMap.find(name);
  if (it == m_nameMap.end()) {
      m_dbg.fatal(CALL_INFO, -1, "%s, Error: MyNetwork lookup of node %s returned no mapping\n", 
          getName().c_str(), name.c_str());
  }
  return it->second;
}

uint64_t MyNetwork::convertToLocalAddress(uint64_t requestedAddress, 
    uint64_t rangeStart)
{
  uint64_t localAddress = 0;

  if (m_interleaveSize == 0) {
    localAddress = requestedAddress - rangeStart;
  } else {
    uint64_t interleaveStep = m_interleaveSize * m_numStack;

    Addr addr = requestedAddress - rangeStart;
    Addr step = addr / interleaveStep;
    Addr offset = addr % interleaveStep;

    localAddress = (step * m_interleaveSize) + offset;
  }

  if (DEBUG_ALL || requestedAddress == DEBUG_ADDR) {
    m_dbg.debug(_L10_, "Converted requested address 0x%" PRIx64 " to local address 0x%" PRIx64 "\n", 
        requestedAddress, localAddress);
  }

  return localAddress;
}

uint64_t MyNetwork::convertToFlatAddress(uint64_t localAddress, 
    uint64_t rangeStart)
{
  uint64_t flatAddress = 0;

  if (m_interleaveSize == 0) {
    flatAddress = localAddress + rangeStart;
  } else {
    uint64_t interleaveStep = m_interleaveSize * m_numStack;

    Addr addr = localAddress;
    Addr step = addr / m_interleaveSize; 
    Addr offset = addr % m_interleaveSize;

    flatAddress = (step * interleaveStep) + offset;
    flatAddress = flatAddress + rangeStart;
  }

  if (DEBUG_ALL || flatAddress == DEBUG_ADDR) {
    m_dbg.debug(_L10_, "Converted local address 0x%" PRIx64 " to flat address 0x%" PRIx64 "\n", 
        localAddress, flatAddress);
  }

  return flatAddress;
}

void MyNetwork::configureParameters(SST::Params& params) 
{
  int debugLevel = params.find_integer("debug_level", 0);
  Output::output_location_t outputLocation =
    (Output::output_location_t)params.find_integer("debug", 0);

  string name = "[" + this->getName() + "] ";
  m_dbg.init(name.c_str(), debugLevel, 0, outputLocation);
  if (debugLevel < 0 || debugLevel > 10) {
    m_dbg.fatal(CALL_INFO, -1, "Debugging level must be betwee 0 and 10. \n");
  }

  int debugAddr = params.find_integer("debug_addr", -1);
  if (debugAddr == -1) {
    DEBUG_ADDR = (Addr)debugAddr;
    DEBUG_ALL = true;
  } else {
    DEBUG_ADDR = (Addr)debugAddr;
    DEBUG_ALL = false;
  }

  m_packetSize = params.find_integer("packet_size", 64);

  unsigned PIMLocalBandwidth = params.find_integer("pim_local_bandwidth", 1024);
  unsigned hostPIMBandwidth = params.find_integer("host_pim_bandwidth", 512);
  unsigned interPIMBandwidth = params.find_integer("inter_pim_bandwidth", 128);

  string frequency = params.find_string("frequency", "1 GHz");
  float frequencyInGHz = (float)(UnitAlgebra(frequency).getRoundedValue()) /
    (float)(UnitAlgebra("1GHz").getRoundedValue());

  m_maxPacketPerCycle[access_type::pl] = (unsigned)((float)PIMLocalBandwidth /
      frequencyInGHz / m_packetSize);
  m_maxPacketPerCycle[access_type::ip] = (unsigned)((float)interPIMBandwidth /
      frequencyInGHz / m_packetSize);
  m_maxPacketPerCycle[access_type::hp] = (unsigned)((float)hostPIMBandwidth /
      frequencyInGHz / m_packetSize);

  m_dbg.debug(_L10_, "Max packets per cycle for PIM-local communication: %u\n",
      m_maxPacketPerCycle[access_type::pl]);
  m_dbg.debug(_L10_, "Max packets per cycle for inter-PIM communication: %u\n",
      m_maxPacketPerCycle[access_type::ip]);
  m_dbg.debug(_L10_, "Max packets per cycle for Host-PIM communication: %u\n",
      m_maxPacketPerCycle[access_type::hp]);

  m_localLatency = params.find_integer("local_latency", 1);
  m_remoteLatency = params.find_integer("remote_latency", 1);

  Clock::Handler<MyNetwork>* clockHandler = new Clock::Handler<MyNetwork>(this,
      &MyNetwork::clockTick);
  registerClock(frequency, clockHandler);

  m_numStack = params.find_integer("num_stack", 1);
  uint64_t stackSize = params.find_integer("stack_size", 0); // in MB
  m_stackSize = stackSize * (1024*1024ul); // Convert into MBs
  m_interleaveSize = (uint64_t) params.find_integer("interleave_size", 4096);
}

void MyNetwork::configureLinks() 
{
  SST::Link* link;
	for (int i = 0; i < m_numStack; i++) {
		ostringstream linkName;
		linkName << "high_network_" << i;
		string ln = linkName.str();
    link = configureLink(ln, "1 ps", new Event::Handler<MyNetwork>(this,
          &MyNetwork::processIncomingRequest));
		if (link) {
      m_highNetPorts.push_back(link);
      m_linkIdMap[m_highNetPorts[i]->getId()] = m_highNetPorts[i];
      m_dbg.output(CALL_INFO, "Port %lu = Link %d\n", m_highNetPorts[i]->getId(), i);
    }
  }
  
  for (int i = 0; i < m_numStack; i++) {
		ostringstream linkName;
		linkName << "low_network_" << i;
		string ln = linkName.str();
    link = configureLink(ln, "1 ps", new Event::Handler<MyNetwork>(this,
          &MyNetwork::processIncomingResponse));
    if (link) {
      m_lowNetPorts.push_back(link);
      m_linkIdMap[m_lowNetPorts[i]->getId()] = m_lowNetPorts[i];
      m_dbg.output(CALL_INFO, "Port %lu = Link %d\n", m_lowNetPorts[i]->getId(), i);
    }
	}
}

void MyNetwork::init(unsigned int phase)
{
  SST::Event *ev;
  for (int i = 0; i < m_numStack; i++) {
    while ((ev = m_highNetPorts[i]->recvInitData())) {
      MemEvent* memEvent = dynamic_cast<MemEvent*>(ev);
      if (memEvent && memEvent->getCmd() == NULLCMD) {
        mapNodeEntry(memEvent->getSrc(), m_highNetPorts[i]->getId());
      }
      delete memEvent;
    }
  }

  initStats();
}

void MyNetwork::finish()
{
  // sanity check for histogram
  for (unsigned stackIdx = 0; stackIdx < m_numStack; stackIdx++) {
    for (unsigned accessTypeIdx = 0; accessTypeIdx <
        static_cast<unsigned>(access_type::max); accessTypeIdx++) {
      uint64_t totalCycle = 0;
      for (auto histogramIterator =
          m_bandwidthUtilizationHistogram[stackIdx][accessTypeIdx].begin();
          histogramIterator !=
          m_bandwidthUtilizationHistogram[stackIdx][accessTypeIdx].end();
          histogramIterator++) {
        totalCycle += histogramIterator->second;
      }
      assert(totalCycle == m_currentCycle);
    }
  }

  printStats();
}

void MyNetwork::initializePacketCounter()
{
  // enum class access_type { pl = 0, ip, hp, max };
  for (unsigned stackIdx = 0; stackIdx < m_numStack; ++stackIdx) {
    m_packetCounters.push_back(vector<unsigned>());
    for (unsigned accessTypeIdx = 0; accessTypeIdx <
        static_cast<unsigned>(access_type::max); accessTypeIdx++) {
      m_packetCounters[stackIdx].push_back(0);
    }
  }
}

void MyNetwork::resetPacketCounter() 
{
  for (auto packetCounterIterator = m_packetCounters.begin();
      packetCounterIterator < m_packetCounters.end(); packetCounterIterator++) {
    for (auto accessTypeIterator = packetCounterIterator->begin();
        accessTypeIterator < packetCounterIterator->end(); accessTypeIterator++) {
      *accessTypeIterator = 0;
    }
  }
}

void MyNetwork::initStats()
{
  for (int stackIdx = 0; stackIdx < m_numStack; ++stackIdx) {
    m_local_accesses.push_back(0);
    m_remote_accesses.push_back(0);
    m_local_access_latencies.push_back(0);
    m_remote_access_latencies.push_back(0);

    // initialization of stat-related variables goes here
    m_latencyMaps.push_back(map<uint64_t, uint64_t>());
    m_per_core_accesses.push_back(vector<uint64_t>());
    m_per_stack_accesses.push_back(vector<uint64_t>());
    for (int j = 0; j < m_numStack; ++j) {
      m_per_core_accesses[stackIdx].push_back(0);
      m_per_stack_accesses[stackIdx].push_back(0);
    }

    m_bandwidthUtilizationHistogram.push_back(vector<map<unsigned, uint64_t>>());
    for (unsigned accessTypeIdx = 0; accessTypeIdx <
        static_cast<unsigned>(access_type::max); accessTypeIdx++) {
      m_bandwidthUtilizationHistogram[stackIdx].push_back(map<unsigned, uint64_t>());
    }
  }
}

void MyNetwork::printStats()
{
  stringstream ss;
  ss << m_name.c_str() << ".stat.out";
  string filename = ss.str();

  ofstream ofs;
  ofs.exceptions(std::ofstream::eofbit | std::ofstream::failbit |
      std::ofstream::badbit);
  ofs.open(filename.c_str(), std::ios_base::out);

  stringstream stat_name;
  uint64_t count;
  for (unsigned stackIdx = 0; stackIdx < m_numStack; ++stackIdx) {
    stat_name.str(string()); stat_name << "local_accesses_core_" << stackIdx;
    count = m_local_accesses[stackIdx];
    writeTo(ofs, m_name, stat_name.str(), count, count);
    stat_name.str(string()); stat_name << "remote_accesses_core_" << stackIdx;
    count = m_remote_accesses[stackIdx];
    writeTo(ofs, m_name, stat_name.str(), count, count);

    for (unsigned j = 0; j < m_numStack; ++j) {
      stat_name.str(string()); stat_name << "requests_from_core_" << stackIdx << "_to_stack_" << j;
      count = m_per_stack_accesses[stackIdx][j];
      writeTo(ofs, m_name, stat_name.str(), count, count);
    }

    for (unsigned j = 0; j < m_numStack; ++j) {
      stat_name.str(string()); stat_name << "responses_from_stack_" << stackIdx << "_to_core_" << j;
      count = m_per_core_accesses[stackIdx][j];
      writeTo(ofs, m_name, stat_name.str(), count, count);
    }

    stat_name.str(string()); stat_name << "local_access_avg_latency_core_" << stackIdx;
    count = (m_local_accesses[stackIdx] > 0) ?
      m_local_access_latencies[stackIdx] / m_local_accesses[stackIdx] : 0;
    writeTo(ofs, m_name, stat_name.str(), count, count);
    stat_name.str(string()); stat_name << "remote_access_avg_latency_core_" << stackIdx;
    count = (m_remote_accesses[stackIdx] > 0) ?
      m_remote_access_latencies[stackIdx] / m_remote_accesses[stackIdx] : 0;
    writeTo(ofs, m_name, stat_name.str(), count, count);

    uint64_t cycle = 0;
    float percent = 0.0f;
    for (unsigned accessTypeIdx = 0; accessTypeIdx <
        static_cast<unsigned>(access_type::max); accessTypeIdx++) {
      for (auto histogramIterator = m_bandwidthUtilizationHistogram[stackIdx][accessTypeIdx].begin();
          histogramIterator != m_bandwidthUtilizationHistogram[stackIdx][accessTypeIdx].end();
          histogramIterator++) {
        stat_name.str(string()); stat_name << access_type_name[accessTypeIdx] << "_PIM_" << stackIdx << "_" << histogramIterator->first;
        cycle = histogramIterator->second;
        percent = 100*((float)cycle)/m_currentCycle;
        writeTo(ofs, m_name, stat_name.str(), cycle, percent);
      }
    }

    ofs << endl;
  }

  ofs.close();
}
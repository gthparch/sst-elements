#include <sstream>
#include <csignal>
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
    MemoryCompInfo *mci = new MemoryCompInfo(i, m_numStack, m_stackSize, m_interleaveSize);
    m_memoryMap[m_lowNetPorts[i]->getId()] = mci;
    cout << *mci << endl;
  }

  for (int i = 0; i < m_numStack; ++i) {
    m_highNetIdxMap[m_highNetPorts[i]->getId()] = i;
    m_lowNetIdxMap[m_lowNetPorts[i]->getId()] = i;
    m_requestQueues.push_back(map<SST::Event*, uint64_t>());
    m_responseQueues.push_back(map<SST::Event*, uint64_t>());
  }
}

MyNetwork::~MyNetwork() 
{
  for (map<LinkId_t, MemoryCompInfo*>::iterator it = m_memoryMap.begin(); it != m_memoryMap.end(); ++it) {
    delete it->second;
  }
}

bool MyNetwork::clockTick(Cycle_t time) 
{
  m_currentCycle++;

  for (auto it = m_responseQueues.begin(); it != m_responseQueues.end(); it++) {
    for (auto it2 = it->begin(); it2 != it->end(); it2++) {
      uint64_t readyCycle = it2->second;
      if (readyCycle < m_currentCycle) { 
        SST::Event* event = it2->first;
        sendResponse(event);
        it->erase(it2);
      }
    }
  }

  for (auto it = m_requestQueues.begin(); it != m_requestQueues.end(); it++) {
    for (auto it2 = it->begin(); it2 != it->end(); it2++) {
      uint64_t readyCycle = it2->second;
      if (readyCycle < m_currentCycle) { 
        SST::Event* event = it2->first;
        sendRequest(event);
        it->erase(it2);
      }
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
    latency = m_local_latency;
    ++m_local_accesses[sourceIdx];
  } else {
    latency = m_remote_latency;
    ++m_remote_accesses[sourceIdx];
  }

  m_latencyMaps[sourceIdx][me->getAddr()] = m_currentCycle;

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
  uint64_t flatAddress = convertToFlatAddress(localAddress, m_memoryMap[me->getDeliveryLink()->getId()]->getRangeStart());

  unsigned latency = 0;
  uint64_t roundTripTime = 0;
  if (sourceIdx == destinationIdx) {
    latency = m_local_latency;
    roundTripTime = m_currentCycle + latency - m_latencyMaps[destinationIdx][flatAddress];
    m_local_access_latencies[destinationIdx] += roundTripTime;
  } else {
    latency = m_remote_latency;
    roundTripTime = m_currentCycle + latency - m_latencyMaps[destinationIdx][flatAddress];
    m_remote_access_latencies[destinationIdx] += roundTripTime;
  }
  m_latencyMaps[destinationIdx].erase(flatAddress);

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
  forwardEvent->setAddr(convertToLocalAddress(me->getAddr(), m_memoryMap[destinationLinkId]->getRangeStart()));
  forwardEvent->setBaseAddr(convertToLocalAddress(me->getAddr(), m_memoryMap[destinationLinkId]->getRangeStart()));

  if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
    m_dbg.debug(_L3_,"SEND %s for 0x%" PRIx64 " currentCycle: %" PRIu64 "\n", CommandString[me->getCmd()], me->getAddr(), m_currentCycle);
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
  forwardEvent->setAddr(convertToFlatAddress(me->getAddr(), m_memoryMap[me->getDeliveryLink()->getId()]->getRangeStart()));
  forwardEvent->setBaseAddr(convertToFlatAddress(me->getAddr(), m_memoryMap[me->getDeliveryLink()->getId()]->getRangeStart()));

  if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
    m_dbg.debug(_L3_,"SEND %s for 0x%" PRIx64 " currentCycle: %" PRIu64 "\n", CommandString[me->getCmd()], me->getAddr(), m_currentCycle);
  }

  destinationLink->send(forwardEvent);
  delete me;
}

void MyNetwork::mapNodeEntry(const string& name, LinkId_t id)
{
  std::map<std::string, LinkId_t>::iterator it = m_nameMap.find(name);
  if (m_nameMap.end() != it) {
    m_dbg.fatal(CALL_INFO, -1, "%s, Error: MyNetwork attempting to map node that has already been mapped\n", getName().c_str());
  }
  m_nameMap[name] = id;
}

LinkId_t MyNetwork::lookupNode(const uint64_t addr) 
{
  LinkId_t destinationLinkId = -1;
  for (map<LinkId_t, MemoryCompInfo*>::iterator it = m_memoryMap.begin(); it != m_memoryMap.end(); ++it) {
    if (it->second->contains(addr)) {
      destinationLinkId = it->first;
    }
  }

  if (destinationLinkId == -1) {
    m_dbg.fatal(CALL_INFO, -1, "%s, Error: MyNetwork lookup for address 0x%" PRIx64 "failed\n", getName().c_str(), addr);
  }

  return destinationLinkId;
}

LinkId_t MyNetwork::lookupNode(const string& name)
{
	map<string, LinkId_t>::iterator it = m_nameMap.find(name);
  if (it == m_nameMap.end()) {
      m_dbg.fatal(CALL_INFO, -1, "%s, Error: MyNetwork lookup of node %s returned no mapping\n", getName().c_str(), name.c_str());
  }
  return it->second;
}

uint64_t MyNetwork::convertToLocalAddress(uint64_t requestedAddress, uint64_t rangeStart)
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
    m_dbg.debug(_L10_, "Converted requested address 0x%" PRIx64 " to local address 0x%" PRIx64 "\n", requestedAddress, localAddress);
  }

  return localAddress;
}

uint64_t MyNetwork::convertToFlatAddress(uint64_t localAddress, uint64_t rangeStart)
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
    m_dbg.debug(_L10_, "Converted local address 0x%" PRIx64 " to flat address 0x%" PRIx64 "\n", localAddress, flatAddress);
  }

  return flatAddress;
}

void MyNetwork::configureParameters(SST::Params& params) 
{
  int debugLevel = params.find_integer("debug_level", 0);
  Output::output_location_t outputLocation = (Output::output_location_t)params.find_integer("debug", 0);

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

  m_local_latency = params.find_integer("local_latency", 1);
  m_remote_latency = params.find_integer("remote_latency", 1);
  string frequency = params.find_string("frequency", "1 GHz");
  
  /** Multiply Frequency times two.  
    * This is because an SST MyNetwork components has 2 SST Links (highNet & LowNet) and thus 
    * it takes a least 2 cycles for any transaction (a real bus should be allowed to have 1 cycle latency).  
    * To overcome this we clock the bus 2x the speed of the cores 
   **/

  UnitAlgebra uA = UnitAlgebra(frequency);
  uA = uA * 2;
  frequency = uA.toString();
  
  Clock::Handler<MyNetwork>* clockHandler = new Clock::Handler<MyNetwork>(this, &MyNetwork::clockTick);
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
		link = configureLink(ln, "1 ps", new Event::Handler<MyNetwork>(this, &MyNetwork::processIncomingRequest));
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
		link = configureLink(ln, "1 ps", new Event::Handler<MyNetwork>(this, &MyNetwork::processIncomingResponse));
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
  printStats();
}

void MyNetwork::initStats()
{
  for (int i = 0; i < m_numStack; ++i) {
    m_local_accesses.push_back(0);
    m_remote_accesses.push_back(0);
    m_local_access_latencies.push_back(0);
    m_remote_access_latencies.push_back(0);

    // initialization of stat-related variables goes here
    m_latencyMaps.push_back(map<uint64_t, uint64_t>());
  }
}

void MyNetwork::printStats()
{
  stringstream ss;
  ss << m_name.c_str() << ".stat.out";
  string filename = ss.str();

  ofstream ofs;
  ofs.exceptions(std::ofstream::eofbit | std::ofstream::failbit | std::ofstream::badbit);
  ofs.open(filename.c_str(), std::ios_base::out);

  stringstream stat_name;
  for (int i = 0; i < m_numStack; ++i) {
    stat_name.str(string()); stat_name << "local_accesses_core_" << i;
    writeTo(ofs, m_name, stat_name.str(), m_local_accesses[i]);
    stat_name.str(string()); stat_name << "remote_accesses_core_" << i;
    writeTo(ofs, m_name, stat_name.str(), m_remote_accesses[i]);
    stat_name.str(string()); stat_name << "local_access_avg_latency_core_" << i;
    writeTo(ofs, m_name, stat_name.str(), m_local_access_latencies[i] / m_local_accesses[i]);
    stat_name.str(string()); stat_name << "remote_access_avg_latency_core_" << i;
    writeTo(ofs, m_name, stat_name.str(), m_remote_access_latencies[i] / m_remote_accesses[i]);
  }

  ofs.close();
}

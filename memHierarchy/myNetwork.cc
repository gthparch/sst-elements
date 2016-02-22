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
    m_requestQueues.push_back(queue<SST::Event*>());
    m_responseQueues.push_back(queue<SST::Event*>());
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
  for (auto it = m_responseQueues.begin(); it != m_responseQueues.end(); it++) {
    std::queue<SST::Event*> &responseQueue = *it;
    while (!responseQueue.empty()) {
      SST::Event* event = responseQueue.front();
      sendResponse(event);
      responseQueue.pop();
    }
  }

  for (auto it = m_requestQueues.begin(); it != m_requestQueues.end(); it++) {
    std::queue<SST::Event*> &requestQueue = *it;
    while (!requestQueue.empty()) {
      SST::Event* event = requestQueue.front();
      sendRequest(event);
      requestQueue.pop();
    }
  }

  return false;
}

void MyNetwork::processIncomingRequest(SST::Event *ev) 
{ 
  MemEvent *me = static_cast<MemEvent*>(ev);
  unsigned idx = m_highNetIdxMap[me->getDeliveryLink()->getId()];
  m_requestQueues[idx].push(ev); 
}

void MyNetwork::processIncomingResponse(SST::Event *ev) 
{ 
  MemEvent *me = static_cast<MemEvent*>(ev);
  unsigned idx = m_lowNetIdxMap[me->getDeliveryLink()->getId()];
  m_responseQueues[idx].push(ev); 
}

void MyNetwork::sendRequest(SST::Event* ev) 
{
  MemEvent *me = static_cast<MemEvent*>(ev);
  if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
    m_dbg.debug(_L3_,"\n\n");
    m_dbg.debug(_L3_,"----------------------------------------------------------------------------------------\n");  //raise(SIGINT);
    m_dbg.debug(_L3_,"Incoming Event. Name: %s, Cmd: %s, Addr: %" PRIx64 ", BsAddr: %" PRIx64 ", Src: %s, Dst: %s, LinkID: %ld \n",
        this->getName().c_str(), CommandString[me->getCmd()], me->getAddr(), me->getBaseAddr(), me->getSrc().c_str(), me->getDst().c_str(), me->getDeliveryLink()->getId());
  }

  LinkId_t destinationLinkId = lookupNode(me->getAddr());
  SST::Link* destinationLink = m_linkIdMap[destinationLinkId];

  MemEvent* forwardEvent = new MemEvent(*me);
  forwardEvent->setAddr(convertToLocalAddress(me->getAddr(), m_memoryMap[destinationLinkId]->getRangeStart()));
  forwardEvent->setBaseAddr(convertToLocalAddress(me->getAddr(), m_memoryMap[destinationLinkId]->getRangeStart()));

  if (DEBUG_ALL || DEBUG_ADDR == forwardEvent->getBaseAddr()) {
    m_dbg.debug(_L3_, "Cmd = %s \n", CommandString[forwardEvent->getCmd()]);
    m_dbg.debug(_L3_, "Src = %s \n", forwardEvent->getSrc().c_str());
    m_dbg.debug(_L3_, "ILK = %ld \n", me->getDeliveryLink()->getId());
    m_dbg.debug(_L3_, "OLK = %ld \n", destinationLink->getId());
  }

  destinationLink->send(forwardEvent);
  delete me;
}

void MyNetwork::sendResponse(SST::Event* ev) 
{
  MemEvent *me = static_cast<MemEvent*>(ev);
  if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
    m_dbg.debug(_L3_,"\n\n");
    m_dbg.debug(_L3_,"----------------------------------------------------------------------------------------\n");  //raise(SIGINT);
    m_dbg.debug(_L3_,"Incoming Event. Name: %s, Cmd: %s, Addr: %" PRIx64 ", BsAddr: %" PRIx64 ", Src: %s, Dst: %s, LinkID: %ld \n",
        this->getName().c_str(), CommandString[me->getCmd()], me->getAddr(), me->getBaseAddr(), me->getSrc().c_str(), me->getDst().c_str(), me->getDeliveryLink()->getId());
  }

  LinkId_t destinationLinkId = lookupNode(me->getDst());
  SST::Link* destinationLink = m_linkIdMap[destinationLinkId];

  MemEvent* forwardEvent = new MemEvent(*me);
  forwardEvent->setAddr(convertToFlatAddress(me->getAddr(), m_memoryMap[me->getDeliveryLink()->getId()]->getRangeStart()));
  forwardEvent->setBaseAddr(convertToFlatAddress(me->getAddr(), m_memoryMap[me->getDeliveryLink()->getId()]->getRangeStart()));

  if (DEBUG_ALL || DEBUG_ADDR == forwardEvent->getBaseAddr()) {
    m_dbg.debug(_L3_, "Cmd = %s \n", CommandString[forwardEvent->getCmd()]);
    m_dbg.debug(_L3_, "Dst = %s \n", forwardEvent->getDst().c_str());
    m_dbg.debug(_L3_, "ILK = %ld \n", me->getDeliveryLink()->getId());
    m_dbg.debug(_L3_, "OLK = %ld \n", destinationLink->getId());
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
    m_dbg.fatal(CALL_INFO, -1, "%s, Error: MyNetwork lookup for address 0x%" PRIx64 "failed\n", addr);
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
  m_dbg.init("--->  ", debugLevel, 0, outputLocation);
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
		link = configureLink(ln, "50 ps", new Event::Handler<MyNetwork>(this, &MyNetwork::processIncomingRequest));
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
		link = configureLink(ln, "50 ps", new Event::Handler<MyNetwork>(this, &MyNetwork::processIncomingResponse));
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
}

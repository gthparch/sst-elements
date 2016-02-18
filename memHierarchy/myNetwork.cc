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

const MyNetwork::key_t MyNetwork::ANY_KEY = pair<uint64_t, int>((uint64_t)-1, -1);

MyNetwork::MyNetwork(ComponentId_t id, Params& params) : Component(id)
{
  configureParameters(params);
  configureLinks();
}

void MyNetwork::processIncomingEvent(SST::Event* ev)
{
  m_eventQueue.push(ev);
}

bool MyNetwork::clockTick(Cycle_t time) 
{
  if (!m_eventQueue.empty()) {
    SST::Event* event = m_eventQueue.front();
    sendSingleEvent(event);
    m_eventQueue.pop();
  }   

  return false;
}

void MyNetwork::sendSingleEvent(SST::Event* ev) 
{
  MemEvent *me = static_cast<MemEvent*>(ev);
  if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
    m_dbg.debug(_L3_,"\n\n");
    m_dbg.debug(_L3_,"----------------------------------------------------------------------------------------\n");  //raise(SIGINT);
    m_dbg.debug(_L3_,"Incoming Event. Name: %s, Cmd: %s, Addr: %" PRIx64 ", BsAddr: %" PRIx64 ", Src: %s, Dst: %s, LinkID: %ld \n",
        this->getName().c_str(), CommandString[me->getCmd()], me->getAddr(), me->getBaseAddr(), me->getSrc().c_str(), me->getDst().c_str(), me->getDeliveryLink()->getId());
  }

  LinkId_t dstLinkId = lookupNode(me->getDst());
  SST::Link* dstLink = m_linkIdMap[dstLinkId];

  MemEvent* forwardEvent = new MemEvent(*me);
  if (DEBUG_ALL || DEBUG_ADDR == forwardEvent->getBaseAddr()) {
    m_dbg.debug(_L3_, "MN Cmd = %s \n", CommandString[forwardEvent->getCmd()]);
    m_dbg.debug(_L3_, "MN Dst = %s \n", forwardEvent->getDst().c_str());
    m_dbg.debug(_L3_, "MN Src = %s \n", forwardEvent->getSrc().c_str());
  }
  dstLink->send(forwardEvent);
  delete me;
}

/*----------------------------------------
 * Helper functions
 *---------------------------------------*/

void MyNetwork::mapNodeEntry(const string& name, LinkId_t id) 
{
	map<string, LinkId_t>::iterator it = m_nameMap.find(name);
	if (it != m_nameMap.end()) {
    m_dbg.fatal(CALL_INFO, -1, "%s, Error: MyNetwork attempting to map node that has already been mapped\n", getName().c_str());
  }
  m_nameMap[name] = id;
}

LinkId_t MyNetwork::lookupNode(const string& name) 
{
	map<string, LinkId_t>::iterator it = m_nameMap.find(name);
  if (it == m_nameMap.end()) {
    m_dbg.fatal(CALL_INFO, -1, "%s, Error: MyNetwork lookup of node %s returned no mapping\n", getName().c_str(), name.c_str());
  }

  return it->second;
}

void MyNetwork::configureParameters(SST::Params& _params) 
{
  int debugLevel = _params.find_integer("debug_level", 0);
  Output::output_location_t outputLocation = (Output::output_location_t)_params.find_integer("debug", 0);
  m_dbg.init("--->  ", debugLevel, 0, outputLocation);
  if (debugLevel < 0 || debugLevel > 10) {
    m_dbg.fatal(CALL_INFO, -1, "Debugging level must be betwee 0 and 10. \n");
  }

  int debugAddr = _params.find_integer("debug_addr", -1);
  if (debugAddr == -1) {
    DEBUG_ADDR = (Addr)debugAddr;
    DEBUG_ALL = true;
  } else {
    DEBUG_ADDR = (Addr)debugAddr;
    DEBUG_ALL = false;
  }

  m_numHighNetPorts = 0;
  m_numLowNetPorts = 0;
  m_maxNumPorts = 1024;
  
  m_latency = _params.find_integer("latency", 1);
  string frequency = _params.find_string("frequency", "1 GHz");
  
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
}

void MyNetwork::configureLinks() 
{
  SST::Link* link;

	for (int i = 0; i < m_maxNumPorts; i++) {
		ostringstream linkName;
		linkName << "high_network_" << i;
		string ln = linkName.str();
		link = configureLink(ln, "50 ps", new Event::Handler<MyNetwork>(this, &MyNetwork::processIncomingEvent));
		if (link) {
      m_highNetPorts.push_back(link);
      m_numHighNetPorts++;
      m_linkIdMap[m_highNetPorts[i]->getId()] = m_highNetPorts[i];
      m_dbg.output(CALL_INFO, "Port %lu = Link %d\n", m_highNetPorts[i]->getId(), i);
    }
  }
  
  for (int i = 0; i < m_maxNumPorts; i++) {
		ostringstream linkName;
		linkName << "low_network_" << i;
		string ln = linkName.str();
		link = configureLink(ln, "50 ps", new Event::Handler<MyNetwork>(this, &MyNetwork::processIncomingEvent));
    if (link) {
      m_lowNetPorts.push_back(link);
      m_numLowNetPorts++;
      m_linkIdMap[m_lowNetPorts[i]->getId()] = m_lowNetPorts[i];
      m_dbg.output(CALL_INFO, "Port %lu = Link %d\n", m_lowNetPorts[i]->getId(), i);
    }
	}

  if (m_numLowNetPorts < 1 || m_numHighNetPorts < 1) {
    m_dbg.fatal(CALL_INFO, -1,"couldn't find number of Ports (numPorts)\n");
  }
}

void MyNetwork::init(unsigned int phase)
{
  SST::Event *ev;

  for (int i = 0; i < m_numHighNetPorts; i++) {
    while ((ev = m_highNetPorts[i]->recvInitData())) {
      MemEvent* memEvent = dynamic_cast<MemEvent*>(ev);
      if (memEvent && memEvent->getCmd() == NULLCMD) {
        mapNodeEntry(memEvent->getSrc(), m_highNetPorts[i]->getId());
        for (int k = 0; k < m_numLowNetPorts; k++) {
          m_lowNetPorts[k]->sendInitData(new MemEvent(*memEvent));
        }
      } else if (memEvent) {
        for (int k = 0; k < m_numLowNetPorts; k++) {
          m_lowNetPorts[k]->sendInitData(new MemEvent(*memEvent));
        }
      }
      delete memEvent;
    }
  }
  
  for (int i = 0; i < m_numLowNetPorts; i++) {
    while ((ev = m_lowNetPorts[i]->recvInitData())) {
      MemEvent* memEvent = dynamic_cast<MemEvent*>(ev);
      if (memEvent && memEvent->getCmd() == NULLCMD) {
        mapNodeEntry(memEvent->getSrc(), m_lowNetPorts[i]->getId());
        for (int i = 0; i < m_numHighNetPorts; i++) {
          m_highNetPorts[i]->sendInitData(new MemEvent(*memEvent));
        }
      }
      delete memEvent;
    }
  }
}

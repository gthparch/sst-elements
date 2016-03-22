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

#define likely(x) __builtin_expect ((x), 1)
#define unlikely(x) __builtin_expect ((x), 0)

MyNetwork::MyNetwork(ComponentId_t id, Params& params) : Component(id)
{
  currentCycle = 0;

  configureParameters(params);
  configureLinks(params);

  for (unsigned i = 0; i < numStack; i++) {
    // PIM Mode
    if (numCore > 1 && numStack > 1 && numCore == numStack) {
      // store local port info
      localPortMap[highNetPorts[i]->getId()] = lowNetPorts[i]->getId();
      localPortMap[lowNetPorts[i]->getId()] = highNetPorts[i]->getId();
      portToStackMap[highNetPorts[i]->getId()] = i;
      portToStackMap[lowNetPorts[i]->getId()] = i;
    }

    // initialize memory component info 
    MemoryCompInfo *mci = new MemoryCompInfo(i, numStack, stackSize, interleaveSize);
    linkToMemoryCompInfoMap[lowNetPorts[i]->getId()] = mci;
    cout << *mci << endl;

    // initialize per-stack request and response queues
    requestQueues.push_back(map<SST::Event*, uint64_t>());
    responseQueues.push_back(map<SST::Event*, uint64_t>());
  }

  initializePacketCounter();
}

MyNetwork::~MyNetwork() 
{
  for_each(linkToMemoryCompInfoMap.begin(), linkToMemoryCompInfoMap.end(), [] (pair<LinkId_t, 
    MemoryCompInfo*>&& it) { delete it.second; });
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
  currentCycle++;
  resetPacketCounter();

  // PIM Mode
  if (likely(numCore > 1 && numStack > 1 && numCore == numStack)) {
    unsigned stackIdx = currentCycle % numStack;
    dbg.debug(_L9_, "currentCycle:%" PRIu64 " start scheduling from PIM[%u]\n", currentCycle, stackIdx);
    for (unsigned counter = 0; counter < numStack; counter++) {
      unsigned numResponseSentThisCycle = 0;
      for (auto& response : responseQueues[stackIdx]) {
        uint64_t readyCycle = response.second;
        if (readyCycle < currentCycle) { 
          SST::Event* ev = response.first;

          // This is for bandwidth control
          MemEvent *me = static_cast<MemEvent*>(ev);
          LinkId_t srcLinkId = me->getDeliveryLink()->getId();
          LinkId_t dstLinkId = lookupNode(me->getDst());
          AccessType accessType = isLocalAccess(srcLinkId, dstLinkId) ?
            AccessType::pl : AccessType::ip;

          // if saturated, don't send any more response to the corresponding
          // accessType
          if (maxPacketPerCycle[accessType] <=
              packetCounters[stackIdx][static_cast<unsigned>(accessType)])
            continue;

          sendResponse(ev);
          responseQueues[stackIdx].erase(responseQueues[stackIdx].find(ev));

          unsigned srcStackIdx = getStackIdx(srcLinkId);
          unsigned dstStackIdx = getStackIdx(dstLinkId);
          if (accessType == AccessType::pl) {
            packetCounters[srcStackIdx][static_cast<unsigned>(accessType)] += 1;
          } else {
            packetCounters[srcStackIdx][static_cast<unsigned>(accessType)] += 1;
            packetCounters[dstStackIdx][static_cast<unsigned>(accessType)] += 1;
          }

          numResponseSentThisCycle++;
        }
      }

      if (numResponseSentThisCycle > 0) {
        dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %2u PIM-local responses from PIM[%u]\n", 
            currentCycle, packetCounters[stackIdx][static_cast<int>(AccessType::pl)], stackIdx);
        dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %2u inter-PIM responses from PIM[%u]\n", 
            currentCycle, packetCounters[stackIdx][static_cast<int>(AccessType::ip)], stackIdx);
        //dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %u host-PIM  responses from PIM[%u]\n", 
        //currentCycle, packetCounters[stackIdx][static_cast<int>(AccessType::hp)], stackIdx);
      }

      stackIdx = (stackIdx == numStack-1) ? 0 : stackIdx+1;
    }

    for (unsigned counter = 0; counter < numStack; counter++) {
      unsigned numRequestSentThisCycle = 0;
      for (auto& request : requestQueues[stackIdx]) {
        uint64_t readyCycle = request.second;
        if (readyCycle < currentCycle) { 
          SST::Event* ev = request.first;
          
          // This is for bandwidth control
          MemEvent *me = static_cast<MemEvent*>(ev);
          LinkId_t srcLinkId = me->getDeliveryLink()->getId();
          LinkId_t dstLinkId = lookupNode(me->getAddr());
          AccessType accessType = isLocalAccess(srcLinkId, dstLinkId) ?
            AccessType::pl : AccessType::ip;

          // if saturated, don't send any more request to the corresponding
          // accessType
          if (maxPacketPerCycle[accessType] <=
              packetCounters[stackIdx][static_cast<unsigned>(accessType)])
            continue;

          sendRequest(ev);
          requestQueues[stackIdx].erase(requestQueues[stackIdx].find(ev));

          unsigned srcStackIdx = getStackIdx(srcLinkId);
          unsigned dstStackIdx = getStackIdx(dstLinkId);
          if (accessType == AccessType::pl) {
            packetCounters[srcStackIdx][static_cast<unsigned>(accessType)] += 1;
          } else {
            packetCounters[srcStackIdx][static_cast<unsigned>(accessType)] += 1;
            packetCounters[dstStackIdx][static_cast<unsigned>(accessType)] += 1;
          }

          numRequestSentThisCycle++;
        }
      }

      if (numRequestSentThisCycle > 0) {
        dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %2u PIM-local requests/responses to/from PIM[%u]\n", 
            currentCycle, packetCounters[stackIdx][static_cast<int>(AccessType::pl)], stackIdx);
        dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %2u inter-PIM requests/responses to/from PIM[%u]\n", 
            currentCycle, packetCounters[stackIdx][static_cast<int>(AccessType::ip)], stackIdx);
        //dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %u host-PIM  requests/responses to/from PIM[%u]\n", 
        //currentCycle, packetCounters[stackIdx][static_cast<int>(AccessType::hp)], stackIdx);
      }

      stackIdx = (stackIdx == numStack-1) ? 0 : stackIdx+1;
    }

    for (unsigned stackIdx = 0; stackIdx < numStack; stackIdx++) {
      for (unsigned accessTypeIdx = 0; accessTypeIdx < static_cast<unsigned>(AccessType::max); accessTypeIdx++) {
        bandwidthUtilizationHistogram[stackIdx][accessTypeIdx][packetCounters[stackIdx][accessTypeIdx]] += 1;
      }
    }
  }
  // 1-to-N or N-to-1 Mode
  else {
    for (unsigned si = 0; si < numStack; si++) {
      unsigned numResponseSentThisCycle = 0;
      for (auto& response : responseQueues[si]) {
        uint64_t readyCycle = response.second;
        if (readyCycle < currentCycle) { 
          SST::Event* ev = response.first;

          AccessType accessType = AccessType::hp;
          // if saturated, don't send any more response
          if (maxPacketPerCycle[accessType] <=
              packetCounters[si][static_cast<unsigned>(accessType)])
            continue;

          sendResponse(ev);
          responseQueues[si].erase(responseQueues[si].find(ev));
          packetCounters[si][static_cast<unsigned>(accessType)] += 1;
          numResponseSentThisCycle++;
        }
      }

      if (numResponseSentThisCycle > 0) {
        dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %u host-PIM responses from PIM[%u]\n", 
          currentCycle, packetCounters[si][static_cast<int>(AccessType::hp)], si);
      }
    }

    for (unsigned si = 0; si < numStack; si++) {
      unsigned numRequestSentThisCycle = 0;
      for (auto& request : requestQueues[si]) {
        uint64_t readyCycle = request.second;
        if (readyCycle < currentCycle) { 
          SST::Event* ev = request.first;

          AccessType accessType = AccessType::hp;
          // if saturated, don't send any more request
          if (maxPacketPerCycle[accessType] <=
              packetCounters[si][static_cast<unsigned>(accessType)])
            continue;

          sendRequest(ev);
          requestQueues[si].erase(requestQueues[si].find(ev));
          packetCounters[si][static_cast<unsigned>(accessType)] += 1;
          numRequestSentThisCycle++;
        }
      }

      if (numRequestSentThisCycle > 0) {
        dbg.debug(_L9_,"currentCycle:%" PRIu64 " sending %u host-PIM  requests/responses to/from PIM[%u]\n", 
          currentCycle, packetCounters[si][static_cast<int>(AccessType::hp)], si);
      }
    }
  }

  return false;
}

void MyNetwork::processIncomingRequest(SST::Event *ev) 
{ 
  MemEvent *me = static_cast<MemEvent*>(ev);

  // PIM Mode
  if (likely(numCore > 1 && numStack > 1 && numCore == numStack)) {
    LinkId_t srcLinkId = me->getDeliveryLink()->getId();
    LinkId_t dstLinkId = lookupNode(me->getAddr());

    bool localAccess = isLocalAccess(srcLinkId, dstLinkId);
    unsigned latency = localAccess ? localLatency : remoteLatency;
    uint64_t readyCycle = currentCycle + latency;

    unsigned srcStackIdx = getStackIdx(srcLinkId);
    unsigned dstStackIdx = getStackIdx(dstLinkId);
    requestQueues[srcStackIdx][ev] = readyCycle; 

    // statistics
    perStackRequests[srcStackIdx][dstStackIdx]++;
    perStackLatencyMaps[srcStackIdx][me->getAddr()] = currentCycle;

    if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
      dbg.debug(_L3_,"RECV %s for 0x%" PRIx64 " src:%s dst:%u "
        "currentCycle:%" PRIu64 " readyCycle:%" PRIu64 "\n",
        CommandString[me->getCmd()], me->getAddr(), me->getSrc().c_str(),
        dstStackIdx, currentCycle, readyCycle);
    }
  }
  // 1-to-N or N-to-1 Mode
  else {
    LinkId_t dstLinkId = lookupNode(me->getAddr());
    unsigned dstStackIdx = getStackIdx(dstLinkId);

    // use <latency> for 1-to-N and N-to-1 configuration
    uint64_t readyCycle = currentCycle + latency;
    requestQueues[dstStackIdx][ev] = readyCycle;

    // statistics
    requests[dstStackIdx]++;
    latencyMap[me->getAddr()] = currentCycle;
  }
}

void MyNetwork::processIncomingResponse(SST::Event *ev) 
{ 
  MemEvent *me = static_cast<MemEvent*>(ev);

  // PIM Mode
  if (likely(numCore > 1 && numStack > 1 && numCore == numStack)) {
    LinkId_t srcLinkId = me->getDeliveryLink()->getId();
    LinkId_t dstLinkId = lookupNode(me->getDst());

    bool localAccess = isLocalAccess(srcLinkId, dstLinkId);
    unsigned latency = localAccess ? localLatency : remoteLatency;
    uint64_t readyCycle = currentCycle + latency;

    unsigned srcStackIdx = getStackIdx(srcLinkId);
    unsigned dstStackIdx = getStackIdx(dstLinkId);
    responseQueues[srcStackIdx][ev] = readyCycle; 

    // statistics
    uint64_t localAddress = me->getAddr();
    uint64_t flatAddress = convertToFlatAddress(localAddress, getMemoryCompInfo(srcLinkId)->getRangeStart());

    uint64_t roundTripTime = 0;
    if (localAccess) {
      roundTripTime = currentCycle + latency - perStackLatencyMaps[dstStackIdx][flatAddress];
      localRequestLatencies[dstStackIdx] += roundTripTime;
    } else {
      roundTripTime = currentCycle + latency - perStackLatencyMaps[dstStackIdx][flatAddress];
      remoteRequestLatencies[dstStackIdx] += roundTripTime;
    }

    perStackLatencyMaps[dstStackIdx].erase(flatAddress);
    perStackResponses[srcStackIdx][dstStackIdx]++;

    if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
      dbg.debug(_L3_,"RECV %s for 0x%" PRIx64 " src:%s dst:%s currentCycle:%" PRIu64 " readyCycle:%" PRIu64 "\n",
        CommandString[me->getCmd()], flatAddress, me->getSrc().c_str(), me->getDst().c_str(), currentCycle, readyCycle);
    }
  }
  // 1-to-N or N-to-1 Mode
  else {
    LinkId_t srcLinkId = me->getDeliveryLink()->getId();
    unsigned srcStackIdx = getStackIdx(srcLinkId);

    // use <latency> for 1-to-N and N-to-1 configuration
    uint64_t readyCycle = currentCycle + latency;
    responseQueues[srcStackIdx][ev] = readyCycle;

    // statistics
    responses[srcStackIdx]++;

    uint64_t localAddress = me->getAddr();
    uint64_t flatAddress = convertToFlatAddress(localAddress, getMemoryCompInfo(srcLinkId)->getRangeStart());
    uint64_t roundTripTime = currentCycle + latency - latencyMap[flatAddress];
    latencies += roundTripTime;
    latencyMap.erase(flatAddress);
  }
}

void MyNetwork::sendRequest(SST::Event* ev) 
{
  MemEvent *me = static_cast<MemEvent*>(ev);

  LinkId_t dstLinkId = lookupNode(me->getAddr());
  SST::Link* dstLink = getLink(dstLinkId);

  MemEvent* forwardEvent = new MemEvent(*me);
  forwardEvent->setAddr(convertToLocalAddress(me->getAddr(), getMemoryCompInfo(dstLinkId)->getRangeStart()));
  forwardEvent->setBaseAddr(convertToLocalAddress(me->getAddr(), getMemoryCompInfo(dstLinkId)->getRangeStart()));

  if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
    unsigned srcStackIdx = getStackIdx(me->getDeliveryLink()->getId());
    unsigned dstStackIdx = getStackIdx(lookupNode(me->getAddr()));
    dbg.debug(_L3_,"SEND %s for 0x%" PRIx64 " at currentCycle: %" PRIu64 " from %u to %u\n", 
        CommandString[me->getCmd()], me->getAddr(), currentCycle, srcStackIdx, dstStackIdx);
  }

  dstLink->send(forwardEvent);
  delete me;
}

void MyNetwork::sendResponse(SST::Event* ev) 
{
  MemEvent *me = static_cast<MemEvent*>(ev);

  LinkId_t srcLinkId = me->getDeliveryLink()->getId();
  LinkId_t dstLinkId = lookupNode(me->getDst());
  SST::Link* dstLink = getLink(dstLinkId);

  MemEvent* forwardEvent = new MemEvent(*me);
  forwardEvent->setAddr(convertToFlatAddress(me->getAddr(), getMemoryCompInfo(srcLinkId)->getRangeStart()));
  forwardEvent->setBaseAddr(convertToFlatAddress(me->getAddr(), getMemoryCompInfo(srcLinkId)->getRangeStart()));

  if (DEBUG_ALL || DEBUG_ADDR == me->getBaseAddr()) {
    unsigned srcStackIdx = getStackIdx(me->getDeliveryLink()->getId());
    unsigned dstStackIdx = getStackIdx(lookupNode(me->getDst()));
    dbg.debug(_L3_,"SEND %s for 0x%" PRIx64 " at currentCycle: %" PRIu64 " from %u to %u\n", 
        CommandString[me->getCmd()], me->getAddr(), currentCycle, srcStackIdx, dstStackIdx);
  }

  dstLink->send(forwardEvent);
  delete me;
}

void MyNetwork::mapNodeEntry(const string& name, LinkId_t id)
{
  auto it = nameToLinkIdMap.find(name);
  if (nameToLinkIdMap.end() != it) {
    dbg.fatal(CALL_INFO, -1, "%s, Error: MyNetwork attempting to map node that has already been mapped\n", 
        getName().c_str());
  }
  nameToLinkIdMap[name] = id;
}

LinkId_t MyNetwork::lookupNode(const uint64_t addr) 
{
  LinkId_t dstLinkId = -1;
  for (auto it : linkToMemoryCompInfoMap) {
    if (it.second->contains(addr)) {
      dstLinkId = it.first;
    }
  }

  if (dstLinkId == -1) {
    dbg.fatal(CALL_INFO, -1, "%s, Error: MyNetwork lookup for address 0x%" PRIx64 "failed\n", 
        getName().c_str(), addr);
  }
  return dstLinkId;
}

LinkId_t MyNetwork::lookupNode(const string& name)
{
	auto it = nameToLinkIdMap.find(name);
  if (it == nameToLinkIdMap.end()) {
      dbg.fatal(CALL_INFO, -1, "%s, Error: MyNetwork lookup of node %s returned no mapping\n", 
          getName().c_str(), name.c_str());
  }
  return it->second;
}

uint64_t MyNetwork::convertToLocalAddress(uint64_t requestedAddress, uint64_t rangeStart)
{
  uint64_t localAddress = 0;

  if (interleaveSize == 0) {
    localAddress = requestedAddress - rangeStart;
  } else {
    uint64_t interleaveStep = interleaveSize * numStack;

    Addr addr = requestedAddress - rangeStart;
    Addr step = addr / interleaveStep;
    Addr offset = addr % interleaveStep;

    localAddress = (step * interleaveSize) + offset;
  }

  if (DEBUG_ALL || requestedAddress == DEBUG_ADDR) {
    dbg.debug(_L10_, "Converted requested address 0x%" PRIx64 " to local address 0x%" PRIx64 "\n", 
        requestedAddress, localAddress);
  }

  return localAddress;
}

uint64_t MyNetwork::convertToFlatAddress(uint64_t localAddress, uint64_t rangeStart)
{
  uint64_t flatAddress = 0;

  if (interleaveSize == 0) {
    flatAddress = localAddress + rangeStart;
  } else {
    uint64_t interleaveStep = interleaveSize * numStack;

    Addr addr = localAddress;
    Addr step = addr / interleaveSize; 
    Addr offset = addr % interleaveSize;

    flatAddress = (step * interleaveStep) + offset;
    flatAddress = flatAddress + rangeStart;
  }

  if (DEBUG_ALL || flatAddress == DEBUG_ADDR) {
    dbg.debug(_L10_, "Converted local address 0x%" PRIx64 " to flat address 0x%" PRIx64 "\n", 
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
  dbg.init(name.c_str(), debugLevel, 0, outputLocation);
  if (debugLevel < 0 || debugLevel > 10) {
    dbg.fatal(CALL_INFO, -1, "Debugging level must be betwee 0 and 10. \n");
  }

  int debugAddr = params.find_integer("debug_addr", -1);
  if (debugAddr == -1) {
    DEBUG_ADDR = (Addr)debugAddr;
    DEBUG_ALL = true;
  } else {
    DEBUG_ADDR = (Addr)debugAddr;
    DEBUG_ALL = false;
  }

  packetSize = params.find_integer("packet_size", 64);

  unsigned PIMLocalBandwidth = params.find_integer("pim_local_bandwidth", 1024);
  unsigned hostPIMBandwidth = params.find_integer("host_pim_bandwidth", 512);
  unsigned interPIMBandwidth = params.find_integer("inter_pim_bandwidth", 128);

  string frequency = params.find_string("frequency", "1 GHz");
  float frequencyInGHz = (float)(UnitAlgebra(frequency).getRoundedValue()) /
    (float)(UnitAlgebra("1GHz").getRoundedValue());

  maxPacketPerCycle[AccessType::pl] = (unsigned)((float)PIMLocalBandwidth / frequencyInGHz / packetSize);
  maxPacketPerCycle[AccessType::ip] = (unsigned)((float)interPIMBandwidth / frequencyInGHz / packetSize);
  maxPacketPerCycle[AccessType::hp] = (unsigned)((float)hostPIMBandwidth / frequencyInGHz / packetSize);

  dbg.debug(_L7_,"Max packets per cycle for PIM-local communication: %u\n", maxPacketPerCycle[AccessType::pl]);
  dbg.debug(_L7_,"Max packets per cycle for inter-PIM communication: %u\n", maxPacketPerCycle[AccessType::ip]);
  dbg.debug(_L7_,"Max packets per cycle for Host-PIM communication: %u\n", maxPacketPerCycle[AccessType::hp]);

  latency = params.find_integer("latency", 1);
  localLatency = params.find_integer("local_latency", 1);
  remoteLatency = params.find_integer("remote_latency", 1);

  Clock::Handler<MyNetwork>* clockHandler = new Clock::Handler<MyNetwork>(this, &MyNetwork::clockTick);
  registerClock(frequency, clockHandler);

  numCore = params.find_integer("num_core", 1);
  numStack = params.find_integer("num_stack", 1);

  // should be 1-to-N, N-to-1 or N-to-N
  if (numCore == numStack) 
    dbg.debug(_L3_,"PIM Mode\n");
  else if (numCore == 1 || numStack == 1) {
    if (numCore == 1) 
      dbg.debug(_L3_,"HOST Mode\n"); 
    if (numStack == 1) 
      dbg.debug(_L3_,"Single Stack Mode\n");
  }
  else assert(0);

  uint64_t stackSize = params.find_integer("stack_size", 0); // in MB
  stackSize = stackSize * (1024*1024ul); // Convert into MBs
  interleaveSize = (uint64_t) params.find_integer("interleave_size", 4096);
}

void MyNetwork::configureLinks(SST::Params& params) 
{
  SST::Link* link;

	for (unsigned i = 0; i < numCore; i++) {
		ostringstream linkName;
		linkName << "high_network_" << i;
		string ln = linkName.str();
    link = configureLink(ln, "1 ps", new Event::Handler<MyNetwork>(this, &MyNetwork::processIncomingRequest));
		if (link) {
      highNetPorts.push_back(link);
      linkIdToLinkMap[highNetPorts[i]->getId()] = highNetPorts[i];
      dbg.output(CALL_INFO, "Port %lu = Link %d\n", highNetPorts[i]->getId(), i);
    }
  }
  
  for (unsigned i = 0; i < numStack; i++) {
		ostringstream linkName;
		linkName << "low_network_" << i;
		string ln = linkName.str();
    link = configureLink(ln, "1 ps", new Event::Handler<MyNetwork>(this, &MyNetwork::processIncomingResponse));
    if (link) {
      lowNetPorts.push_back(link);
      linkIdToLinkMap[lowNetPorts[i]->getId()] = lowNetPorts[i];
      dbg.output(CALL_INFO, "Port %lu = Link %d\n", lowNetPorts[i]->getId(), i);
    }
	}
}

void MyNetwork::init(unsigned int phase)
{
  SST::Event *ev;
  for (unsigned i = 0; i < numCore; i++) {
    while ((ev = highNetPorts[i]->recvInitData())) {
      MemEvent* memEvent = dynamic_cast<MemEvent*>(ev);
      if (memEvent && memEvent->getCmd() == NULLCMD) {
        mapNodeEntry(memEvent->getSrc(), highNetPorts[i]->getId());
      }
      delete memEvent;
    }
  }

  initStats();
}

void MyNetwork::finish()
{
  // sanity check for histogram
  for (unsigned stackIdx = 0; stackIdx < numStack; stackIdx++) {
    for (unsigned accessTypeIdx = 0; accessTypeIdx < static_cast<unsigned>(AccessType::max); accessTypeIdx++) {
      uint64_t totalCycle = 0;
      for (auto& histogramIterator : bandwidthUtilizationHistogram[stackIdx][accessTypeIdx]) {
        totalCycle += histogramIterator.second;
      }
      assert(totalCycle == currentCycle);
    }
  }

  printStats();
}

void MyNetwork::initializePacketCounter()
{
  // enum class AccessType { pl = 0, ip, hp, max };
  for (unsigned stackIdx = 0; stackIdx < numStack; ++stackIdx) {
    packetCounters.push_back(vector<unsigned>());
    for (unsigned accessTypeIdx = 0; accessTypeIdx < static_cast<unsigned>(AccessType::max); accessTypeIdx++) {
      packetCounters[stackIdx].push_back(0);
    }
  }
}

void MyNetwork::resetPacketCounter() 
{
  for (auto& stackPacketCounters : packetCounters) {
    for (auto& packetCounter : stackPacketCounters) {
      packetCounter = 0;
    }
  }
}

void MyNetwork::initStats()
{
  // PIM Mode
  if (likely(numCore > 1 && numStack > 1 && numCore == numStack)) {
    for (unsigned si = 0; si < numStack; si++) {
      localRequestLatencies.push_back(0);
      remoteRequestLatencies.push_back(0);

      perStackLatencyMaps.push_back(map<uint64_t, uint64_t>());
      perStackResponses.push_back(vector<uint64_t>());
      perStackRequests.push_back(vector<uint64_t>());
      for (int j = 0; j < numStack; ++j) {
        perStackResponses[si].push_back(0);
        perStackRequests[si].push_back(0);
      }

      bandwidthUtilizationHistogram.push_back(vector<map<unsigned, uint64_t>>());
      for (unsigned accessTypeIdx = 0; accessTypeIdx < static_cast<unsigned>(AccessType::max); accessTypeIdx++) {
        bandwidthUtilizationHistogram[si].push_back(map<unsigned, uint64_t>());
      }
    }
  }
  // 1-to-N or N-to-1 Mode
  else {
    latencies = 0;
    for (unsigned si = 0; si < numStack; si++) {
      // initialization of stat-related variables goes here
      requests.push_back(0);
      responses.push_back(0);
    }
  }
}

void MyNetwork::printStats()
{
  string componentName = this->Component::getName();

  stringstream ss;
  ss << componentName.c_str() << ".stat.out";
  string filename = ss.str();

  ofstream ofs;
  ofs.exceptions(std::ofstream::eofbit | std::ofstream::failbit |
      std::ofstream::badbit);
  ofs.open(filename.c_str(), std::ios_base::out);

  stringstream statisticName;
  uint64_t count;
  for (unsigned stackIdx = 0; stackIdx < numStack; ++stackIdx) {
    uint64_t totalRequests = 0;
    for_each(perStackRequests[stackIdx].begin(), perStackRequests[stackIdx].end(), 
        [&totalRequests] (uint64_t req) { totalRequests += req; });

    dbg.debug(_L7_, "PIM[%u] total requests: %" PRIu64 "\n", stackIdx, totalRequests);

    statisticName.str(string()); statisticName << "local_request_pim_" << stackIdx;
    uint64_t localRequests = perStackRequests[stackIdx][stackIdx];
    writeTo(ofs, componentName, statisticName.str(), localRequests, localRequests);

    dbg.debug(_L7_, "PIM[%u] local requests: %" PRIu64 "\n", stackIdx, localRequests);

    statisticName.str(string()); statisticName << "remote_request_pim_" << stackIdx;
    uint64_t remoteRequests = totalRequests - localRequests;
    writeTo(ofs, componentName, statisticName.str(), remoteRequests, remoteRequests);

    dbg.debug(_L7_, "PIM[%u] local requests: %" PRIu64 "\n", stackIdx, remoteRequests);

    for (unsigned j = 0; j < numStack; ++j) {
      statisticName.str(string()); statisticName << "requests_from_pim_" << stackIdx << "_to_pim_" << j;
      count = perStackRequests[stackIdx][j];
      writeTo(ofs, componentName, statisticName.str(), count, count);

      dbg.debug(_L7_, "requests from PIM[%u] to PIM[%u]: %" PRIu64 "\n", stackIdx, j, count);
    }

    for (unsigned j = 0; j < numStack; ++j) {
      statisticName.str(string()); statisticName << "responses_from_pim_" << stackIdx << "_to_pim_" << j;
      count = perStackResponses[stackIdx][j];
      writeTo(ofs, componentName, statisticName.str(), count, count);

      dbg.debug(_L7_, "responses from PIM[%u] to PIM[%u]: %" PRIu64 "\n", stackIdx, j, count);
    }

    statisticName.str(string()); statisticName << "local_request_avg_latency_pim_" << stackIdx;
    count = (localRequests > 0) ? localRequestLatencies[stackIdx] / localRequests : 0;
    writeTo(ofs, componentName, statisticName.str(), count, count);

    dbg.debug(_L7_, "PIM[%u] local requests average latency: %" PRIu64 "\n", stackIdx, count);

    statisticName.str(string()); statisticName << "remote_request_avg_latency_pim_" << stackIdx;
    count = (remoteRequests > 0) ? remoteRequestLatencies[stackIdx] / remoteRequests : 0;
    writeTo(ofs, componentName, statisticName.str(), count, count);

    dbg.debug(_L7_, "PIM[%u] remote requests average latency: %" PRIu64 "\n", stackIdx, count);

    uint64_t cycle = 0;
    float percent = 0.0f;
    for (unsigned accessTypeIdx = 0; accessTypeIdx < static_cast<unsigned>(AccessType::max); accessTypeIdx++) {
      for (auto& histogramIterator : bandwidthUtilizationHistogram[stackIdx][accessTypeIdx]) {
        statisticName.str(string()); 
        statisticName << AccessTypeName[accessTypeIdx] << "_PIM_" << stackIdx << "_" << histogramIterator.first;
        cycle = histogramIterator.second;
        percent = 100*((float)cycle)/currentCycle;
        writeTo(ofs, componentName, statisticName.str(), cycle, percent);
      }
    }

    ofs << endl;
  }

  ofs.close();
}

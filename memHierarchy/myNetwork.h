#ifndef SST_MEMHIERARCHY_MY_NETWORK_H
#define SST_MEMHIERARCHY_MY_NETWORK_H

#include <boost/serialization/deque.hpp>
#include <boost/serialization/map.hpp>
#include <boost/algorithm/string.hpp>

#include <fstream>
#include <queue>
#include <map>

#include <sst/core/event.h>
#include <sst/core/sst_types.h>
#include <sst/core/component.h>
#include <sst/core/link.h>
#include <sst/core/timeConverter.h>
#include <sst/core/output.h>

#include "sst/elements/memHierarchy/memEvent.h"
#include "sst/elements/memHierarchy/util.h"

using namespace std;

namespace SST { namespace MemHierarchy {

class MyNetwork : public SST::Component 
{
public:
  MyNetwork(SST::ComponentId_t id, SST::Params& params);
  virtual ~MyNetwork();
  virtual void init(unsigned int phase);
  virtual void finish();

private:
  class MemoryCompInfo 
  {
  public: 
    MemoryCompInfo(unsigned idx, unsigned numStack, uint64_t stackSize, uint64_t interleaveSize) 
    {
      m_rangeStart = interleaveSize * idx;
      m_rangeEnd = stackSize * numStack - interleaveSize * (numStack - idx - 1);
      m_interleaveSize = interleaveSize;
      m_interleaveStep = interleaveSize * numStack;
    }
    
    bool contains(uint64_t addr) const 
    {
      if (addr < m_rangeStart || m_rangeEnd <= addr) return false;
      if (m_interleaveSize == 0) return true;
      uint64_t offset = (addr - m_rangeStart) % m_interleaveStep;
      return (offset < m_interleaveSize);
    }

    bool operator<(const MemoryCompInfo &m) const 
    {
      return (m_rangeStart < m.m_rangeStart);
    }

    friend ostream& operator<<(ostream& os, const MemoryCompInfo& mci)
    {
      os << "range (" << mci.m_rangeStart << ", " << mci.m_rangeEnd << ") interleaveSize: " << mci.m_interleaveSize;
      return os;
    }

    uint64_t getRangeStart(void) const { return m_rangeStart; }
    uint64_t getRangeEnd(void) const { return m_rangeEnd; }
    uint64_t getInterleaveSize(void) const { return m_interleaveSize; }
    uint64_t getInterleaveStep(void) const { return m_interleaveStep; }

  private:
    uint64_t m_rangeStart;
    uint64_t m_rangeEnd;
    uint64_t m_interleaveSize;
    uint64_t m_interleaveStep;
  };

private:
  void configureParameters(SST::Params&);
  void configureLinks();

  void processIncomingRequest(SST::Event *ev);
  void processIncomingResponse(SST::Event *ev);  

  void sendRequest(SST::Event *ev);
  void sendResponse(SST::Event *ev);
  
  bool clockTick(Cycle_t);
  
  void mapNodeEntry(const string&, LinkId_t);
  LinkId_t lookupNode(const uint64_t);
  LinkId_t lookupNode(const string& name);

  uint64_t convertToLocalAddress(uint64_t requestedAddress, uint64_t rangeStart);
  uint64_t convertToFlatAddress(uint64_t localAddress, uint64_t rangeStart);

  void initStats();
  void printStats();

  // Helper function for printing statistics in MacSim format
  template<typename T>
  void writeTo(ofstream &stream, string prefix, string name, T count)
  {
    #define FILED1_LENGTH 45
    #define FILED2_LENGTH 20
    #define FILED3_LENGTH 30

    stream.setf(ios::left, ios::adjustfield);
    string capitalized_prefixed_name = boost::to_upper_copy(prefix + "_" + name);
    stream << setw(FILED1_LENGTH) << capitalized_prefixed_name;
    stream.setf(ios::right, ios::adjustfield);
    stream << setw(FILED2_LENGTH) << count << setw(FILED3_LENGTH) << count << endl << endl;
  }

private:
  Output m_dbg;
  bool DEBUG_ALL;
  Addr DEBUG_ADDR;

  unsigned m_packetSize;

  unsigned m_maxPacketPIMInternal;
  unsigned m_maxPacketInterPIM;
  unsigned m_maxPacketHostPIM;

  vector<map<SST::Event*, uint64_t>> m_requestQueues;
  vector<map<SST::Event*, uint64_t>> m_responseQueues;

  unsigned m_localLatency;
  unsigned m_remoteLatency;

  vector<SST::Link*> m_highNetPorts;
  vector<SST::Link*> m_lowNetPorts;
  map<string, LinkId_t> m_nameMap;
  map<LinkId_t, SST::Link*> m_linkIdMap;
  map<LinkId_t, MemoryCompInfo*> m_memoryMap;
  map<LinkId_t, unsigned> m_highNetIdxMap;
  map<LinkId_t, unsigned> m_lowNetIdxMap;

  unsigned m_numStack;
  uint64_t m_interleaveSize;
  uint64_t m_stackSize;

  uint64_t m_currentCycle;
  string m_name;

  vector<map<uint64_t, uint64_t>> m_latencyMaps;

  vector<uint64_t> m_local_accesses;
  vector<uint64_t> m_remote_accesses;
  vector<uint64_t> m_local_access_latencies;
  vector<uint64_t> m_remote_access_latencies;
  vector<vector<uint64_t>> m_per_core_accesses; // count the number of requests from each core from each stack's point of view
  vector<vector<uint64_t>> m_per_stack_accesses; // count the number of requests sent to each stack from each core's point of view
};

}}
#endif /* SST_MEMHIERARCHY_MY_NETWORK_H */

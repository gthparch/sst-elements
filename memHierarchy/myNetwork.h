#ifndef SST_MEMHIERARCHY_MY_NETWORK_H
#define SST_MEMHIERARCHY_MY_NETWORK_H

#include <boost/serialization/deque.hpp>
#include <boost/serialization/map.hpp>

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

    friend std::ostream& operator<<(std::ostream& os, const MemoryCompInfo& mci) 
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
  
  void mapNodeEntry(const std::string&, LinkId_t);
  LinkId_t lookupNode(const uint64_t);
  LinkId_t lookupNode(const std::string& name);

  uint64_t convertToLocalAddress(uint64_t requestedAddress, uint64_t rangeStart);
  uint64_t convertToFlatAddress(uint64_t localAddress, uint64_t rangeStart);

private:
  Output m_dbg;
  bool DEBUG_ALL;
  Addr DEBUG_ADDR;

  std::vector<std::map<SST::Event*, uint64_t>> m_requestQueues;
  std::vector<std::map<SST::Event*, uint64_t>> m_responseQueues;

  unsigned m_local_latency;
  unsigned m_remote_latency;

  std::vector<SST::Link*> m_highNetPorts;
  std::vector<SST::Link*> m_lowNetPorts;
  std::map<string, LinkId_t> m_nameMap;
  std::map<LinkId_t, SST::Link*> m_linkIdMap;
  std::map<LinkId_t, MemoryCompInfo*> m_memoryMap;
  std::map<LinkId_t, unsigned> m_highNetIdxMap;
  std::map<LinkId_t, unsigned> m_lowNetIdxMap;

  unsigned m_numStack;
  uint64_t m_interleaveSize;
  uint64_t m_stackSize;

  uint64_t m_currentCycle;
};

}}
#endif /* SST_MEMHIERARCHY_MY_NETWORK_H */

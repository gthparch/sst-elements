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
  typedef MemEvent::id_type key_t;
  static const key_t ANY_KEY;
  static const char BUS_INFO_STR[];
  
  MyNetwork(SST::ComponentId_t id, SST::Params& params);
  virtual void init(unsigned int phase);

private:
  /** Adds event to the incoming event queue.  Reregisters clock if needed */
  void processIncomingEvent(SST::Event *ev);
  
  /** Send event to a single destination */
  void sendSingleEvent(SST::Event *ev);
  
  /**  Clock Handler */
  bool clockTick(Cycle_t);
  
  /** Configure MyNetwork objects with the appropriate parameters */
  void configureParameters(SST::Params&);
  void configureLinks();
  
  void mapNodeEntry(const std::string&, LinkId_t);
  LinkId_t lookupNode(const std::string&);

  Output m_dbg;
  bool DEBUG_ALL;
  Addr DEBUG_ADDR;

  int m_numHighNetPorts;
  int m_numLowNetPorts;
  int m_maxNumPorts;
  int m_latency;

  std::vector<SST::Link*> m_highNetPorts;
  std::vector<SST::Link*> m_lowNetPorts;
  std::map<string, LinkId_t> m_nameMap;
  std::map<LinkId_t, SST::Link*> m_linkIdMap;
  std::queue<SST::Event*> m_eventQueue;
};

}}
#endif /* SST_MEMHIERARCHY_MY_NETWORK_H */

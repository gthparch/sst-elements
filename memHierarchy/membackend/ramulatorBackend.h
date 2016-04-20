#ifndef _H_SST_MEMH_RAMULATOR_BACKEND
#define _H_SST_MEMH_RAMULATOR_BACKEND

#include "membackend/memBackend.h"
#include "ramulator/src/Config.h"
#include "ramulator/src/Memory.h"

using namespace std;
using namespace SST;
using namespace SST::MemHierarchy;
using namespace ramulator;

class Ramulator : public MemBackend {
public:
    Ramulator();
    Ramulator(SST::Component *comp, SST::Params &params);
    ~Ramulator();

    void initialize(string config, int cacheline);

    virtual bool issueRequest(DRAMReq *req);
    virtual void clock();
    virtual void finish();

public:
    ramulator::MemoryBase *mem;

private:
    ramulator::Config configs;
    double tCK;
    map<uint64_t, deque<DRAMReq*> > memReqs;
}; // class Ramulator

#endif

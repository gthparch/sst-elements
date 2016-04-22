#include <map>
#include <string>
#include <sst_config.h>

#include "ramulator/src/Request.h"
#include "ramulator/src/MemoryFactory.h"
#include "ramulator/src/HBM.h"
#include "ramulator/src/Statistics.h"
#include "ramulatorBackend.h"

Ramulator::Ramulator(Component *comp, Params &params) : MemBackend(comp, params)
{
    string config = params.find_string("config", NO_STRING_DEFINED);
    if (NO_STRING_DEFINED == config)
        ctrl->dbg.fatal(CALL_INFO, -1, "Model must define a 'config' file parameter\n");

    unsigned cacheline = (unsigned)params.find_integer("cacheline", 64);

    Stats::statlist.output(comp->getName() + ".HBM.stat.out");
    initialize(config, cacheline);
}

Ramulator::~Ramulator() 
{
    delete mem;
}

void Ramulator::initialize(string config, int cacheline)
{
    configs = ramulator::Config(config);
    configs.set_core_num(1); // TODO
    mem = MemoryFactory<HBM>::create(configs, cacheline);
    tCK = mem->clk_ns();
}

bool Ramulator::issueRequest(DRAMReq *req)
{
    uint64_t addr = req->baseAddr_ + req->amtInProcess_;
    ramulator::Request::Type type = req->isWrite_ ? Request::Type::WRITE : Request::Type::READ;

    ctrl->dbg.debug(_L10_, "Received Memory Request for address 0x%" PRIx64 "\n", addr);

    auto complete = [this](Request& r) {
        deque<DRAMReq *> &reqs = memReqs[r.addr];
        ctrl->dbg.debug(_L10_, "Memory Request for address 0x%" PRIx64 " Finished [%zu reqs]\n", r.addr, reqs.size());
        assert(reqs.size());
        DRAMReq *req = reqs.front();
        reqs.pop_front();
        if (0 == reqs.size()) memReqs.erase(r.addr);
        ctrl->handleMemResponse(req);
    };

    Request rReq(addr, type, complete);
    bool stall = !mem->send(rReq);
    if (stall) return false;

    ctrl->dbg.debug(_L10_, "Issued transaction for address 0x%" PRIx64 "\n", (Addr)addr);
    memReqs[addr].push_back(req);
    return true;
}

void Ramulator::clock()
{
    mem->tick();
    Stats::curTick++;
}

void Ramulator::finish()
{
    mem->finish();
    Stats::statlist.printall();
}

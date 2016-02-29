// Copyright 2009-2015 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2015, Sandia Corporation
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>
#include <sst/core/serialization.h>
#include <sst/core/element.h>
#include <sst/core/component.h>
#include <sst/core/subcomponent.h>
#include <sst/core/output.h>
#include <sst/core/params.h>

#include "Vault.h"

using namespace std;
using namespace SST;

extern "C" {
  Component* create_VaultSimC( SST::ComponentId_t id,  SST::Params& params );
  Component* create_logicLayer( SST::ComponentId_t id,  SST::Params& params );
  Component* create_quad( SST::ComponentId_t id,  SST::Params& params );
}

const char *memEventList[] = {
  "MemEvent",
  NULL
};

// ------------------------------------------------------- Logiclayer -------------------------------------------------------------//
static const ElementInfoPort logicLayer_ports[] = {
  {"bus_%(vaults/quad)d", "Link to the individual memory vaults/quad. Bus ID Should match ID of Quad", memEventList},
  {"toCPU", "Connection towards the processor (directly to the processor, or down the chain in the direction of the processor)", memEventList},    
  {"toMem", "If 'terminal' is 0 (i.e. this is not the last cube in the chain) then this port connects to the next cube.", memEventList},
  {"toXBar_%(quad)d", "Link to Quad XBar shared between Quads", memEventList},
  {NULL, NULL, NULL}
};

static const ElementInfoStatistic logicLayer_statistics[] = {
  { "Total_memory_ops_processed", "Total memory ops processed", "reqs", 1},
  { "HMC_ops_processed", "Total HMC ops processed", "reqs", 1},
  { "Total_HMC_candidate_processed", "Total HMC Candidate (instruction that might be able to use HMC) ops processed", "reqs", 1},
  { "Total_HMC_transactions_processed", "Total HMC Transaction ops processed", "reqs", 1},
  { "Req_recv_from_CPU", "Bandwidth used (receives from the CPU by the LL) per cycle (in messages)", "reqs", 1},
  { "Req_send_to_CPU", "Bandwidth used (sends from the CPU by the LL) per cycle (in messages)", "reqs", 1},
  { "Req_recv_from_Mem", "Bandwidth used (receives from other memories by the LL) per cycle (in messages)", "reqs", 1},
  { "Req_send_to_Mem", "Bandwidth used (sends from other memories by the LL) per cycle (in messages)", "reqs", 1},
  { NULL, NULL, NULL, 0 }
};

static const ElementInfoParam logicLayer_params[] = {
  {"clock",                           "Logic Layer Clock Rate." , "2.0 Ghz"},
  {"llID",                            "Logic Layer ID (Unique id within chain)"},
  {"cacheLineSize",                   "Optional, used to find mapping requests to Vaults", "64"},
  {"have_quad",                       "If logicLayer has quads set to 1", "0"},
  {"num_vault_per_quad",              "Number of Vaults per quad", "4"},
  {"req_LimitPerCycle",               "Number of memory events which can be processed per cycle per link."},
  {"LL_MASK",                         "Bitmask to determine 'ownership' of an address by a cube. A cube 'owns' an address if ((((addr >> LL_SHIFT) & LL_MASK) == llID) || (LL_MASK == 0)). LL_SHIFT is set in vaultGlobals.h and is 8 by default."},
  {"terminal",                        "Is this the last cube in the chain?"},
  {"vaults",                          "Number of vaults per cube."},
  {"debug",                           "0 (default): No debugging, 1: STDOUT, 2: STDERR, 3: FILE.", "0"},
  {"debug_level",                     "debug verbosity level (0-10)"},
  {"statistics_format",               "Optional, Stats format. Options: 0[default], 1[MacSim]", "0"},
  { NULL, NULL }
};

// --------------------------------------------------------- Quad ----------------------------------------------------------------//
static const ElementInfoPort quad_ports[] = {
  {"bus_%(vaults)d", "Link to the individual memory vaults", memEventList},
  {"toLogicLayer", "Link to LogicLayer to answer requests", memEventList},
  {"toXBar", "Link to LogicLayer XBar shared between Quads", memEventList},
  {NULL, NULL, NULL}
};

static const ElementInfoStatistic quad_statistics[] = {
  {"Total_transactions_recv",           "Total transactions received", "reqs", 1},
  {"Transactions_sent_to_xbar",         "Total transactions sent back to Xbar due to quadID not matched", "reqs", 1},
  {"Transactions_recv_from_vaults",     "Total transactions sent back to Xbar due to quadID not matched", "reqs", 1},
  { NULL, NULL, NULL, 0 }
};

static const ElementInfoParam quad_params[] = {
  {"clock",                           "Quad Clock Rate", "2.0 Ghz"},
  {"quadID",                          "Quad ID"},
  {"num_vault_per_quad",              "Number of Vaults per quad", "4"},
  {"num_all_vaults",                  "Number of all vaults in this HMC, needed for address mapping"},
  {"cacheLineSize",                   "Optional, used to find mapping requests to Vaults", "64"},
  {"debug",                           "0 (default): No debugging, 1: STDOUT, 2: STDERR, 3: FILE.", "0"},
  {"debug_level",                     "debug verbosity level (0-10)"},
  {"statistics_format",               "Optional, Stats format. Options: 0[default], 1[MacSim]", "0"},
  { NULL, NULL }
};

// ------------------------------------------------------- VaultSimC -------------------------------------------------------------//
static const ElementInfoParam VaultSimC_params[] = {
  {"clock",                           "Vault Clock Rate.", "1.0 Ghz"},
  {"cacheLineSize",                   "Optional, used to strip address bits for DRAMSim2", "64"},
  {"debug",                           "VaultSimC debug: 0 (default): No debugging, 1: STDOUT, 2: STDERR, 3: FILE.", "0"},
  {"debug_level",                     "VaultSimC debug verbosity level (0-10)"},
  {"statistics_format",               "Optional, Stats format. Options: 0[default], 1[MacSim]", "0"},
  {"num_bit_shift_address_dram",      "Number of bits that address is shifted before sending to to DRAMSim. Must be set larger than 0 if using multiple vault/quad.", 0},
  {"vault.id",                        "Unique ID number of Vault", NULL},
  {"vault.device_ini",                "Name of DRAMSim Device configuration file", NULL},
  {"vault.debug",                     "Vault debug: 0 (default): No debugging, 1: STDOUT, 2: STDERR, 3: FILE.", 0},
  {"vault.debug_level",               "Vault debug verbosity level (0-10)", 0},
  {"vault.debug_OnFlyHmcOps",         "Vault debugging for hmc queue"},
  {"vault.debug_OnFlyHmcOpsThresh",   "Vault debugging for hmc queue threshold value"},
  {"vault.system_ini",                "Name of DRAMSim System configuration file", NULL},
  {"vault.pwd",                       "Path of DRAMSim input files (ignored if file name is an absolute path)", NULL},
  {"vault.logfile",                   "DRAMSim output path", NULL},
  {"vault.mem_size",                  "Size of total physical memory in MB", "0"},
  {"vault.statistics_format",         "Optional, Stats format. Options: 0[default], 1[MacSim]", "0"},
  {"vault.num_dram_banks_per_rank",   "Number of Banks per Rank in a single DRAM module, should follow ini files. numChannel should be 1.", NULL},
  {"vault.HMCCost_LogicalOps",        "Compute Cost of Logical Ops in Vault's cycles", "0"},
  {"vault.HMCCost_CASOps",            "Compute Cost of CAS Ops in Vault's cycles", "0"},
  {"vault.HMCCost_CompOps",           "Compute Cost of Compare Ops in Vault's cycles", "0"},
  {"vault.HMCCost_Add8",              "Compute Cost of Add 8b Ops in Vault's cycles", "0"},
  {"vault.HMCCost_Add16",             "Compute Cost of Add 16b Ops in Vault's cycles", "0"},
  {"vault.HMCCost_AddDual",           "Compute Cost of Add Dual Ops in Vault's cycles", "0"},
  {"vault.HMCCost_FPAdd",             "Compute Cost of FP Add in Vault's cycles", "0"},
  {"vault.HMCCost_Swap",              "Compute Cost of Swap Op in Vault's cycles", "0"},
  {"vault.HMCCost_BitW",              "Compute Cost of Bit W Op in Vault's cycles", "0"},
  { NULL, NULL }
};

static const ElementInfoPort VaultSimC_ports[] = {
  {"bus", "Link to the logic layer", memEventList},
  {NULL, NULL, NULL}
};

static const ElementInfoStatistic VaultSimC_statistics[] = {
  { NULL, NULL, NULL, 0 }
};

// ------------------------------------------------------- Components -------------------------------------------------------------//
static const ElementInfoComponent components[] = {
  { "logicLayer",
    "Logic Layer Component",
    NULL,
    create_logicLayer,
    logicLayer_params,
    logicLayer_ports,
    COMPONENT_CATEGORY_MEMORY,
    logicLayer_statistics
  },
  { "quad",
    "Quads for LogicLayer Component",
    NULL,
    create_quad,
    quad_params,
    quad_ports,
    COMPONENT_CATEGORY_MEMORY,
    quad_statistics
  },
  { "VaultSimC",
    "Vault Component",
    NULL,
    create_VaultSimC,
    VaultSimC_params,
    VaultSimC_ports,
    COMPONENT_CATEGORY_MEMORY,
    VaultSimC_statistics
  },
  { NULL, NULL, NULL, NULL }
};

#if defined(HAVE_LIBDRAMSIM)
static SubComponent* create_Vault(Component* comp, Params& params) {
    return new Vault(comp, params);
}

// ------------------------------------------------------- Vault -------------------------------------------------------------//
static const ElementInfoParam Vault_params[] = {
    {"id",                          "Unique ID number of Vault", NULL},
    {"device_ini",                  "Name of DRAMSim Device configuration file", NULL},
    {"debug",                       "Vault debug: 0 (default): No debugging, 1: STDOUT, 2: STDERR, 3: FILE.", 0},
    {"debug_level",                 "Vault debug verbosity level (0-10)", 0},
    {"debug_OnFlyHmcOps",           "Vault debugging for hmc queue"},
    {"debug_OnFlyHmcOpsThresh",     "Vault debugging for hmc queue threshold value"},
    {"system_ini",                  "Name of DRAMSim System configuration file", NULL},
    {"pwd",                         "Path of DRAMSim input files (ignored if file name is an absolute path)", NULL},
    {"logfile",                     "DRAMSim output path", NULL},
    {"mem_size",                    "Size of total physical memory in MB", "0"},
    {"statistics_format",           "Optional, Stats format. Options: 0[default], 1[MacSim]", "0"},
    {"num_dram_banks_per_rank",     "Number of Banks per Rank in a single DRAM module, should follow ini files. numChannel should be 1. Used for atomic instructions bank locking.", NULL},
    {"HMCCost_LogicalOps",          "Compute Cost of Logical Ops in Vault's cycles", "0"},
    {"HMCCost_CASOps",              "Compute Cost of CAS Ops in Vault's cycles", "0"},
    {"HMCCost_CompOps",             "Compute Cost of Compare Ops in Vault's cycles", "0"},
    {"HMCCost_Add8",                "Compute Cost of Add 8b Ops in Vault's cycles", "0"},
    {"HMCCost_Add16",               "Compute Cost of Add 16b Ops in Vault's cycles", "0"},
    {"HMCCost_AddDual",             "Compute Cost of Add Dual Ops in Vault's cycles", "0"},
    {"HMCCost_FPAdd",               "Compute Cost of FP Add in Vault's cycles", "0"},
    {"HMCCost_Swap",                "Compute Cost of Swap Op in Vault's cycles", "0"},
    {"HMCCost_BitW",                "Compute Cost of Bit W Op in Vault's cycles", "0"},
    {NULL, NULL, NULL}
};

static const ElementInfoStatistic Vault_statistics[] = {
  { "Total_transactions",           "Total transactions", "reqs", 1},
  { "Total_hmc_ops",                "Total hmc ops", "reqs", 1},
  { "Total_non_hmc_ops",            "Total non hmc ops", "reqs", 1},
  { "Total_candidate_hmc_ops",      "Total candidate hmc ops", "reqs", 1},
  { "Total_hmc_confilict_happened", "Total hmc conflict happened", "reqs", 1},
  { "Total_non_hmc_read",           "Total non hmc read", "reqs", 1},
  { "Total_non_hmc_write",          "Total non hmc write", "reqs", 1},
  { "Hmc_ops_total_latency",        "Hmc ops total latency", "cycles", 1},
  { "Hmc_ops_issue_latency",        "Hmc ops issue latency", "cycles", 1},
  { "Hmc_ops_read_latency",         "Hmc ops read latency", "cycles", 1},
  { "Hmc_ops_write_latency",        "Hmc ops write latency", "cycles", 1},
  { NULL, NULL, NULL, 0 }
};

// ------------------------------------------------------- Subcomponents -------------------------------------------------------------//

static const ElementInfoSubComponent subcomponents[] = {
    {   
        "Vault",                          /*!< Name of the subcomponent. */
        "DRAMSim-based Vault timings",    /*!< Brief description of the subcomponent. */
        NULL,                             /*!< Pointer to a function that will print additional documentation (optional) */
        create_Vault,                     /*!< Pointer to a function to initialize a subcomponent instance. */
        Vault_params,                     /*!< List of parameters which are used by this subcomponent. */
        Vault_statistics,                 /*!< List of statistics supplied by this subcomponent. */
        "SST::VaultSimC"                  /*!< Name of SuperClass which for this subcomponent can be used. */
    },
    {NULL, NULL, NULL, NULL, NULL, NULL}
};
#endif

extern "C" {
  ElementLibraryInfo VaultSimC_eli = {
    "VaultSimC",                          /*!< Name of the Library. */
    "Stacked memory Vault Components",    /*!< Brief description of the Library */
    components,                           /*!< List of Components contained in the library. */
    NULL,                                 /*!< List of Events exported by the library. */
    NULL,                                 /*!< List of Introspectors provided by the library. */
    NULL,                                 /*!< List of Modules provided by the library. */
    subcomponents,                        /*!< List of SubComponents provided by the library. */
    NULL,                                 /*!< List of Partitioners provided by the library. */ 
    NULL,                                 /*!< Pointer to Function to generate a Python Module for use in Configurations */
    NULL                                  /*!< List of Generators provided by the library. */
  };
}

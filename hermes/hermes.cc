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

#include <sst/core/element.h>
#include <sst/core/configGraph.h>

#include <stdio.h>
#include <stdarg.h>

#include "msgapi.h"

using namespace std;
using namespace SST::Hermes;


extern "C" {
    ElementLibraryInfo hermes_eli = {
        "hermes",
        "Message exchange interfaces",
        NULL, //components,
	NULL,
	NULL,
	NULL, //modules,
	// partitioners,
	// generators,
	NULL,
	NULL,
    };
}

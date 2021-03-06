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


#ifndef _H_ZODIAC_COLLECTIVE_EVENT_BASE
#define _H_ZODIAC_COLLECTIVE_EVENT_BASE

#include "sst/elements/hermes/msgapi.h"
#include "zevent.h"

namespace SST {
namespace Zodiac {

class ZodiacCollectiveEvent : public ZodiacEvent {

	public:
		ZodiacCollectiveEvent();
		virtual ZodiacEventType getEventType() = 0;

};

}
}

#endif

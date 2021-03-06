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


#ifndef _H_EMBER_INIT_EVENT
#define _H_EMBER_INIT_EVENT

#include "emberMPIEvent.h"

namespace SST {
namespace Ember {

class EmberInitEvent : public EmberMPIEvent {

public:
	EmberInitEvent( MP::Interface& api, Output* output,
                    EmberEventTimeStatistic* stat ) :
            EmberMPIEvent( api, output, stat ){}
	~EmberInitEvent() {}

    std::string getName() { return "Init"; }

    void issue( uint64_t time, FOO* functor ) {

        EmberEvent::issue( time );
        m_api.init( functor );
    }
};

}
}

#endif

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


#ifndef _H_EMBER_REDUCE_EV
#define _H_EMBER_REDUCE_EV

#include "emberMPIEvent.h"

namespace SST {
namespace Ember {

class EmberReduceEvent : public EmberMPIEvent {
public:
	EmberReduceEvent( MP::Interface& api, Output* output,
                     EmberEventTimeStatistic* stat,
            Addr mydata, Addr result, uint32_t count,
            PayloadDataType dtype, ReductionOperation op, RankID root,
            Communicator group ) :
        EmberMPIEvent( api, output, stat ),
        m_mydata(mydata),
        m_result(result),
        m_count(count),
        m_dtype(dtype),
        m_op(op),
        m_root(root),
        m_group(group)
    {}

   std::string getName() { return "Reduce"; }

    void issue( uint64_t time, FOO* functor ) {

        EmberEvent::issue( time );

        m_api.reduce( m_mydata, m_result, m_count, m_dtype, m_op,
                                         m_root, m_group, functor );
    }

	~EmberReduceEvent() {}

private:
    Addr                m_mydata;
    Addr                m_result;
    uint32_t            m_count;
    PayloadDataType     m_dtype;
    ReductionOperation  m_op;
    RankID              m_root;
    Communicator        m_group;
};

}
}

#endif

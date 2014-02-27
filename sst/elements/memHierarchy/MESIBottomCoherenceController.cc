/*
 * File:   MESIBottomCoherenceController.cc
 * Author: Caesar De la Paz III
 * Email:  caesar.sst@gmail.com
 */

#include <sst_config.h>
#include <vector>
#include "coherenceControllers.h"
#include "MESIBottomCoherenceController.h"
using namespace SST;
using namespace SST::MemHierarchy;
using namespace SST::Interfaces;

/*----------------------------------------------------------------------------------------------------------------------
 * Bottom Coherence Controller Implementation
 *---------------------------------------------------------------------------------------------------------------------*/
	    
void MESIBottomCC::handleEviction(MemEvent* event, CacheLine* wbCacheLine){
	BCC_MESIState state = wbCacheLine->getState();
    updateEvictionStats(state);
    
    switch(state){
	case S:
		wbCacheLine->setState(I);
        sendCommand(PutS, wbCacheLine, parentLinks_->at(getParentId(wbCacheLine)));
		break;
    case E:
		wbCacheLine->setState(I);
        sendCommand(PutE, wbCacheLine, parentLinks_->at(getParentId(wbCacheLine)));
        break;
	case M:
		wbCacheLine->setState(I);
		sendCommand(PutM, wbCacheLine, parentLinks_->at(getParentId(wbCacheLine)));
		break;
	default:
		_abort(MemHierarchy::CacheController, "Not a valid state: %s", BccLineString[state]);
    }
}


void MESIBottomCC::handleAccess(MemEvent* _event, CacheLine* _cacheLine, Command _cmd){
    BCC_MESIState state = _cacheLine->getState();
    
    switch(_cmd){
    case GetS:
        processGetSRequest(_event, _cacheLine);
        break;
    case GetSEx:
    case GetX:
        if(modifiedStateNeeded(_event, _cacheLine)) return;
        processGetXRequest(_event, _cacheLine, _cmd);
        break;
    case PutS:
        PUTSReqsReceived_++;
        break;
    case PutE:
        processPutERequest(_event, _cacheLine);
    case PutM:
        processPutMRequest(_event, _cacheLine);
        break;
    default:
        _abort(MemHierarchy::CacheController, "Wrong command type!");
    }
}


void MESIBottomCC::handlePutAck(MemEvent* event, CacheLine* cacheLine){
    cacheLine->decAckCount();
}


void MESIBottomCC::handleInvalidate(MemEvent *event, CacheLine* cacheLine, Command cmd){
    //if(!canInvalidateRequestProceed(event, cacheLine, true)) return;
    if(!canInvalidateRequestProceed(event, cacheLine, false)) return;
    
    switch(cmd){
        case Inv:
			processInvRequest(event, cacheLine);
            break;
        case InvX:
			processInvXRequest(event, cacheLine);  //TODO: not tested yet
            break;
	    default:
            _abort(MemHierarchy::CacheController, "Command not supported.\n");
	}

}

void MESIBottomCC::handleAccessAck(MemEvent* ackEvent, CacheLine* cacheLine, const vector<mshrType*> mshrEntry){
    cacheLine->decAckCount();
    printData(d_, "Response Data", &ackEvent->getPayload());
    
    assert(cacheLine->getAckCount() == 0);
    assert(mshrEntry.front()->elem.type() == typeid(MemEvent*));
    assert(cacheLine->unlocked());
    
    MemEvent* origEv = boost::get<MemEvent*>(mshrEntry.front()->elem);
    Command origCmd  = origEv->getCmd();
    assert(MemEvent::isDataRequest(origCmd));
    cacheLine->setData(ackEvent->getPayload(), ackEvent);
    if(cacheLine->getState() == S && ackEvent->getGrantedState() == E) cacheLine->setState(E);

}


void MESIBottomCC::handleFetchInvalidate(MemEvent* _event, CacheLine* _cacheLine, int _parentId){
    if(!canInvalidateRequestProceed(_event, _cacheLine, false)) return;
    Command cmd = _event->getCmd();

    switch(cmd){
        case FetchInvalidate:
            _cacheLine->setState(I);
            FetchInvalidateReqSent_++;
            break;
        case FetchInvalidateX:  //TODO: not tested yet
            _cacheLine->setState(S);
            FetchInvalidateXReqSent_++;
            break;
	    default:
            _abort(MemHierarchy::CacheController, "Command not supported.\n");
	}
    
    sendResponse(_event, _cacheLine, _parentId);
}


/*---------------------------------------------------------------------------------------------------
 * Helper Functions
 *--------------------------------------------------------------------------------------------------*/

inline bool MESIBottomCC::modifiedStateNeeded(MemEvent* _event, CacheLine* _cacheLine){
    bool pf = _event->isPrefetch();
    Addr addr = _cacheLine->getBaseAddr();
    BCC_MESIState state = _cacheLine->getState();

    if(state == I || state == S){
        if(state == S){
            inc_GETXMissSM(addr, pf);
            _cacheLine->setState(SM);
        }
        else{
            inc_GETXMissIM(addr, pf);
            _cacheLine->setState(IM);
        }
        forwardMessage(_event, _cacheLine, &_event->getPayload());
        return true;
    }
    return false;
}


bool MESIBottomCC::canInvalidateRequestProceed(MemEvent* _event, CacheLine* _cacheLine, bool _sendAcks){
    bool ret = true;
    if(!_cacheLine || _cacheLine->inTransition()){
        d_->debug(_WARNING_,"Warning: Inv/FetchInv Rx but line's invalid or in transition. State = %s\n", BccLineString[_cacheLine->getState()]);
        ret = false;
    }
    
    if(!ret && _sendAcks) sendAckResponse(_event);

    return ret;
}


void MESIBottomCC::processGetXRequest(MemEvent* event, CacheLine* cacheLine, Command cmd){
    BCC_MESIState state = cacheLine->getState();
    Addr addr = cacheLine->getBaseAddr();
    bool pf = event->isPrefetch();
  
    if(state == E) cacheLine->setState(M);    /* upgrade */
    assert(cacheLine->getState() == M);
    
    if(cmd == GetX){
        cacheLine->setData(event->getPayload(), event);
        if(L1_ && event->queryFlag(MemEvent::F_LOCKED)){
            assert(cacheLine->isLockedByUser());
            cacheLine->decLock();
        }
    }
    if(L1_ && cmd == GetSEx) cacheLine->incLock();
    inc_GETXHit(addr, pf);
}


void MESIBottomCC::processInvRequest(MemEvent* _event, CacheLine* _cacheLine){
    BCC_MESIState state = _cacheLine->getState();
    
    if(state == M){
        _cacheLine->setState(I);
        sendCommand(PutM, _cacheLine, parentLinks_->at(getParentId(_cacheLine)));
        InvalidatePUTMReqSent_++;
    }
    else if(state == E){
        _cacheLine->setState(I);
        sendCommand(PutE, _cacheLine, parentLinks_->at(getParentId(_cacheLine)));
        InvalidatePUTMReqSent_++;
    }
    else{
        _cacheLine->setState(I);
        _cacheLine->setAckCount(0);
        //if(state != S) sendAckResponse(_event);
    }
}


void MESIBottomCC::processInvXRequest(MemEvent* _event, CacheLine* _cacheLine){
    BCC_MESIState state = _cacheLine->getState();

    if(state == M){
        _cacheLine->setState(I);
        sendCommand(PutM, _cacheLine, parentLinks_->at(getParentId(_cacheLine)));
        InvalidatePUTMReqSent_++;
    }
    else{
        _cacheLine->setState(S);
        sendAckResponse(_event);
    }
}

void MESIBottomCC::processGetSRequest(MemEvent* event, CacheLine* cacheLine){
    BCC_MESIState state = cacheLine->getState();
    Addr addr = cacheLine->getBaseAddr();
    bool pf = event->isPrefetch();

    if(state == I){
        cacheLine->setState(IS);
        forwardMessage(event, cacheLine, NULL);
        inc_GETSMissIS(addr, pf);
    }
    else inc_GETSHit(addr, pf);
}

void MESIBottomCC::processPutMRequest(MemEvent* event, CacheLine* cacheLine){
    BCC_MESIState state = cacheLine->getState();
    assert(state == M || state == E);
    if(state == E) cacheLine->setState(M);
    cacheLine->setData(event->getPayload(), event);
    PUTMReqsReceived_++;
}

void MESIBottomCC::processPutERequest(MemEvent* event, CacheLine* cacheLine){
    BCC_MESIState state = cacheLine->getState();
    assert(state == E || state == M);
    PUTEReqsReceived_++;
}

inline void MESIBottomCC::forwardMessage(MemEvent* _event, CacheLine* _cacheLine, vector<uint8_t>* _data){
    Addr baseAddr = _cacheLine->getBaseAddr();
    unsigned int lineSize = _cacheLine->getLineSize();
    forwardMessage(_event, baseAddr, lineSize, _data);
}


inline void MESIBottomCC::forwardMessage(MemEvent* _event, Addr _baseAddr, unsigned int _lineSize, vector<uint8_t>* _data){
    Link* deliveryLink = parentLinks_->at(getParentId(_baseAddr));

    Command cmd = _event->getCmd();
    MemEvent* forwardEvent;
    d_->debug(_L1_,"Forwarding Message: Addr = %#016llx, BaseAddr = %#016llx, Cmd = %s, Size = %i \n",
             (uint64_t)_event->getAddr(), _baseAddr, CommandString[cmd], _event->getSize());
    if(cmd == GetX) forwardEvent = new MemEvent((SST::Component*)owner_, _event->getAddr(), _baseAddr, _lineSize, cmd, *_data);
    else forwardEvent = new MemEvent((SST::Component*)owner_, _event->getAddr(), _baseAddr, _lineSize, cmd, _lineSize);

    uint64_t deliveryTime;
    if(_event->queryFlag(MemEvent::F_UNCACHED)){
        forwardEvent->setFlag(MemEvent::F_UNCACHED);
        deliveryTime = timestamp_;
    }
    else deliveryTime =  timestamp_ + accessLatency_;
    response resp = {deliveryLink, forwardEvent, deliveryTime, false};
    outgoingEventQueue_.push(resp);
}

inline void MESIBottomCC::sendResponse(MemEvent* _event, CacheLine* _cacheLine, int _parentId){
    Command cmd = _event->getCmd();

    MemEvent *responseEvent = _event->makeResponse((SST::Component*)owner_);
    responseEvent->setPayload(*_cacheLine->getData());
    Link* deliveryLink = _event->getDeliveryLink();
    response resp = {deliveryLink, responseEvent, timestamp_ + accessLatency_, true};
    outgoingEventQueue_.push(resp);
    
    d_->debug(_L1_,"Sending %s Response Message: Addr = %#016llx, BaseAddr = %#016llx, Cmd = %s, Size = %i \n",
              CommandString[cmd], _event->getBaseAddr(), _event->getBaseAddr(), CommandString[cmd], lineSize_);
}



unsigned int MESIBottomCC::getParentId(CacheLine* wbCacheLine){
    uint32_t res = 0;
    uint64_t tmp = wbCacheLine->getBaseAddr();
    for (uint32_t i = 0; i < 4; i++) {
        res ^= (uint32_t) (((uint64_t)0xffff) & tmp);
        tmp = tmp >> 16;
    }
    return (res % parentLinks_->size());
}

unsigned int MESIBottomCC::getParentId(Addr baseAddr){
    uint32_t res = 0;
    for (uint32_t i = 0; i < 4; i++) {
        res ^= (uint32_t) (((uint64_t)0xffff) & baseAddr);
        baseAddr = baseAddr >> 16;
    }
    return (res % parentLinks_->size());
}


void MESIBottomCC::printStats(int _stats, uint64_t _GetSExReceived,
                               uint64_t _invalidateWaitingForUserLock, uint64_t _totalInstReceived,
                               uint64_t _nonCoherenceReqsReceived){
    Output* dbg = new Output();
    dbg->init("", 0, 0, (Output::output_location_t)_stats);
    int totalMisses = GETXMissIM_ + GETXMissSM_ + GETSMissIS_;
    int totalHits = GETSHit_ + GETXHit_;
    double hitRatio = (totalHits / ( totalHits + (double)totalMisses)) * 100;
    dbg->output(C,"--------------------------------------------------------------------\n");
    dbg->output(C,"Name: %s\n", ownerName_.c_str());
    dbg->output(C,"--------------------------------------------------------------------\n");
    dbg->output(C,"GetS-IS misses: %i\n", GETSMissIS_);
    dbg->output(C,"GetX-SM misses: %i\n", GETXMissSM_);
    dbg->output(C,"GetX-IM misses: %i\n", GETXMissIM_);
    dbg->output(C,"GetS hits: %i\n", GETSHit_);
    dbg->output(C,"GetX hits: %i\n", GETXHit_);
    dbg->output(C,"Total misses: %i\n", totalMisses);
    dbg->output(C,"Total hits: %i\n", totalHits);
    dbg->output(C,"Hit Ratio:  %.3f%%\n", hitRatio);
    dbg->output(C,"PutS received: %i\n", PUTSReqsReceived_);
    dbg->output(C,"PutM received: %i\n", PUTMReqsReceived_);
    dbg->output(C,"PUTS sent due to evictions: %u\n", EvictionPUTSReqSent_);
    dbg->output(C,"PUTM sent due to evictions: %u\n", EvictionPUTMReqSent_);
    dbg->output(C,"PUTM sent due to invalidations: %u\n", InvalidatePUTMReqSent_);
    dbg->output(C,"Invalidates recieved that locked due to user atomic lock: %u\n", _invalidateWaitingForUserLock);
    dbg->output(C,"Total instructions recieved: %u\n", _totalInstReceived);
    dbg->output(C,"Memory requests received (non-coherency related): %u\n\n", _nonCoherenceReqsReceived);
}

inline void MESIBottomCC::updateEvictionStats(BCC_MESIState _state){
    if(_state == S)      EvictionPUTSReqSent_++;
    else if(_state == M) EvictionPUTMReqSent_++;
    else if(_state == E) EvictionPUTSReqSent_++;
    else _abort(MemHierarchy::CacheController, "State not supported for eviction.\n");
}


inline void MESIBottomCC::inc_GETXMissSM(Addr addr, bool prefetchRequest){
    if(!prefetchRequest){
        GETXMissSM_++;
        listener_->notifyAccess(CacheListener::WRITE, CacheListener::MISS, addr);
    }
}
inline void MESIBottomCC::inc_GETXMissIM(Addr addr, bool prefetchRequest){
    if(!prefetchRequest){
        GETXMissIM_++;
        listener_->notifyAccess(CacheListener::WRITE, CacheListener::MISS, addr);
    }
}
inline void MESIBottomCC::inc_GETSHit(Addr addr, bool prefetchRequest){
    if(!prefetchRequest){
        GETSHit_++;
        listener_->notifyAccess(CacheListener::READ, CacheListener::HIT, addr);
    }
}
inline void MESIBottomCC::inc_GETXHit(Addr addr, bool prefetchRequest){
    if(!prefetchRequest){
        GETXHit_++;
        listener_->notifyAccess(CacheListener::WRITE, CacheListener::HIT, addr);
    }
}
inline void MESIBottomCC::inc_GETSMissIS(Addr addr, bool prefetchRequest){
    if(!prefetchRequest){
        GETSMissIS_++;
        listener_->notifyAccess(CacheListener::READ, CacheListener::MISS, addr);
    }
}

inline bool MESIBottomCC::isExclusive(CacheLine* cacheLine) {
    BCC_MESIState state = cacheLine->getState();
    return (state == E) || (state == M);
}

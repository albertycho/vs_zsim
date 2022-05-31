/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "timing_cache.h"
#include "event_recorder.h"
#include "timing_event.h"
#include "zsim.h"

// Events
class HitEvent : public TimingEvent {
    private:
        TimingCache* cache;

    public:
        HitEvent(TimingCache* _cache,  uint32_t postDelay, int32_t domain) : TimingEvent(0, postDelay, domain), cache(_cache) {}

        void simulate(uint64_t startCycle) {
            cache->simulateHit(this, startCycle);
        }
};


class MissStartEvent : public TimingEvent {
    private:
        TimingCache* cache;
    public:
        uint64_t startCycle; //for profiling purposes
        MissStartEvent(TimingCache* _cache,  uint32_t postDelay, int32_t domain) : TimingEvent(0, postDelay, domain), cache(_cache) {}
        void simulate(uint64_t startCycle) {cache->simulateMissStart(this, startCycle);}
};

class MissResponseEvent : public TimingEvent {
    private:
        TimingCache* cache;
        MissStartEvent* mse;
    public:
        MissResponseEvent(TimingCache* _cache, MissStartEvent* _mse, int32_t domain) : TimingEvent(0, 0, domain), cache(_cache), mse(_mse) {}
        void simulate(uint64_t startCycle) {cache->simulateMissResponse(this, startCycle, mse);}
};

class MissWritebackEvent : public TimingEvent {
    private:
        TimingCache* cache;
        MissStartEvent* mse;
    public:
        MissWritebackEvent(TimingCache* _cache,  MissStartEvent* _mse, uint32_t postDelay, int32_t domain) : TimingEvent(0, postDelay, domain), cache(_cache), mse(_mse) {}
        void simulate(uint64_t startCycle) {cache->simulateMissWriteback(this, startCycle, mse);}
};

class ReplAccessEvent : public TimingEvent {
    private:
        TimingCache* cache;
    public:
        uint32_t accsLeft;
        ReplAccessEvent(TimingCache* _cache, uint32_t _accsLeft, uint32_t preDelay, uint32_t postDelay, int32_t domain) : TimingEvent(preDelay, postDelay, domain), cache(_cache), accsLeft(_accsLeft) {}
        void simulate(uint64_t startCycle) {cache->simulateReplAccess(this, startCycle);}
};

class WritebackOnInvalsEvent : public TimingEvent {
    private:
        TimingCache* cache;
    public:
        WritebackOnInvalsEvent(TimingCache* _cache, uint32_t postDelay, int32_t domain) : TimingEvent(0, postDelay, domain), cache(_cache) {}
        void simulate(uint64_t startCycle) {cache->simulateWritebackOnInvals(this, startCycle);}
};

TimingCache::TimingCache(uint32_t _numLines, CC* _cc, CacheArray* _array, ReplPolicy* _rp,
        uint32_t _accLat, uint32_t _invLat, uint32_t mshrs, uint32_t _tagLat, uint32_t _ways, uint32_t _cands, uint32_t _domain, const g_string& _name, int _level)
    : Cache(_numLines, _cc, _array, _rp, _accLat, _invLat, _name, _level), numMSHRs(mshrs), tagLat(_tagLat), ways(_ways), cands(_cands)
{
    lastFreeCycle = 0;
    lastAccCycle = 0;
    assert(numMSHRs > 0);
    activeMisses = 0;
    domain = _domain;
    //info("%s: mshrs %d domain %d", name.c_str(), numMSHRs, domain);
}

void TimingCache::initStats(AggregateStat* parentStat) {
    AggregateStat* cacheStat = new AggregateStat();
    cacheStat->init(name.c_str(), "Timing cache stats");
    initCacheStats(cacheStat);

    //Stats specific to timing cache
    profOccHist.init("occHist", "Occupancy MSHR cycle histogram", numMSHRs+1);
    cacheStat->append(&profOccHist);

    profHitLat.init("latHit", "Cumulative latency accesses that hit (demand and non-demand)");
    profMissRespLat.init("latMissResp", "Cumulative latency for miss start to response");
    profMissLat.init("latMiss", "Cumulative latency for miss start to finish (free MSHR)");

    cacheStat->append(&profHitLat);
    cacheStat->append(&profMissRespLat);
    cacheStat->append(&profMissLat);

    parentStat->append(cacheStat);
}

// TODO(dsm): This is copied verbatim from Cache. We should split Cache into different methods, then call those.
uint64_t TimingCache::access(MemReq& req) {

    bool is_llc=false;
    //for plotting
    if(level==1){//llc
        is_llc=true;
    }

    int req_level = req.flags >> 16;
    if (req.type == PUTS || req.type == PUTX) {
        req_level = level;
    }
    bool correct_level = (req_level == level);
    int32_t lineId = -1;
    //info("In cache access, req type is %s, my level is %d, input level is %d, childId is %d",AccessTypeName(req.type),level,req_level, req.childId);
    bool no_record = 0;//((req.flags) & (MemReq::NORECORD)) != 0;

    EventRecorder* evRec = zinfo->eventRecorders[req.srcId];
    assert_msg(evRec, "TimingCache is not connected to TimingCore");

    TimingRecord writebackRecord, accessRecord, invalOnAccRecord;
    writebackRecord.clear();
    accessRecord.clear();
    invalOnAccRecord.clear();
    uint64_t evDoneCycle = 0;

    uint64_t respCycle = req.cycle;

    // Tie two events to an optional timing record
    // TODO: Promote to evRec if this is more generally useful
    auto connect = [evRec](const TimingRecord* r, TimingEvent* startEv, TimingEvent* endEv, uint64_t startCycle, uint64_t endCycle) {
        assert_msg(startCycle <= endCycle, "start > end? %ld %ld", startCycle, endCycle);
        if (r) {
            assert_msg(startCycle <= r->reqCycle, "%ld / %ld", startCycle, r->reqCycle);
            assert_msg(r->respCycle <= endCycle, "%ld %ld %ld %ld", startCycle, r->reqCycle, r->respCycle, endCycle);
            uint64_t upLat = r->reqCycle - startCycle;
            uint64_t downLat = endCycle - r->respCycle;

            if (upLat) {
                DelayEvent* dUp = new (evRec) DelayEvent(upLat);
                dUp->setMinStartCycle(startCycle);
                startEv->addChild(dUp, evRec)->addChild(r->startEvent, evRec);
            }
            else {
                startEv->addChild(r->startEvent, evRec);
            }

            if (downLat) {
                DelayEvent* dDown = new (evRec) DelayEvent(downLat);
                dDown->setMinStartCycle(r->respCycle);
                r->endEvent->addChild(dDown, evRec)->addChild(endEv, evRec);
            }
            else {
                r->endEvent->addChild(endEv, evRec);
            }
        }
        else {
            if (startCycle == endCycle) {
                startEv->addChild(endEv, evRec);
            }
            else {
                DelayEvent* dEv = new (evRec) DelayEvent(endCycle - startCycle);
                dEv->setMinStartCycle(startCycle);
                startEv->addChild(dEv, evRec)->addChild(endEv, evRec);
            }
        }
    };
                    

    bool skipAccess = cc->startAccess(req); //may need to skip access due to races (NOTE: may change req.type!)
    if (likely(!skipAccess)) {
        if (correct_level) {
            int temp = req_level - 1;
            req.flags  = (req.flags & 0xffff) | (temp << 16);
            
            bool updateReplacement = (req.type == GETS) || (req.type == GETX) || (req.type == CLEAN_S);
            lineId = array->lookup(req.lineAddr, &req, updateReplacement);                                   
            respCycle += accLat;

            bool alloc = 0;

            if (lineId == -1 && cc->shouldAllocate(req)) {     // a NIC egress access that misses in the LLC should not allocate a line
                //assert(cc->shouldAllocate(req)); //dsm: for now, we don't deal with non-inclusion in TimingCache

                //Make space for new line
                alloc = 1;
                Address wbLineAddr;
                lineId = array->preinsert(req.lineAddr, &req, &wbLineAddr); //find the lineId to replace
                //info("[%s] Evicting 0x%lx", name.c_str(), wbLineAddr);
                req.clear(MemReq::INGR_EVCT);
                req.clear(MemReq::EGR_EVCT);
                int i=3;
                glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
                while (i < nicInfo->expected_core_count + 3){
                    Address base_ing = (Address)(nicInfo->nic_elem[i].recv_buf) >> lineBits;
                    uint64_t size_ing = nicInfo->recv_buf_pool_size; 
                    Address top_ing = ((Address)(nicInfo->nic_elem[i].recv_buf) + size_ing) >> lineBits;
                    Address base_egr = (Address)(nicInfo->nic_elem[i].lbuf) >> lineBits;
                    //uint64_t size_egr = 256*nicInfo->forced_packet_size;
                    uint64_t size_egr = 64*nicInfo->forced_packet_size; 
                    Address top_egr = ((Address)(nicInfo->nic_elem[i].lbuf) + size_egr) >> lineBits;
                    if (wbLineAddr >= base_ing && wbLineAddr <= top_ing) {
                        req.set(MemReq::INGR_EVCT);
                        break;
                    }
                    if (wbLineAddr >= base_egr && wbLineAddr <= top_egr) {
                        req.set(MemReq::EGR_EVCT);
                        break;
                    }
                    i++;
                }

                
                //Evictions are not in the critical path in any sane implementation -- we do not include their delays
                //NOTE: We might be "evicting" an invalid line for all we know. Coherence controllers will know what to do
                evDoneCycle = cc->processEviction(req, wbLineAddr, lineId, respCycle); //if needed, send invalidates/downgrades to lower level, and wb to upper level

                req.clear(MemReq::INGR_EVCT);
                req.clear(MemReq::EGR_EVCT);

                array->postinsert(req.lineAddr, &req, lineId); //do the actual insertion. NOTE: Now we must split insert into a 2-phase thing because cc unlocks us.

                if (evRec->hasRecord()) writebackRecord = evRec->popRecord();
            }

            //if (evRec->hasRecord()) writebackRecord = evRec->popRecord();

            uint64_t getDoneCycle = respCycle;  // latency from next level (if any), before invalidations are sent
            uint64_t invalOnAccCycle = 0;
            respCycle = cc->processAccess(req, lineId, respCycle, correct_level, &getDoneCycle, &invalOnAccCycle);

            if (no_record || req.type == CLEAN || req.type == CLEAN_S) {
                assert(!(evRec->hasRecord()));
            }
            else {
                if (getDoneCycle != 0) {
                    // normal accesses
                    if (!(req.is(MemReq::PKTOUT) && req.type == GETX)) {

                        if (evRec->hasRecord()) accessRecord = evRec->popRecord();

                        // At this point we have all the info we need to hammer out the timing record
                        TimingRecord tr = { req.lineAddr << lineBits, req.cycle, respCycle, req.type, nullptr, nullptr}; //note the end event is the response, not the wback

                        if (getDoneCycle - req.cycle == accLat && !alloc) {
                            // Hit
                            assert(!writebackRecord.isValid());
                            assert(!accessRecord.isValid());
                            uint64_t hitLat = respCycle - req.cycle; // accLat + invLat
                            HitEvent* ev = new (evRec) HitEvent(this, hitLat, domain);
                            ev->setMinStartCycle(req.cycle);
                            tr.startEvent = tr.endEvent = ev;
                        }
                        else {
                            getDoneCycle = respCycle;
                            assert(req.type != CLEAN);
                            //assert_msg(getDoneCycle == respCycle, "gdc %ld rc %ld", getDoneCycle, respCycle);

                            // Miss events:
                            // MissStart (does high-prio lookup) -> getEvent || evictionEvent || replEvent (if needed) -> MissWriteback

                            MissStartEvent* mse = new (evRec) MissStartEvent(this, accLat, domain);
                            MissResponseEvent* mre = new (evRec) MissResponseEvent(this, mse, domain);
                            MissWritebackEvent* mwe = new (evRec) MissWritebackEvent(this, mse, accLat, domain);

                            mse->setMinStartCycle(req.cycle);
                            mre->setMinStartCycle(getDoneCycle);
                            
                            mwe->setMinStartCycle(MAX(evDoneCycle, getDoneCycle));

                            // Get path
                            connect(accessRecord.isValid() ? &accessRecord : nullptr, mse, mre, req.cycle + accLat, getDoneCycle);
                            
                            mre->addChild(mwe, evRec);

                            // Eviction path
                            if (evDoneCycle) {
                                connect(writebackRecord.isValid() ? &writebackRecord : nullptr, mse, mwe, req.cycle + accLat, evDoneCycle);
                            }

                            // Replacement path
                            if (evDoneCycle && cands > ways) {
                                uint32_t replLookups = (cands + (ways - 1)) / ways - 1; // e.g., with 4 ways, 5-8 -> 1, 9-12 -> 2, etc.
                                assert(replLookups);

                                uint32_t fringeAccs = ways - 1;
                                uint32_t accsSoFar = 0;

                                TimingEvent* p = mse;

                                // Candidate lookup events
                                while (accsSoFar < replLookups) {
                                    uint32_t preDelay = accsSoFar ? 0 : tagLat;
                                    uint32_t postDelay = tagLat - MIN(tagLat - 1, fringeAccs);
                                    uint32_t accs = MIN(fringeAccs, replLookups - accsSoFar);
                                    //info("ReplAccessEvent rl %d fa %d preD %d postD %d accs %d", replLookups, fringeAccs, preDelay, postDelay, accs);
                                    ReplAccessEvent* raEv = new (evRec) ReplAccessEvent(this, accs, preDelay, postDelay, domain);
                                    raEv->setMinStartCycle(req.cycle /*lax...*/);
                                    accsSoFar += accs;
                                    p->addChild(raEv, evRec);
                                    p = raEv;
                                    fringeAccs *= ways - 1;
                                }

                                // Swap events -- typically, one read and one write work for 1-2 swaps. Exact number depends on layout.
                                ReplAccessEvent* rdEv = new (evRec) ReplAccessEvent(this, 1, tagLat, tagLat, domain);
                                rdEv->setMinStartCycle(req.cycle /*lax...*/);
                                ReplAccessEvent* wrEv = new (evRec) ReplAccessEvent(this, 1, 0, 0, domain);
                                wrEv->setMinStartCycle(req.cycle /*lax...*/);

                                p->addChild(rdEv, evRec)->addChild(wrEv, evRec)->addChild(mwe, evRec);
                            }


                            tr.startEvent = mse;
                            tr.endEvent = mre; // note the end event is the response, not the wback
                        }
                        evRec->pushRecord(tr);
                    }
                    // egress access from the NIC that invalidate
                    else {
                        
                        // At this point we have all the info we need to hammer out the timing record
                        TimingRecord tr = { req.lineAddr << lineBits, req.cycle, respCycle, req.type, nullptr, nullptr}; //note the end event is the response, not the wback
                        
                        // case 1: hit in the llc
                            // we have invalidated everyone + ourselves and have possibly sent a wb to memory
                        if (getDoneCycle - req.cycle == accLat) { 
                            assert(!writebackRecord.isValid());
                            assert(!accessRecord.isValid());
                            if (evRec->hasRecord()) writebackRecord = evRec->popRecord();                            
                            uint64_t hitLat = respCycle - req.cycle; // accLat + invLat + memLat
                            HitEvent* ev = new (evRec) HitEvent(this, hitLat, domain);
                            ev->setMinStartCycle(req.cycle);
                            WritebackOnInvalsEvent* wbe = new (evRec) WritebackOnInvalsEvent(this, accLat, domain);
                            wbe->setMinStartCycle(MAX(respCycle,invalOnAccCycle));
                            connect(accessRecord.isValid() ? &writebackRecord : nullptr, ev, wbe, respCycle, invalOnAccCycle);
                            tr.startEvent = tr.endEvent = ev;
                            evRec->pushRecord(tr);
                        }   

                        // case 2: miss in the llc, hit in priv caches
                            // bring data+inval priv caches --> return data --> inval llc + write to mem
                        else if (invalOnAccCycle){
                            getDoneCycle = respCycle;

                            MissStartEvent* mse = new (evRec) MissStartEvent(this, accLat, domain);
                            MissResponseEvent* mre = new (evRec) MissResponseEvent(this, mse, domain);
                            MissWritebackEvent* mwe = new (evRec) MissWritebackEvent(this, mse, accLat, domain);

                            mse->setMinStartCycle(req.cycle);
                            mre->setMinStartCycle(getDoneCycle);
                            
                            mwe->setMinStartCycle(MAX(invalOnAccCycle, getDoneCycle));

                            // Get path
                            assert(!accessRecord.isValid());
                            connect(nullptr, mse, mre, req.cycle + accLat, getDoneCycle);
                            
                            mre->addChild(mwe, evRec);

                            if (evRec->hasRecord()) writebackRecord = evRec->popRecord();
                            connect(writebackRecord.isValid() ? &writebackRecord : nullptr, mse, mwe, getDoneCycle, invalOnAccCycle);

                            /*
                            // final llc inval path
                            if (evRec->hasRecord()) invalOnAccRecord = evRec->popRecord(); 
                            assert(invalOnAccRecord.isValid());
                            WritebackOnInvalsEvent* wbe = new (evRec) WritebackOnInvalsEvent(this, accLat, domain);
                            wbe->setMinStartCycle(getDoneCycle);
                            connect(&invalOnAccRecord, mre, wbe, getDoneCycle, invalOnAccCycle);
                            */
                            
                            tr.startEvent = mse;
                            tr.endEvent = mre; // note the end event is the response, not the wback
                            evRec->pushRecord(tr);
                        }
                        // case 3: miss everywhere
                            // no allocation, process access forwarded the request to memory, we have a mem read timing record
                        else {
                            // there is a memory record that the core will pick up
                        }
                        
                    }
                }
            }
        }
    
        else {
            //info("passing to mem");
            uint64_t invalCycle = 0;
            if(req.type == GETX && req.is(MemReq::PKTIN)) {  // ingress dma write, might need to invalidate self/upper levels
                bool reqWriteback = false;
                respCycle += accLat;
                //InvReq invreq = {req.lineAddr, INV, &reqWriteback, respCycle, 1742};
                //invalCycle = MAX(this->finishInvalidate(invreq),respCycle);      // check if LLC has a copy + propagate to children
                int32_t templineId = array->lookup(req.lineAddr, nullptr, false);
                MemReq invreq = {req.lineAddr, CLEAN, req.childId, req.state, respCycle, req.childLock, req.initialState, req.srcId};
                invalCycle = cc->processAccess(invreq, templineId, respCycle, true);
                assert(!evRec->hasRecord());
                /*
                if (reqWriteback) {     // writeback to mem in case the invalidations caused evictions
                    assert(!correct_level);
                    MemReq wbreq = {req.lineAddr, PUTX, req.childId, req.state, respCycle, req.childLock, *(req.state), req.srcId, 0};
                    invalCycle = MAX(cc->processAccess(wbreq,lineId, respCycle, correct_level), respCycle); // our own (llc) state has already been adjusted to I
                    //marina: SHOULD I HAVE TIMING SIMULATION HERE???
                }
                */

                respCycle = cc->processAccess(req, lineId, respCycle, correct_level);
                assert(evRec->hasRecord());
                // Timing simulation
                TimingRecord tr = { req.lineAddr << lineBits, req.cycle, respCycle, req.type, nullptr, nullptr}; //note the end event is the response, not the wback

                MissStartEvent* mse = new (evRec) MissStartEvent(this, accLat, domain);
                MissResponseEvent* mre = new (evRec) MissResponseEvent(this, mse, domain);
                MissWritebackEvent* mwe = new (evRec) MissWritebackEvent(this, mse, accLat, domain);

                mse->setMinStartCycle(req.cycle);
                mre->setMinStartCycle(respCycle);
                
                mwe->setMinStartCycle(MAX(respCycle, invalCycle));                 

                if (evRec->hasRecord()) accessRecord = evRec->popRecord();
                connect(accessRecord.isValid() ? &accessRecord : nullptr, mse, mre, req.cycle + accLat, respCycle);
                
                mre->addChild(mwe, evRec);

                //if (evRec->hasRecord()) accessRecord = evRec->popRecord();
                connect(nullptr, mse, mwe, req.cycle + accLat, invalCycle);

                tr.startEvent = mse;
                tr.endEvent = mre; // note the end event is the response, not the wback

                evRec->pushRecord(tr);
            }
            else {
				/////// dbg
				int is_rb = 0;
				int is_lb=0;
				int i=3;
				while (i < nicInfo->expected_core_count + 3){
                    Address base_ing = (Address)(nicInfo->nic_elem[i].recv_buf) >> lineBits;
                    uint64_t size_ing = nicInfo->recv_buf_pool_size; 
                    Address top_ing = ((Address)(nicInfo->nic_elem[i].recv_buf) + size_ing) >> lineBits;
                    Address base_egr = (Address)(nicInfo->nic_elem[i].lbuf) >> lineBits;
                    uint64_t size_egr = 256*nicInfo->forced_packet_size;
                    Address top_egr = ((Address)(nicInfo->nic_elem[i].lbuf) + size_egr) >> lineBits;
                    if (req.lineAddr >= base_ing && req.lineAddr <= top_ing) {
                        is_rb=1;
                        break;
                    }
                    if (req.lineAddr >= base_egr && req.lineAddr <= top_egr) {
                        is_lb=1;
                        break;
                    }
                    i++;
                }

				info("req type: %d, flags: %x, is_lb=%d, is_rb=%d",req.type, req.flags, is_lb,is_rb);
                panic("?!");
            }
        }
    }
    cc->endAccess(req);

    assert_msg(respCycle >= req.cycle, "[%s] resp < req? 0x%lx type %s childState %s, respCycle %ld reqCycle %ld",
            name.c_str(), req.lineAddr, AccessTypeName(req.type), MESIStateName(*req.state), respCycle, req.cycle);
    return respCycle;
}


uint64_t TimingCache::highPrioAccess(uint64_t cycle) {
    assert(cycle >= lastFreeCycle);
    uint64_t lookupCycle = MAX(cycle, lastAccCycle+1);
    if (lastAccCycle < cycle-1) lastFreeCycle = cycle-1; //record last free run
    lastAccCycle = lookupCycle;
    return lookupCycle;
}

/* The simple things you see here are complicated,
 * I look pretty young but I'm just back-dated...
 *
 * To make this efficient, we do not want to keep priority queues. Instead, a
 * low-priority access is granted if there was a free slot on the *previous*
 * cycle. This means that low-prio accesses should be post-dated by 1 cycle.
 * This is fine to do, since these accesses are writebacks and non critical
 * path accesses. Essentially, we're modeling that we know those accesses one
 * cycle in advance.
 */
uint64_t TimingCache::tryLowPrioAccess(uint64_t cycle) {
    if (lastAccCycle < cycle-1 || lastFreeCycle == cycle-1) {
        lastFreeCycle = 0;
        lastAccCycle = MAX(cycle-1, lastAccCycle);
        return cycle;
    } else {
        return 0;
    }
}

void TimingCache::simulateHit(HitEvent* ev, uint64_t cycle) {
    if (activeMisses < numMSHRs) {
        uint64_t lookupCycle = highPrioAccess(cycle);
        profHitLat.inc(lookupCycle-cycle);
        ev->done(lookupCycle);  // postDelay includes accLat + invalLat
    } else {
        // queue
        ev->hold();
        pendingQueue.push_back(ev);
    }
}

void TimingCache::simulateMissStart(MissStartEvent* ev, uint64_t cycle) {
    if (activeMisses < numMSHRs) {
        activeMisses++;
        profOccHist.transition(activeMisses, cycle);

        ev->startCycle = cycle;
        uint64_t lookupCycle = highPrioAccess(cycle);
        ev->done(lookupCycle);
    } else {
        //info("Miss, all MSHRs used, queuing");
        ev->hold();
        pendingQueue.push_back(ev);
    }
}

void TimingCache::simulateMissResponse(MissResponseEvent* ev, uint64_t cycle, MissStartEvent* mse) {
    profMissRespLat.inc(cycle - mse->startCycle);
    ev->done(cycle);
}

void TimingCache::simulateMissWriteback(MissWritebackEvent* ev, uint64_t cycle, MissStartEvent* mse) {
    uint64_t lookupCycle = tryLowPrioAccess(cycle);
    if (lookupCycle) { //success, release MSHR
        assert(activeMisses);
        profMissLat.inc(cycle - mse->startCycle);
        activeMisses--;
        profOccHist.transition(activeMisses, lookupCycle);
        if (!pendingQueue.empty()) {
            //info("XXX %ld elems in pending queue", pendingQueue.size());
            for (TimingEvent* qev : pendingQueue) {
                qev->requeue(cycle+1);
            }
            pendingQueue.clear();
        }
        ev->done(cycle);
    } else {
        ev->requeue(cycle+1);
    }
}

void TimingCache::simulateReplAccess(ReplAccessEvent* ev, uint64_t cycle) {
    assert(ev->accsLeft);
    uint64_t lookupCycle = tryLowPrioAccess(cycle);
    if (lookupCycle) {
        ev->accsLeft--;
        if (!ev->accsLeft) {
            ev->done(cycle);
        } else {
            ev->requeue(cycle+1);
        }
    } else {
        ev->requeue(cycle+1);
    }
}

void TimingCache::simulateWritebackOnInvals(WritebackOnInvalsEvent* ev, uint64_t cycle) {
    ev->done(cycle);
}

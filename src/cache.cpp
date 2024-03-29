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

#include "cache.h"
#include "hash.h"

#include "event_recorder.h"
#include "timing_event.h"
#include "zsim.h"

Cache::Cache(uint32_t _numLines, CC* _cc, CacheArray* _array, ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, const g_string& _name, int _level)
    : cc(_cc), array(_array), rp(_rp), numLines(_numLines), accLat(_accLat), invLat(_invLat), name(_name), level(_level) {}

const char* Cache::getName() {
    return name.c_str();
}

void Cache::setParents(uint32_t childId, const g_vector<MemObject*>& parents, Network* network) {
    cc->setParents(childId, parents, network);
}

void Cache::setChildren(const g_vector<BaseCache*>& children, Network* network) {
	//info("%s setChildren, children.size()=%d",name.c_str(), children.size());
    cc->setChildren(children, network);
}

void Cache::initStats(AggregateStat* parentStat) {
    AggregateStat* cacheStat = new AggregateStat();
    cacheStat->init(name.c_str(), "Cache stats");
    initCacheStats(cacheStat);
    parentStat->append(cacheStat);
}

void Cache::initCacheStats(AggregateStat* cacheStat) {
    cc->initStats(cacheStat);
    array->initStats(cacheStat);
    rp->initStats(cacheStat);
}

uint64_t Cache::access(MemReq& req) {

    bool no_record = req.flags & (MemReq::NORECORD) != 0;

    uint32_t req_level = req.flags >> 16;
    if (req.type == PUTS || req.type == PUTX) {
        req_level = level;
    }
    bool correct_level = (req_level == level);
	

    int32_t lineId = -1;
    //info("In cache access, req type is %s, my level is %d, input level is %d",AccessTypeName(req.type),level,req_level);

    uint64_t respCycle = req.cycle;
    bool skipAccess = cc->startAccess(req); //may need to skip access due to races (NOTE: may change req.type!)
    if (likely(!skipAccess)) {
        if (correct_level) {
            uint32_t temp = req_level - 1;
            req.flags  = (req.flags & 0xffff) | (temp << 16);
            bool updateReplacement = (req.type == GETS) || (req.type == GETX) || (req.type == CLEAN_S);
            lineId = array->lookup(req.lineAddr, &req, updateReplacement);
            respCycle += accLat;

            if (lineId == -1 && cc->shouldAllocate(req)) {
                //Make space for new line
                Address wbLineAddr;
                lineId = array->preinsert(req.lineAddr, &req, &wbLineAddr); //find the lineId to replace
                trace(Cache, "[%s] Evicting 0x%lx", name.c_str(), wbLineAddr);

                req.clear(MemReq::INGR_EVCT);
                req.clear(MemReq::EGR_EVCT);
                int i=3;
                /* skip nicInfo check - only used for sweeper
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
                */
                //Evictions are not in the critical path in any sane implementation -- we do not include their delays
                //NOTE: We might be "evicting" an invalid line for all we know. Coherence controllers will know what to do
                
                cc->processEviction(req, wbLineAddr, lineId, respCycle); //if needed, send invalidates/downgrades to lower level, and wb to upper level

                req.clear(MemReq::INGR_EVCT);
                req.clear(MemReq::EGR_EVCT);
                
                array->postinsert(req.lineAddr, &req, lineId); //do the actual insertion. NOTE: Now we must split insert into a 2-phase thing because cc unlocks us.
            }
            // Enforce single-record invariant: Writeback access may have a timing
            // record. If so, read it.
            EventRecorder* evRec = zinfo->eventRecorders[req.srcId];
            TimingRecord wbAcc;
            wbAcc.clear();

            if (no_record) {
                assert(!(evRec->hasRecord()));
            }

            if (unlikely(evRec && evRec->hasRecord())) {
                wbAcc = evRec->popRecord();
            }
        
            respCycle = cc->processAccess(req, lineId, respCycle, correct_level);

            if (no_record) {
                assert(!evRec->hasRecord());
            }
            else {
                // Access may have generated another timing record. If *both* access
                // and wb have records, stitch them together
                if (unlikely(wbAcc.isValid())) {
                    if (!evRec->hasRecord()) {
                        // Downstream should not care about endEvent for PUTs
                        wbAcc.endEvent = nullptr;
                        evRec->pushRecord(wbAcc);
                    }
                    else {
                        // Connect both events
                        TimingRecord acc = evRec->popRecord();
                        assert(wbAcc.reqCycle >= req.cycle);
                        assert(acc.reqCycle >= req.cycle);
                        DelayEvent* startEv = new (evRec) DelayEvent(0);
                        DelayEvent* dWbEv = new (evRec) DelayEvent(wbAcc.reqCycle - req.cycle);
                        DelayEvent* dAccEv = new (evRec) DelayEvent(acc.reqCycle - req.cycle);
                        startEv->setMinStartCycle(req.cycle);
                        dWbEv->setMinStartCycle(req.cycle);
                        dAccEv->setMinStartCycle(req.cycle);
                        startEv->addChild(dWbEv, evRec)->addChild(wbAcc.startEvent, evRec);
                        startEv->addChild(dAccEv, evRec)->addChild(acc.startEvent, evRec);

                        acc.reqCycle = req.cycle;
                        acc.startEvent = startEv;
                        // endEvent / endCycle stay the same; wbAcc's endEvent not connected
                        evRec->pushRecord(acc);
                    }
                }
            }
        }
        else {
			//info("not correct level, should only see in l1, name: %s, level=%d", name.c_str(), level);
            respCycle = cc->processAccess(req, lineId, respCycle, correct_level);
        }
    }

    cc->endAccess(req);

    assert_msg(respCycle >= req.cycle, "[%s] resp < req? 0x%lx type %s childState %s, respCycle %ld reqCycle %ld",
            name.c_str(), req.lineAddr, AccessTypeName(req.type), MESIStateName(*req.state), respCycle, req.cycle);
    return respCycle;
}

void Cache::startInvalidate() {
    cc->startInv(); //note we don't grab tcc; tcc serializes multiple up accesses, down accesses don't see it
}

uint64_t Cache::finishInvalidate(const InvReq& req) {
    int32_t lineId = array->lookup(req.lineAddr, nullptr, false);
    uint64_t respCycle = req.cycle;
    size_t found = name.find("l3");
    if (lineId == -1) {
        // am I the llc?
        if (found == std::string::npos) {   //not the llc, kill the received invalidation
            cc->finishInv();
        }
        else {          // i am the llc but don't have the line; someone above me might have it though
            respCycle = req.cycle + invLat;
            respCycle = cc->processInv(req, lineId, respCycle);
            if (respCycle == req.cycle + invLat)
                respCycle -= invLat;
        }
    }
    else {  // line is present
        //assert_msg(lineId != -1, "[%s] Invalidate on non-existing address 0x%lx type %s lineId %d, reqWriteback %d", name.c_str(), req.lineAddr, InvTypeName(req.type), lineId, *req.writeback);
        respCycle = req.cycle + invLat;
        //trace(Cache, "[%s] Invalidate start 0x%lx type %s lineId %d, reqWriteback %d", name.c_str(), req.lineAddr, InvTypeName(req.type), lineId, *req.writeback);
        respCycle = cc->processInv(req, lineId, respCycle); //send invalidates or downgrades to children, and adjust our own state
        if (found == std::string::npos) // if i'm not the llc, release the bcc lock
            cc->finishInv();
        //trace(Cache, "[%s] Invalidate end 0x%lx type %s lineId %d, reqWriteback %d, latency %ld", name.c_str(), req.lineAddr, InvTypeName(req.type), lineId, *req.writeback, respCycle - req.cycle);
    }
    return respCycle;
}

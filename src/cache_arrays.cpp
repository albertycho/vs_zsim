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

#include "cache_arrays.h"
#include "hash.h"
#include "repl_policies.h"

#include <math.h>   
#include "zsim.h"

/* Set-associative array implementation */

SetAssocArray::SetAssocArray(uint32_t _numLines, uint32_t _assoc, ReplPolicy* _rp, HashFamily* _hf) : rp(_rp), hf(_hf), numLines(_numLines), assoc(_assoc)  {
    //array = gm_calloc<Address>(numLines);
    array = gm_calloc<CacheLine>(numLines);
    for(int i=0; i<numLines; i++) {
        array[i].addr = 0;
        array[i].nicType = DATA;
        array[i].lastUSer = NONE;
    }
    numSets = numLines/assoc;

	//info("numSets = %d",numSets);
    if(isPow2(numSets)){
		//info("numSets is pow2");
        setMask = numSets-1;
	}
    else {
        setMask = ceil(log2(numSets));
    }

    //assert_msg(isPow2(numSets), "must have a power of 2 # sets, but you specified %d", numSets);
}

void SetAssocArray::initStats(AggregateStat* parentStat) {
    AggregateStat* objStats = new AggregateStat();
    objStats->init("array", "CacheArray stats");
	//netMisses_nic_rb.init("netMiss_nic_rb", "Requests associated with network functionality from nic for recv buffer, misses");
    //netMisses_nic_lb.init("netMiss_nic_lb", "Requests associated with network functionality from nic for local buffer, misses");
    //netMisses_core.init("netMiss_core", "Requests associated with network functionality from core, misses");
    //netHits_nic_rb.init("netHit_nic_rb", "Requests associated with network functionality from nic for recv buffer, hits");
    //netHits_nic_lb.init("netHit_nic_lb", "Requests associated with network functionality from nic for local buffer, hits");
    //netHits_core.init("netHit_core", "Requests associated with network functionality from core, hits");
    appMisses.init("oldappMiss", "Requests associated with app functionality, misses");
    appHits.init("oldappHit", "Requests associated with app functionality, hits");
    way_misses.init("way_inserts", "Insertions per cache way",assoc);
    way_hits.init("way_hits", "Hits per cache way",assoc);
    // nic_rb_way_hits.init("nic_rb_way_hits", "Hits per cache way",assoc);
    // nic_rb_way_misses.init("nic_rb_way_inserts", "Insertions per cache way",assoc);
    // rb_insert_server.init("rb_insert_server", "Insertions per cache way");
    // NNF_way_hits.init("NNF_way_hits", "Hits per cache way by NNF",assoc);
    // NNF_way_misses.init("NNF_way_misses", "misses per cache way by NNF",assoc);

    //objStats->append(&netMisses_nic_rb);
    //objStats->append(&netMisses_nic_lb);
    //objStats->append(&netMisses_core);
    ////objStats->append(&netHits_nic);
    //objStats->append(&netHits_nic_rb);
    //objStats->append(&netHits_nic_lb);
    //objStats->append(&netHits_core);
    objStats->append(&appMisses);
    objStats->append(&appHits);
    objStats->append(&way_misses);
    objStats->append(&way_hits);
    // objStats->append(&nic_rb_way_hits);
    // objStats->append(&nic_rb_way_misses);
    // objStats->append(&rb_insert_server);
    // objStats->append(&NNF_way_hits);
    // objStats->append(&NNF_way_misses);
    parentStat->append(objStats);
}

bool SetAssocArray::isCons(const Address lineAddr) {
    uint32_t set = (hf->hash(0, lineAddr) & setMask) % numSets;
    uint32_t first = set*assoc;
    for (uint32_t id = first; id < first + assoc; id++) {
        if (array[id].addr ==  lineAddr) {
            if(array[id].lastUSer == APP)
                return true;
            else
                return false;
        }
    }
}

int32_t SetAssocArray::lookup(const Address lineAddr, const MemReq* req, bool updateReplacement) {
    uint32_t set = (hf->hash(0, lineAddr) & setMask) % numSets;
    uint32_t first = set*assoc;
    for (uint32_t id = first; id < first + assoc; id++) {
        if (array[id].addr ==  lineAddr) {
            if (req != nullptr) {
                if (req->srcId > 1) //  comming from app core
                    array[id].lastUSer = APP;
                else                // coming from NIC
                    array[id].lastUSer = NIC;
                if (req->is(MemReq::NETRELATED_ING) || req->is(MemReq::NETRELATED_EGR)){
                    array[id].nicType = NETWORK;
                    if (req->srcId > 1) {
                        //netHits_core.atomicInc();
                    }
                    else {
                        //netHits_nic.atomicInc();
                        if(req->flags & MemReq::PKTIN){
                            //netHits_nic_rb.atomicInc();
                            nic_rb_way_hits.inc(id-first);
                            if((id-first) < (12 - (nicInfo->num_ddio_ways))){
                                nicInfo->spillover_count++;
                            }
                        }
                    }
                }
                else {
                    array[id].nicType = DATA;
                    if(req->type == GETS || req->type == GETX){
                        appHits.atomicInc();
                    }
					// if(req->srcId > 14){
					// 	if(req->type == GETS || req->type == GETX){
					// 		NNF_way_hits.inc(id-first);
					// 	}
					// }
                }
            }
            way_hits.inc(id-first);
            if (updateReplacement) 
                rp->update(id, req);
            return id;
        }
    }
    if (req != nullptr) {
        if (req->flags & MemReq::NETRELATED_ING || req->flags & MemReq::NETRELATED_EGR) {
            if (req->srcId > 2) {
                //netMisses_core.atomicInc();
            }
            else {
                if(req->flags & MemReq::PKTIN){
                    //netMisses_nic_rb.atomicInc();
                }
                else if(req->flags & MemReq::PKTOUT){
                    //netMisses_nic_lb.atomicInc();
                }
                else{
                    printf("NETWORK related access from nic but not PKTIN or PKTOUT? shouldn't happen\n");
                }
            }
        }
        else {
            if(req->type == GETS || req->type == GETX){
                appMisses.atomicInc();
            }
        }
    }
    return -1;
}


bool is_rb_addr_ca(Address lineaddr){
    uint64_t num_cores = zinfo->numCores;
    for(int i=0; i<num_cores;i++){
        uint64_t rb_base=(uint64_t) nicInfo->nic_elem[i].recv_buf;
        uint64_t rb_top =rb_base+nicInfo->recv_buf_pool_size;
        uint64_t rb_base_line=rb_base>>lineBits;
        uint64_t rb_top_line = rb_top>>lineBits;
        if (lineaddr >= rb_base_line && lineaddr <= rb_top_line) {
            return true;
        }
    }
    
    return false;
}

uint32_t SetAssocArray::preinsert(const Address lineAddr, const MemReq* req, Address* wbLineAddr) { //TODO: Give out valid bit of wb cand?
    uint32_t set = (hf->hash(0, lineAddr) & setMask) % numSets;
    uint32_t first = set*assoc;

    uint32_t candidate = rp->rankCands(req, SetAssocCands(first, first+assoc));
    way_misses.inc(candidate-first);
    /*
    //info("eviction candidate is %lld, with index %ld", array[candidate], candidate);
    if(req->flags & MemReq::PKTIN){
        nic_rb_way_misses.inc(candidate-first);
    }
    else if(is_rb_addr_ca(lineAddr)){ //brought in by core after tight leaky DMA
        nic_rb_way_misses.inc(candidate-first);
        rb_insert_server.atomicInc();
    }
    */
    *wbLineAddr = array[candidate].addr;
    return candidate;
}

void SetAssocArray::postinsert(const Address lineAddr, const MemReq* req, uint32_t candidate) {
    rp->replaced(candidate);
    array[candidate].addr = lineAddr;
    if (req != nullptr) {
        if (req->srcId > 1) //  comming from app core
            array[candidate].lastUSer = APP;
        else                // coming from NIC
            array[candidate].lastUSer = NIC;
        if (req->is(MemReq::NETRELATED_ING) || req->is(MemReq::NETRELATED_EGR))
            array[candidate].nicType = NETWORK;
        else
            array[candidate].nicType = DATA;
    }
    rp->update(candidate, req);
}


/* ZCache implementation */

ZArray::ZArray(uint32_t _numLines, uint32_t _ways, uint32_t _candidates, ReplPolicy* _rp, HashFamily* _hf) //(int _size, int _lineSize, int _assoc, int _zassoc, ReplacementPolicy<T>* _rp, int _hashType)
    : rp(_rp), hf(_hf), numLines(_numLines), ways(_ways), cands(_candidates)
{
    assert_msg(ways > 1, "zcaches need >=2 ways to work");
    assert_msg(cands >= ways, "candidates < ways does not make sense in a zcache");
    assert_msg(numLines % ways == 0, "number of lines is not a multiple of ways");

    //Populate secondary parameters
    numSets = numLines/ways;
    assert_msg(isPow2(numSets), "must have a power of 2 # sets, but you specified %d", numSets);
    setMask = numSets - 1;

    lookupArray = gm_calloc<uint32_t>(numLines);
    array = gm_calloc<Address>(numLines);
    for (uint32_t i = 0; i < numLines; i++) {
        lookupArray[i] = i;  // start with a linear mapping; with swaps, it'll get progressively scrambled
    }
    swapArray = gm_calloc<uint32_t>(cands/ways + 2);  // conservative upper bound (tight within 2 ways)
}

void ZArray::initStats(AggregateStat* parentStat) {
    AggregateStat* objStats = new AggregateStat();
    objStats->init("array", "ZArray stats");
    statSwaps.init("swaps", "Block swaps in replacement process");
    objStats->append(&statSwaps);
    parentStat->append(objStats);
}

int32_t ZArray::lookup(const Address lineAddr, const MemReq* req, bool updateReplacement) {
    /* Be defensive: If the line is 0, panic instead of asserting. Now this can
     * only happen on a segfault in the main program, but when we move to full
     * system, phy page 0 might be used, and this will hit us in a very subtle
     * way if we don't check.
     */
    if (unlikely(!lineAddr)) panic("ZArray::lookup called with lineAddr==0 -- your app just segfaulted");

    for (uint32_t w = 0; w < ways; w++) {
        uint32_t lineId = lookupArray[w*numSets + (hf->hash(w, lineAddr) & setMask)];
        if (array[lineId] == lineAddr) {
            if (updateReplacement) {
                rp->update(lineId, req);
            }
            return lineId;
        }
    }
    return -1;
}

uint32_t ZArray::preinsert(const Address lineAddr, const MemReq* req, Address* wbLineAddr) {
    ZWalkInfo candidates[cands + ways]; //extra ways entries to avoid checking on every expansion

    bool all_valid = true;
    uint32_t fringeStart = 0;
    uint32_t numCandidates = ways; //seeds

    //info("Replacement for incoming 0x%lx", lineAddr);

    //Seeds
    for (uint32_t w = 0; w < ways; w++) {
        uint32_t pos = w*numSets + (hf->hash(w, lineAddr) & setMask);
        uint32_t lineId = lookupArray[pos];
        candidates[w].set(pos, lineId, -1);
        all_valid &= (array[lineId] != 0);
        //info("Seed Candidate %d addr 0x%lx pos %d lineId %d", w, array[lineId], pos, lineId);
    }

    //Expand fringe in BFS fashion
    while (numCandidates < cands && all_valid) {
        uint32_t fringeId = candidates[fringeStart].lineId;
        Address fringeAddr = array[fringeId];
        assert(fringeAddr);
        for (uint32_t w = 0; w < ways; w++) {
            uint32_t hval = hf->hash(w, fringeAddr) & setMask;
            uint32_t pos = w*numSets + hval;
            uint32_t lineId = lookupArray[pos];

            // Logically, you want to do this...
#if 0
            if (lineId != fringeId) {
                //info("Candidate %d way %d addr 0x%lx pos %d lineId %d parent %d", numCandidates, w, array[lineId], pos, lineId, fringeStart);
                candidates[numCandidates++].set(pos, lineId, (int32_t)fringeStart);
                all_valid &= (array[lineId] != 0);
            }
#endif
            // But this compiles as a branch and ILP sucks (this data-dependent branch is long-latency and mispredicted often)
            // Logically though, this is just checking for whether we're revisiting ourselves, so we can eliminate the branch as follows:
            candidates[numCandidates].set(pos, lineId, (int32_t)fringeStart);
            all_valid &= (array[lineId] != 0);  // no problem, if lineId == fringeId the line's already valid, so no harm done
            numCandidates += (lineId != fringeId); // if lineId == fringeId, the cand we just wrote will be overwritten
        }
        fringeStart++;
    }

    //Get best candidate (NOTE: This could be folded in the code above, but it's messy since we can expand more than zassoc elements)
    assert(!all_valid || numCandidates >= cands);
    numCandidates = (numCandidates > cands)? cands : numCandidates;

    //info("Using %d candidates, all_valid=%d", numCandidates, all_valid);

    uint32_t bestCandidate = rp->rankCands(req, ZCands(&candidates[0], &candidates[numCandidates]));
    assert(bestCandidate < numLines);

    //Fill in swap array

    //Get the *minimum* index of cands that matches lineId. We need the minimum in case there are loops (rare, but possible)
    uint32_t minIdx = -1;
    for (uint32_t ii = 0; ii < numCandidates; ii++) {
        if (bestCandidate == candidates[ii].lineId) {
            minIdx = ii;
            break;
        }
    }
    assert(minIdx >= 0);
    //info("Best candidate is %d lineId %d", minIdx, bestCandidate);

    lastCandIdx = minIdx; //used by timing simulation code to schedule array accesses

    int32_t idx = minIdx;
    uint32_t swapIdx = 0;
    while (idx >= 0) {
        swapArray[swapIdx++] = candidates[idx].pos;
        idx = candidates[idx].parentIdx;
    }
    swapArrayLen = swapIdx;
    assert(swapArrayLen > 0);

    //Write address of line we're replacing
    *wbLineAddr = array[bestCandidate];

    return bestCandidate;
}

void ZArray::postinsert(const Address lineAddr, const MemReq* req, uint32_t candidate) {
    //We do the swaps in lookupArray, the array stays the same
    assert(lookupArray[swapArray[0]] == candidate);
    for (uint32_t i = 0; i < swapArrayLen-1; i++) {
        //info("Moving position %d (lineId %d) <- %d (lineId %d)", swapArray[i], lookupArray[swapArray[i]], swapArray[i+1], lookupArray[swapArray[i+1]]);
        lookupArray[swapArray[i]] = lookupArray[swapArray[i+1]];
    }
    lookupArray[swapArray[swapArrayLen-1]] = candidate; //note that in preinsert() we walk the array backwards when populating swapArray, so the last elem is where the new line goes
    //info("Inserting lineId %d in position %d", candidate, swapArray[swapArrayLen-1]);

    rp->replaced(candidate);
    array[candidate] = lineAddr;
    rp->update(candidate, req);

    statSwaps.inc(swapArrayLen-1);
}


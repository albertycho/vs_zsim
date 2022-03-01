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

#include "coherence_ctrls.h"
#include "cache.h"
#include "network.h"

/* Do a simple XOR block hash on address to determine its bank. Hacky for now,
 * should probably have a class that deals with this with a real hash function
 * (TODO)
 */
uint32_t MESIBottomCC::getParentId(Address lineAddr) {
    //Hash things a bit
    uint32_t res = 0;
    uint64_t tmp = lineAddr;
    for (uint32_t i = 0; i < 4; i++) {
        res ^= (uint32_t) ( ((uint64_t)0xffff) & tmp);
        tmp = tmp >> 16;
    }
    return (res % parents.size());
}


void MESIBottomCC::init(const g_vector<MemObject*>& _parents, Network* network, const char* name) {
    parents.resize(_parents.size());
    parentRTTs.resize(_parents.size());
    for (uint32_t p = 0; p < parents.size(); p++) {
        parents[p] = _parents[p];
        parentRTTs[p] = (network)? network->getRTT(name, parents[p]->getName()) : 0;
    }
}


uint64_t MESIBottomCC::processEviction(Address wbLineAddr, int32_t lineId, bool lowerLevelWriteback, uint64_t cycle, uint32_t srcId, bool no_record) {

    uint32_t norecord_flag = 0;
    if (no_record) {
        norecord_flag = MemReq::NORECORD;
    }
    assert(lineId > -1);
    MESIState* state = &array[lineId];
    if (lowerLevelWriteback) {
        //If this happens, when tcc issued the invalidations, it got a writeback. This means we have to do a PUTX, i.e. we have to transition to M if we are in E
        assert(*state == M || *state == E); //Must have exclusive permission!
        *state = M; //Silent E->M transition (at eviction); now we'll do a PUTX
    }
    uint64_t respCycle = cycle;
    switch (*state) {
        case I:
            break; //Nothing to do
        case S:
        case E:
            {
                MemReq req = {wbLineAddr, PUTS, selfId, state, cycle, &ccLock, *state, srcId,  norecord_flag /*0 no flags*/};
                respCycle = parents[getParentId(wbLineAddr)]->access(req);
            }
            break;
        case M:
            {
                MemReq req = {wbLineAddr, PUTX, selfId, state, cycle, &ccLock, *state, srcId, norecord_flag /*0 no flags*/};
                respCycle = parents[getParentId(wbLineAddr)]->access(req);
            }
            break;

        default: panic("!?, lineId %d",lineId);
    }
    assert_msg(*state == I, "Wrong final state %s on eviction", MESIStateName(*state));
    return respCycle;
}

uint64_t MESIBottomCC::passToNext( Address lineAddr, AccessType type, uint32_t childId, uint32_t srcId, uint32_t flags, uint64_t cycle) {
    MESIState dummyState = MESIState::I;
    uint32_t parentId = getParentId(lineAddr);
    MemReq req = {lineAddr, type, childId, &dummyState, cycle, &ccLock, dummyState, srcId, flags};
    return parents[parentId]->access(req);
}

uint64_t MESIBottomCC::processAccess(Address lineAddr, int32_t lineId, AccessType type, uint64_t cycle, uint32_t srcId, uint32_t flags, bool is_llc) {
    uint64_t respCycle = cycle;
    MESIState* state;
    if (lineId != -1) {
        state = &array[lineId];
    }
    else {
        state = (MESIState*)malloc(sizeof(MESIState));
        *state = I;
    }
    switch (type) {
        // A PUTS/PUTX does nothing w.r.t. higher coherence levels --- it dies here
        case PUTS: //Clean writeback, nothing to do (except profiling)
            assert(*state != I);
            profPUTS.inc();
            break;
        case PUTX: //Dirty writeback
            assert(*state == M || *state == E);
            if (*state == E) {
                //Silent transition, record that block was written to
                *state = M;
            }
            profPUTX.inc();
            break;
        case GETS:
            if (*state == I) {
                uint32_t parentId = getParentId(lineAddr);
                MemReq req = {lineAddr, GETS, selfId, state, cycle, &ccLock, *state, srcId, flags};    
                uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
                uint32_t netLat = parentRTTs[parentId];
                profGETNextLevelLat.inc(nextLevelLat);
                profGETNetLat.inc(netLat);
                respCycle += nextLevelLat + netLat;
                profGETSMiss.inc();
                assert(*state == S || *state == E || (*state == I && (req.flags & MemReq::PKTOUT)));
            } else {
                profGETSHit.inc();
            }
            //TODO: Albert - add invaliate if flags & READNINV here?
            if((flags & MemReq::READNINV) && (is_llc)){
                //if(srcId==3) info("BottomCC: readNinv, state=%d", *state);
                //downgrade from M to E. Assume Recv buffers are affiliated to cores so no shared
                //considered setting to INV, but coherence won't work for inclusive cache
                *state=E;
            }
            break;
        case GETX:
            if (flags & MemReq::PKTOUT) { // this is an eggress access directed to the LLC that also invalidates
                assert(flags >> 16 == 0);
                if (*state == S || *state == E || *state == M) { // data is present in the core's cache hierarchy, invalidate them
                    *state = I;
                }
                else {  // go to memory 
                    uint32_t parentId = getParentId(lineAddr);
                    MemReq req = {lineAddr, GETX, selfId, state, cycle, &ccLock, *state, srcId, flags};  // we have reached the correct level for ingress, the following requests downwards should have the pktin flag set
                    uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
                    uint32_t netLat = parentRTTs[parentId];
                    profGETNextLevelLat.inc(nextLevelLat);
                    profGETNetLat.inc(netLat);
                    respCycle += nextLevelLat + netLat;
                }
                assert(*state == I);
            } else {
                if (*state == I || *state == S) {
                    //Profile before access, state changes
                    
                    if (*state == I) profGETXMissIM.inc();
                    else profGETXMissSM.inc();

                    if ((flags & MemReq::PKTIN) && (flags >> 16 == 0)) {    // this is an ingress packed that is directed to the LLC and it missed, so we don't go to memory
                        //info("ddio ingress missed in llc, don't go to mem, state %s",MESIStateName(*state));
                        *state = M;
                        respCycle++;
                    } else {
                        uint32_t parentId = getParentId(lineAddr);
                        MemReq req = {lineAddr, GETX, selfId, state, cycle, &ccLock, *state, srcId, flags & ~MemReq::PKTIN};  // we have reached the correct level for ingress, the following requests downwards should have the pktin flag set
                        uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
                        uint32_t netLat = parentRTTs[parentId];
                        profGETNextLevelLat.inc(nextLevelLat);
                        profGETNetLat.inc(netLat);
                        respCycle += nextLevelLat + netLat;
                    }
                } else {
                    if (*state == E) {
                        // Silent transition
                        // NOTE: When do we silent-transition E->M on an ML hierarchy... on a GETX, or on a PUTX?
                        /* Actually, on both: on a GETX b/c line's going to be modified anyway, and must do it if it is the L1 (it's OK not
                        * to transition if L2+, we'll TX on the PUTX or invalidate, but doing it this way minimizes the differences between
                        * L1 and L2+ controllers); and on a PUTX, because receiving a PUTX while we're in E indicates the child did a silent
                        * transition and now that it is evictiong, it's our turn to maintain M info.
                        */
                        *state = M;
                    }
                    profGETXHit.inc();
                }
            }
            assert_msg(*state == M, "Wrong final state on GETX, lineId %d numLines %d, finalState %s", lineId, numLines, MESIStateName(*state));
            break;

        default: panic("!?");
    }
    assert_msg(respCycle >= cycle, "XXX %ld %ld", respCycle, cycle);
    return respCycle;
}

void MESIBottomCC::processWritebackOnAccess(Address lineAddr, int32_t lineId, AccessType type) {
    assert(lineId > -1);
    MESIState* state = &array[lineId];
    assert(*state == M || *state == E);
    if (*state == E) {
        //Silent transition to M if in E
        *state = M;
    }
}

void MESIBottomCC::processInval(Address lineAddr, int32_t lineId, InvType type, bool* reqWriteback) {
    assert(lineId > -1);
    MESIState* state = &array[lineId];
    //assert(*state != I);
    if (*state == I) {
        return;
    }
    switch (type) {
        case INVX: //lose exclusivity
            //Hmmm, do we have to propagate loss of exclusivity down the tree? (nah, topcc will do this automatically -- it knows the final state, always!)
            assert_msg(*state == E || *state == M, "Invalid state %s", MESIStateName(*state));
            if (*state == M) *reqWriteback = true;
            *state = S;
            profINVX.inc();
            break;
        case INV: //invalidate
            assert(*state != I);
            if (*state == M) *reqWriteback = true;
            *state = I;
            profINV.inc();
            break;
        case FWD: //forward
            assert_msg(*state == S, "Invalid state %s on FWD", MESIStateName(*state));
            profFWD.inc();
            break;
        default: panic("!?");
    }
    //NOTE: BottomCC never calls up on an invalidate, so it adds no extra latency
}


uint64_t MESIBottomCC::processNonInclusiveWriteback(Address lineAddr, AccessType type, uint64_t cycle, MESIState* state, uint32_t srcId, uint32_t flags) {
    if (!nonInclusiveHack) panic("Non-inclusive %s on line 0x%lx, this cache should be inclusive", AccessTypeName(type), lineAddr);

    //info("Non-inclusive wbackon line 0x%lx, forwarding",lineAddr);
    MemReq req = {lineAddr, type, selfId, state, cycle, &ccLock, *state, srcId, flags | MemReq::NONINCLWB};
    uint64_t respCycle = parents[getParentId(lineAddr)]->access(req);
    assert(*state == I);
    return respCycle;
}


/* MESITopCC implementation */

void MESITopCC::init(const g_vector<BaseCache*>& _children, Network* network, const char* name) {
    if (_children.size() > MAX_CACHE_CHILDREN) {
        panic("[%s] Children size (%d) > MAX_CACHE_CHILDREN (%d)", name, (uint32_t)_children.size(), MAX_CACHE_CHILDREN);
    }
    children.resize(_children.size());
    childrenRTTs.resize(_children.size());
    for (uint32_t c = 0; c < children.size(); c++) {
        children[c] = _children[c];
        childrenRTTs[c] = (network)? network->getRTT(name, children[c]->getName()) : 0;
    }
}

uint64_t MESITopCC::sendInvalidates(Address lineAddr, int32_t lineId, InvType type, bool* reqWriteback, uint64_t cycle, uint32_t srcId) {
    //Send down downgrades/invalidates
    //assert(lineId > -1);
    /*
    if (lineId == -1 && evicted_lines.count(lineAddr) > 0) { // i am the llc but don't have the line; check evicted_lines 
        uint32_t numChildren = children.size();
        uint32_t sentInvs = 0;
        uint64_t maxCycle = cycle;
        Entry* e = &evicted_lines[lineAddr];
        for (uint32_t c = 0; c < numChildren; c++) {
            if()
            InvReq req = {lineAddr, type, reqWriteback, cycle, srcId};
            uint64_t respCycle = children[c]->invalidate(req);
            if(respCycle != req.cycle)
                respCycle += childrenRTTs[c];
            maxCycle = MAX(respCycle, maxCycle);
        }
        return maxCycle;
    }
    */
    Entry* e;
    
    if (lineId == -1) {
        panic("!?");
        if (evicted_lines.count(lineAddr) > 0) {
            e = &evicted_lines[lineAddr];
            assert(!e->isEmpty());
        }
        else {
            return cycle;
        }
    }
    else {
        e = &array[lineId];
    }
     
    //Don't propagate downgrades if sharers are not exclusive.
    if (type == INVX && !e->isExclusive()) {
        return cycle;
    }
    
    uint64_t maxCycle = cycle; //keep maximum cycle only, we assume all invals are sent in parallel
    if (!e->isEmpty()) {
        uint32_t numChildren = children.size();
        uint32_t sentInvs = 0;
        for (uint32_t c = 0; c < numChildren; c++) {
            if (e->sharers[c]) {
                InvReq req = {lineAddr, type, reqWriteback, cycle, srcId};
                uint64_t respCycle = children[c]->invalidate(req);
                respCycle += childrenRTTs[c];
                maxCycle = MAX(respCycle, maxCycle);
                if (type == INV) e->sharers[c] = false;
                sentInvs++;
            }
        }
        assert(sentInvs == e->numSharers);
        if (type == INV) {
            e->numSharers = 0;
        } else {
            //TODO: This is kludgy -- once the sharers format is more sophisticated, handle downgrades with a different codepath
            assert(e->exclusive);
            assert(e->numSharers == 1);
            e->exclusive = false;
        }
    }
    return maxCycle;
}

void MESITopCC::processNonInclusiveWriteback(Address lineAddr, uint64_t srcId) {
    if (!nonInclusiveHack) panic("this cache should be inclusive");
    if (evicted_lines.count(lineAddr) > 0) {
        Entry *e = &evicted_lines[lineAddr];
        assert(e->sharers[srcId]);
        if(e->numSharers == 1)
            evicted_lines.erase(lineAddr);
        else {
            evicted_lines[lineAddr].sharers[srcId] = false;
            evicted_lines[lineAddr].numSharers--;
        }
    }
    return;
}

uint64_t MESITopCC::processEviction(Address wbLineAddr, int32_t lineId, bool* reqWriteback, uint64_t cycle, uint32_t srcId) {  
    if (nonInclusiveHack) {
        // Don't invalidate anything, just clear our entry
        assert(lineId > -1);
        assert(evicted_lines.count(wbLineAddr)==0);
        if (array[lineId].numSharers > 0) {
            evicted_lines[wbLineAddr] = array[lineId];
        }
        array[lineId].clear();
        //info("eviction from the llc on line 0x%lx",wbLineAddr);
        return cycle;
    } else {
        //Send down invalidates
        return sendInvalidates(wbLineAddr, lineId, INV, reqWriteback, cycle, srcId);
    }
}

uint64_t MESITopCC::processAccess(Address lineAddr, int32_t lineId, AccessType type, uint32_t childId, bool haveExclusive,
                                  MESIState* childState, bool* inducedWriteback, uint64_t cycle, uint32_t srcId, uint32_t flags) {
    Entry* e; 
    if (lineId != -1)
        e = &array[lineId];
    else {
        e = nullptr; //(Entry*)malloc(sizeof(Entry));
    }

    uint64_t respCycle = cycle;
    switch (type) {
        case PUTX:
            assert(e->isExclusive());
            if (flags & MemReq::PUTX_KEEPEXCL) {
                assert(e->sharers[childId]);
                assert(*childState == M);
                *childState = E; //they don't hold dirty data anymore
                break; //don't remove from sharer set. It'll keep exclusive perms.
            }
            //note NO break in general
        case PUTS:
            assert(e->sharers[childId]);
            e->sharers[childId] = false;
            e->numSharers--;
            *childState = I;
            break;
        case GETS:
        assert(!(flags & MemReq::PKTIN));
            // should a GETS from the NIC modify any cache state? I think not (unless it finds an invalid line, which we deal with in bcc)
            if(!(flags & MemReq::PKTOUT)) {

                if ((e->isEmpty() && haveExclusive && !(flags & MemReq::NOEXCL) && evicted_lines.count(lineAddr) == 0)
                    || (e->isEmpty() && evicted_lines.count(lineAddr) == 1 && evicted_lines[lineAddr].sharers[childId] == true && evicted_lines[lineAddr].numSharers == 1)) {
                    //Give in E state
                    e->exclusive = true;
                    e->sharers[childId] = true;
                    e->numSharers = 1;
                    *childState = E;
                    evicted_lines.erase(lineAddr);
                } else {
                    //Give in S state
                    assert(e->sharers[childId] == false);

                    if (evicted_lines.count(lineAddr) > 0) {
                        Entry* evct = &evicted_lines[lineAddr];
                        uint32_t numChildren = children.size();
                        for (uint32_t c = 0; c < numChildren; c++) {
                            if (evct->sharers[c]) {
                                e->sharers[c] = true;
                                e->numSharers++;
                                if (evct->exclusive) {
                                    assert(!e->exclusive);
                                    e->exclusive = true;
                                }
                            }
                        }
                        evicted_lines.erase(lineAddr);
                    }

                    if (e->isExclusive()) {
                        //Downgrade the exclusive sharer
                        respCycle = sendInvalidates(lineAddr, lineId, INVX, inducedWriteback, cycle, srcId);
                    }

                    assert_msg(!e->isExclusive(), "Can't have exclusivity here. isExcl=%d excl=%d numSharers=%d", e->isExclusive(), e->exclusive, e->numSharers);

                    if(!e->sharers[childId]) {
                        e->sharers[childId] = true;
                        e->numSharers++;
                        e->exclusive = false; //dsm: Must set, we're explicitly non-exclusive
                    }
                    *childState = S;
                }
            }
            break;
        case GETX:
            assert((flags & MemReq::PKTOUT) || haveExclusive); //the current cache better have exclusive access to this line

            if (evicted_lines.count(lineAddr) > 0) {
                Entry* evct = &evicted_lines[lineAddr];
                uint32_t numChildren = children.size();
                for (uint32_t c = 0; c < numChildren; c++) {
                    if (evct->sharers[c]) {
                        e->sharers[c] = true;
                        e->numSharers++;
                    }
                }
                evicted_lines.erase(lineAddr);
            }

            // if we write directly to the l2/llc, we want all children to be invalidated
            if (!((flags & MemReq::PKTIN) && childId > MAX_CACHE_CHILDREN)) {
                // If child is in sharers list (this is an upgrade miss), take it out
                if (e->sharers[childId]) {
                    assert_msg(!e->isExclusive(), "Spurious GETX, childId=%d numSharers=%d isExcl=%d excl=%d", childId, e->numSharers, e->isExclusive(), e->exclusive);
                    e->sharers[childId] = false;
                    e->numSharers--;
                    assert(e->numSharers>=0);
                }
            }
/*
            if (childId == 0xDA0000) {//direct access
                //info("directAccess");
                // Invalidate all other copies
                respCycle = sendInvalidates(lineAddr, lineId, INV, inducedWriteback, cycle, srcId);
                assert(e->numSharers == 0);
                e->exclusive = true;
            }
            else {
                // If child is in sharers list (this is an upgrade miss), take it out
                if (e->sharers[childId]) {
                    assert_msg(!e->isExclusive(), "Spurious GETX, childId=%d numSharers=%d isExcl=%d excl=%d", childId, e->numSharers, e->isExclusive(), e->exclusive);
                    e->sharers[childId] = false;
                    e->numSharers--;
                }
*/

                // Invalidate all other copies
            respCycle = sendInvalidates(lineAddr, lineId, INV, inducedWriteback, cycle, srcId);
            
            if (flags & MemReq::PKTOUT)
                e->exclusive = false;       // this is an egress access that invalidates the LLC too
            else
                e->exclusive = true;

            if (!((flags & MemReq::PKTIN) && childId > MAX_CACHE_CHILDREN)) {
                // Set current sharer, mark exclusive
                e->sharers[childId] = true;
                e->numSharers++;

                assert(e->numSharers == 1);

                *childState = M; //give in M directly
            
/*
                // Set current sharer, mark exclusive
                e->sharers[childId] = true;
                e->numSharers++;
                e->exclusive = true;

                assert(e->numSharers == 1);

                *childState = M; //give in M directly
*/
            }

            break;

        default: panic("!?");
    }

    return respCycle;
}

uint64_t MESITopCC::processInval(Address lineAddr, int32_t lineId, InvType type, bool* reqWriteback, uint64_t cycle, uint32_t srcId) {
    if (type == FWD) {//if it's a FWD, we should be inclusive for now, so we must have the line, just invLat works
        assert(!nonInclusiveHack); //dsm: ask me if you see this failing and don't know why
        return cycle;
    } else {
        //Just invalidate or downgrade down to children as needed
        return sendInvalidates(lineAddr, lineId, type, reqWriteback, cycle, srcId);
    }
}


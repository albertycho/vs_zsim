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
#include "zsim.h"


uint64_t get_stat_group(uint64_t srcId){
    //assuming we will only run upto 2 distinct loadgen
    void* lg_p_vp = static_cast<void*>(gm_get_lg_ptr());
    load_generator* lg_p = (load_generator*)lg_p_vp;
    
    uint64_t num_lg = lg_p->num_loadgen;
    for(uint64_t i=0; i<num_lg;i++){
        for(uint64_t j=0;j<lg_p->lgs[i].num_cores;j++){
            if(srcId==lg_p->lgs[i].core_ids[j]){
                return i;
            }
        }
    }
    

    return NNF; //didn't belong to a server for NF
}

int get_target_core_id_from_rb_addr(Address lineaddr){
    uint64_t num_cores = zinfo->numCores;
	uint64_t start_core = 3; //0,1,2 are maintenance cores, not server
    for(int i=start_core; i<num_cores;i++){
        uint64_t rb_base=(uint64_t) nicInfo->nic_elem[i].recv_buf;
        uint64_t rb_top =rb_base+nicInfo->recv_buf_pool_size;
        uint64_t rb_base_line=rb_base>>lineBits;
        uint64_t rb_top_line = rb_top>>lineBits;
		//rb_base_line--; //give room for misaligned buffers
		//rb_top_line++;
        if (lineaddr >= rb_base_line && lineaddr <= rb_top_line) {
            return i;
        }
    }
    
    return -1;
}
int print_rb_bases(){
    uint64_t num_cores = zinfo->numCores;
    for(int i=0; i<num_cores;i++){
        uint64_t rb_base=(uint64_t) nicInfo->nic_elem[i].recv_buf;
        uint64_t rb_top =rb_base+nicInfo->recv_buf_pool_size;
		info("core %d rb_base: %X, \t rb_top: %X",i,rb_base,rb_top);
    }
    
    return -1;
}


int get_target_core_id_from_lb_addr(Address lineaddr){
    uint64_t num_cores = zinfo->numCores;
    for(int i=0; i<num_cores;i++){
        uint64_t lb_base=(uint64_t) nicInfo->nic_elem[i].lbuf;
        //uint64_t lb_top =lb_base+256*nicInfo->forced_packet_size;
        uint64_t lb_top =lb_base+64*nicInfo->forced_packet_size;
        uint64_t lb_base_line=lb_base>>lineBits;
        uint64_t lb_top_line = lb_top>>lineBits;
        
        if (lineaddr >= lb_base_line && lineaddr <= lb_top_line) {
            return i;
        }
    }
    
    return -1;
}



#include "zsim.h"

/* Do a simple XOR block hash on address to determine its bank. Hacky for now,
 * should probably have a class that deals with this with a real hash function
 * (TODO)
 */
uint32_t MESIBottomCC::getParentId(Address lineAddr) {
	//for RB address, see if we can not hash
	//int rb_cid = get_target_core_id_from_rb_addr(lineAddr);
	uint32_t policy = nicInfo->getParentId_policy;
	if(policy==0){
		uint64_t bank_id = (lineAddr>>11) % parents.size(); //shifted by number of sets
		return bank_id;
	}
	//if(parents.size()==24){
	//	if(rb_cid!=-1){
	//		nicInfo->rb_bank_hist[bank_id]++;
	//	}
	//	else{
	//		nicInfo->non_rb_bank_hist[bank_id]++;
	//	}
	//}


	else if(policy==1){
    //Hash things a bit
    	uint32_t res = 0;
    	uint64_t tmp = lineAddr;
    	for (uint32_t i = 0; i < 4; i++) {
    	    res ^= (uint32_t) ( ((uint64_t)0xffff) & tmp);
    	    tmp = tmp >> 16;
    	}

		//uint32_t bid = (res % parents.size());
		////if parent = l3
		//if(parents.size()==24){
		//	if(rb_cid!=-1){
		//		nicInfo->rb_bank_hist[bid]++;
		//	}
		//	else{
		//		nicInfo->non_rb_bank_hist[bid]++;
		//	}
		//}
    	return (res % parents.size());
	}

	uint64_t bank_id = (lineAddr>>11) % parents.size(); //shifted by number of sets
    return bank_id;
}


void MESIBottomCC::init(const g_vector<MemObject*>& _parents, Network* network, const char* name) {
    parents.resize(_parents.size());
    parentRTTs.resize(_parents.size());
    for (uint32_t p = 0; p < parents.size(); p++) {
        parents[p] = _parents[p];
        parentRTTs[p] = (network)? network->getRTT(name, parents[p]->getName()) : 0;
    }
}


uint64_t MESIBottomCC::processEviction(Address wbLineAddr, int32_t lineId, bool lowerLevelWriteback, uint64_t cycle, uint32_t srcId, uint32_t flags) {

    uint32_t evict_flag = 0;
    if (flags & MemReq::NORECORD) {
        evict_flag |= MemReq::NORECORD;
    }
    if (flags & MemReq::INGR_EVCT) {
        evict_flag |= MemReq::INGR_EVCT;
    }
    if (flags & MemReq::EGR_EVCT) {
        evict_flag |= MemReq::EGR_EVCT;
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
                MemReq req = {wbLineAddr, PUTS, selfId, state, cycle, &ccLock, *state, srcId,  evict_flag /*0 no flags*/};
                respCycle = parents[getParentId(wbLineAddr)]->access(req);
            }
            break;
        case M:
            {
                MemReq req = {wbLineAddr, PUTX, selfId, state, cycle, &ccLock, *state, srcId, evict_flag /*0 no flags*/};
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

uint64_t MESIBottomCC::processAccess(Address lineAddr, int32_t lineId, AccessType type, uint64_t cycle, uint32_t srcId, uint32_t flags, bool existsInPriv) {
    uint64_t respCycle = cycle;
    MESIState* state;
    bool isMiss = false;
    bool isEgrToMem = false;
    if (lineId != -1) {
        state = &array[lineId];
    }
    else {
        state = (MESIState*)malloc(sizeof(MESIState));
        *state = I;
        isEgrToMem = true;
    }
    switch (type) {
        // A PUTS/PUTX does nothing w.r.t. higher coherence levels --- it dies here
        case PUTS: //Clean writeback, nothing to do (except profiling)
            isMiss = (*state == I);
            if (nonInclusiveHack) 
                *state = S;
            else
                assert(*state != I);
            profPUTS.inc();
            break;
        case PUTX: //Dirty writeback
            assert(*state == M || *state == E || (*state == I && nonInclusiveHack));
            isMiss = (*state == I);
            if (*state == E || *state == I) {
                //Silent transition, record that block was written to
                *state = M;
            }
            profPUTX.inc();
            break;
        case GETS:
            if (*state == I) {
                if (!(flags & MemReq::PKTOUT) || (flags & MemReq::PKTOUT && isEgrToMem)) {  // Egress DDIO tried to read from llc, missed and there is no copy in private caches
                    uint32_t parentId = getParentId(lineAddr);
                    MemReq req = {lineAddr, GETS, selfId, state, cycle, &ccLock, *state, srcId, flags};    
                    uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
                    uint32_t netLat = parentRTTs[parentId];
                    profGETNextLevelLat.inc(nextLevelLat);
                    profGETNetLat.inc(netLat);
                    respCycle += nextLevelLat + netLat;
                    profGETSMiss.inc();
                    isMiss = true;
                    assert(*state == S || *state == E || (*state == I && flags & MemReq::PKTOUT && isEgrToMem));
                }
                else if (flags & MemReq::PKTOUT && !isEgrToMem) { // Egress DDIO tried to read from llc, missed but there is a copy in private caches, bring it to the llc
                    *state = E; // assume that the priv copy  is clean, if not the inducedWriteback will take care of it
                }
            } else {
                profGETSHit.inc();
            }
            //TODO: Albert - add invaliate if flags & READNINV here?
            //if((flags & MemReq::READNINV) && (is_llc)){
            //    //downgrade from M to E. Assume Recv buffers are affiliated to cores so no shared
            //    //considered setting to INV, but coherence won't work for inclusive cache
            //    *state=E;
            //}
            break;
        case GETX:
            if (flags & MemReq::PKTOUT) { // this is an eggress access directed to the LLC that also invalidates
                assert(flags >> 16 == 0);
                if (*state == S || *state == E || *state == M) { // data is present in the LLC, invalidate them
                    *state = I;
                }
                
                else if (!existsInPriv && isEgrToMem){  //  data not present in the private caches or the llc, go to memory 
                    uint32_t parentId = getParentId(lineAddr);
                    MemReq req = {lineAddr, GETX, selfId, state, cycle, &ccLock, *state, srcId, flags};  // we have reached the correct level for ingress, the following requests downwards should have the pktin flag set
                    uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
                    uint32_t netLat = parentRTTs[parentId];
                    profGETNextLevelLat.inc(nextLevelLat);
                    profGETNetLat.inc(netLat);
                    respCycle += nextLevelLat + netLat;
                    isMiss = true;
                }
                /*
                else {  // data isn't present in the llc, but is in private caches
                    assert(*state == I);
                }
                */
                assert(*state == I);
            } else {
                if (*state == I || *state == S) {
                    //Profile before access, state changes
                    
                    if (*state == I) profGETXMissIM.inc();
                    else profGETXMissSM.inc();
                    
                    isMiss = true;

                    if ((flags & MemReq::PKTIN) && (flags >> 16 == 0)) {    // this is an ingress packed that is directed to the LLC and it missed, so we don't go to memory
                        //info("ddio ingress missed in llc, don't go to mem, state %s",MESIStateName(*state));
                        if (*state == S) {
                            isMiss = false;
                        }
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
            assert_msg(*state == M || (flags & MemReq::PKTOUT && *state == I), "Wrong final state on GETX, lineId %d numLines %d, finalState %s", lineId, numLines, MESIStateName(*state));
            break;
        case CLEAN:
            {
                *state = I;     // invalidate copy
                uint32_t parentId = getParentId(lineAddr);
                MemReq req = {lineAddr, CLEAN, selfId, state, cycle, &ccLock, *state, srcId, flags};  // we have reached the correct level for ingress, the following requests downwards should have the pktin flag set
                uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
                uint32_t netLat = parentRTTs[parentId];
                respCycle += nextLevelLat + netLat;
            }
            break;
        case CLEAN_S:
            {
                /*
                int i=3;
                glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
                bool ex = false;
                while (i < nicInfo->expected_core_count + 3){
                    Address base_ing = (Address)(nicInfo->nic_elem[i].recv_buf) >> lineBits;
                    uint64_t size_ing = nicInfo->recv_buf_pool_size; 
                    Address top_ing = ((Address)(nicInfo->nic_elem[i].recv_buf) + size_ing) >> lineBits;
                    Address base_egr = (Address)(nicInfo->nic_elem[i].lbuf) >> lineBits;
                    uint64_t size_egr = 256*nicInfo->forced_packet_size;
                    Address top_egr = ((Address)(nicInfo->nic_elem[i].lbuf) + size_egr) >> lineBits;
                    if (lineAddr >= base_ing && lineAddr <= top_ing) {
                        ex = true;
                        break;
                    }
                    i++;
                }
                assert(ex);
                if (*state == I) {
                    profCleanMiss.inc();
                }
                else {
                    profCleanHit.inc();
                    if (nonInclusiveHack) {
                        if (*state == M)
                            *state = S;
                    }
                    else 
                        *state = I;
                }
                */
                uint32_t parentId = getParentId(lineAddr);
                MemReq req = {lineAddr, CLEAN_S, selfId, state, cycle, &ccLock, *state, srcId, flags}; 
                uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
                uint32_t netLat = parentRTTs[parentId];
                respCycle += nextLevelLat + netLat;
            }
            break;
        default: panic("!?");
    }

    /*
    uint64_t stat_group=get_stat_group(srcId);
    
    if (type != PUTS && type != PUTX && type != CLEAN  && type != CLEAN_S) {
        if (flags & MemReq::NETRELATED_ING) {
            if (srcId > 1) {
                switch (stat_group) {
                    case NF0: 
                        if(isMiss)
                            netMiss_core_rb.inc();
                        else 
                            netHit_core_rb.inc();
                        break;
                    case NF1:
                        if(isMiss)
                            netMiss_core_rb_grp1.inc();
                        else 
                            netHit_core_rb_grp1.inc();
                        break;
                    default: 
                		int core_id=get_target_core_id_from_rb_addr(lineAddr);
						info("srcId: %d, rb_coreID: %d",srcId, core_id);
						panic("core rb should be for NF0 or NF1");
                }

            }
            else {
                if(!(nicInfo->zeroCopy)){
                    assert (flags & MemReq::PKTIN);
                }
                else{
                    assert ((flags & MemReq::PKTOUT)||(flags & MemReq::PKTIN));
                }
                int core_id=get_target_core_id_from_rb_addr(lineAddr);
                assert(core_id>=0);
                uint64_t nic_stat_group=get_stat_group(core_id);
                if(flags & MemReq::PKTIN){
                    switch (nic_stat_group) {
                        case NF0: 
                            if(isMiss)
                                netMiss_nic_rb.inc();
                            else 
                                netHit_nic_rb.inc();
                            break;
                        case NF1:
                            if(isMiss)
                                netMiss_nic_rb_grp1.inc();
                            else 
                                netHit_nic_rb_grp1.inc();
                            break;
                        default:
                            info("core_id: %d, Addr: %X, nic_stat_group: %d", core_id, lineAddr<<lineBits, nic_stat_group);
							print_rb_bases();
							panic("nic rb should be for NF0 or NF1");
                    }
                }
                else if(flags & MemReq::PKTOUT){//zero copy, count these as LB miss
                    switch (nic_stat_group) {
                        case NF0: 
                            if(isMiss)
                                netMiss_nic_lb.inc();
                            else 
                                netHit_nic_lb.inc();
                            break;
                        case NF1:
                            if(isMiss)
                                netMiss_nic_lb_grp1.inc();
                            else 
                                netHit_nic_lb_grp1.inc();
                            break;
                        default: 
                            info("core_id: %d, Addr: %X, nic_stat_group: %d", core_id, lineAddr<<lineBits, nic_stat_group);
                            panic("core lb should be for NF0 or NF1");
                    }

                }
            } 
        }   
        else if (flags & MemReq::NETRELATED_EGR) {          
            if (srcId > 1) {

                switch (stat_group) {
                    case NF0: 
                        if(isMiss)
                            netMiss_core_lb.inc();
                        else 
                            netHit_core_lb.inc();
                        break;
                    case NF1:
                        if(isMiss)
                            netMiss_core_lb_grp1.inc();
                        else 
                            netHit_core_lb_grp1.inc();
                        break;
                    default: panic("core lb should be for NF0 or NF1");
                }
            }
            else {
                assert(flags & MemReq::PKTOUT);
                int core_id=get_target_core_id_from_lb_addr(lineAddr);
                assert(core_id>=0);
                uint64_t nic_stat_group=get_stat_group(core_id);
                
                switch (nic_stat_group) {
                    case NF0: 
                        if(isMiss)
                            netMiss_nic_lb.inc();
                        else 
                            netHit_nic_lb.inc();
                        break;
                    case NF1:
                        if(isMiss)
                            netMiss_nic_lb_grp1.inc();
                        else 
                            netHit_nic_lb_grp1.inc();
                        break;
                    default: panic("nic lb should be for NF0 or NF1");
                }
            }
                        
        }
        else {
            if (srcId > 2) {
                switch (stat_group) {
                    case NF0: 
                        if(isMiss)
                            appMiss.inc();
                        else 
                            appHit.inc();
                        break;
                    case NF1:
                        if(isMiss)
                            appMiss_grp1.inc();
                        else 
                            appHit_grp1.inc();
                        break;
                    case NNF:
                        if(isMiss)
                            appMiss_NNF.inc();
                        else 
                            appHit_NNF.inc();
                        break;
                    default: panic("app access didn't belong to any grp?");
                }

            }
            else {
                if(isMiss)
                    nicMiss.inc();
                else
                    nicHit.inc();
            }
        }
    }
    else if (srcId > 1 && type != CLEAN && type != CLEAN_S) {
        switch (stat_group) {
            case NF0: 
                if(isMiss)
                    appPutMiss.inc();
                else 
                    appPutHit.inc();
                break;
            case NF1:
                if(isMiss)
                    appPutMiss_grp1.inc();
                else 
                    appPutHit_grp1.inc();
                break;
            case NNF:
                if(isMiss)
                    appPutMiss_NNF.inc();
                else 
                    appPutHit_NNF.inc();
                break;
            default: panic("appPut didn't belong to any grp?");
        }
    }
    */
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
    //assert(lineId > -1);
    if (lineId == -1) {
        return;
    }
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

    Entry* e;
    if (!nonInclusiveHack){
        e = &array[lineId];
    }
    else {
        if (directory.count(lineAddr) == 0 || directory[lineAddr].numSharers == 0) {     // no private copies above me 
            return cycle;
        }
        else {      // private copies exist above me
            e = &directory[lineAddr];
        }
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
    directory[lineAddr].sharers[srcId] = false;
    directory[lineAddr].numSharers--;

    return;
}

uint64_t MESITopCC::processEviction(Address wbLineAddr, int32_t lineId, bool* reqWriteback, uint64_t cycle, uint32_t srcId) {  
    if (nonInclusiveHack) {
        // Don't invalidate anything, just clear our entry
        assert(lineId > -1);
        if (directory.count(wbLineAddr) > 0 && directory[wbLineAddr].numSharers == 0) {
            directory.erase(wbLineAddr);
            //directory[wbLineAddr]->clear();
        }
        //assert(directory.count(wbLineAddr) > 0);
        //directory[wbLineAddr]->exists = false;      // line was evicted from the LLC, but it might still be in priv caches
        //array[lineId].clear();
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
    //e = &array[lineId];
    bool alloc = false;
    
    if (!nonInclusiveHack)
        e = &array[lineId];
    else {
        if (directory.empty() || directory.count(lineAddr) == 0){
            //directory[lineAddr] = (Entry*)gm_malloc(sizeof(Entry));
            directory[lineAddr].clear();
            alloc = true;
            assert(directory.size() < directory.max_size());
        }
        e = &directory[lineAddr];
    }
    
    uint64_t respCycle = cycle;
    switch (type) {
        // assumption for PUT operations is that we always have info about who sends the PUT (aka they will be in the sharers)
        case PUTX:
            assert(e->isExclusive() || nonInclusiveHack);
            if (flags & MemReq::PUTX_KEEPEXCL) {
                assert(e->sharers[childId]);
                assert(*childState == M);
                *childState = E; //they don't hold dirty data anymore
                break; //don't remove from sharer set. It'll keep exclusive perms.
            }
        case PUTS: 
            if(!alloc) {
                assert(e->sharers[childId]);
                e->numSharers--;         
            }
            e->sharers[childId] = false;
            *childState = I;
            assert(e->numSharers >= 0);
            break;
        case GETS:
        assert(!(flags & MemReq::PKTIN));
            // should a GETS from the NIC modify any cache state? I think not (unless it finds an invalid line, which we deal with in bcc)
            // apparently it should, if it misses and the line is in a private cache
            if(!(flags & MemReq::PKTOUT)) {
                if (e->isEmpty() && haveExclusive && !(flags & MemReq::NOEXCL)) {
                     //Give in E state
                    e->exclusive = true;
                    e->sharers[childId] = true;
                    e->numSharers = 1;
                    *childState = E;
                } else {
                    //Give in S state
                    assert(e->sharers[childId] == false);

                    if (e->isExclusive()) {
                        //Downgrade the exclusive sharer
                        respCycle = sendInvalidates(lineAddr, lineId, INVX, inducedWriteback, cycle, srcId);
                    }

                    assert_msg(!e->isExclusive(), "Can't have exclusivity here. isExcl=%d excl=%d numSharers=%d", e->isExclusive(), e->exclusive, e->numSharers);

                    e->sharers[childId] = true;
                    e->numSharers++;
                    e->exclusive = false; //dsm: Must set, we're explicitly non-exclusive
                    *childState = S;
                }
            }
            else {
                if (lineId > -1) {
                    // case 1: miss in the llc, hit in priv caches
                    // we have allocated a line in the llc, currently it is in E, retrieve info about priv owner
                
                    // case 2: hit in the llc
                    // we also do this for case 1
                    if (e->isExclusive()) {
                        //Downgrade the exclusive sharer
                        // for case 1, in bcc we have assumed that there exists 1 private copy in E/M state, if it is M inducedWriteback will turn llc to M
                        respCycle = sendInvalidates(lineAddr, lineId, INVX, inducedWriteback, cycle, srcId);
                    }
                }
                // case 3: miss everywhere
                else {
                    if(this->existsInPrivate(lineAddr)){
                        info("lbuf exists in private cache, numsharers = %d",directory[lineAddr].numSharers);
                        info("lbuf directory hit count = %d", directory.count(lineAddr));
                        info("e->numsharers: %d", e->numSharers);
                        if(e->isExclusive()){
                            info("e is exclusive");
                        }
                        uint32_t numChildren = children.size();
                        info("numchildren: %d",numChildren);
                        for (uint32_t c = 0; c < numChildren; c++) {
                            //if (e->sharers[c]) {
                            if(directory.find(lineAddr)->second.sharers[c]){
                                info("line owned by: %s",children[c]->getName());
                            }
                        }
                        //info(directory.find(lineAddr)->getName());
                        info("first: %d",directory.find(lineAddr)->first);
                        
                        //info("second: ",)

                        for(int ii=0; ii<27;ii++){
                            if(directory[lineAddr].sharers[ii]){
                                info("line is in private cache of %d", ii);
                            }
                        }
                    }
                    if(!(nicInfo->zeroCopy)){
                        assert(!this->existsInPrivate(lineAddr)); // nothing to do, chack we are invalid and none has the line above us
                    }
                }
                    
            }

            break;
        case GETX:
            assert((flags & MemReq::PKTOUT) || haveExclusive); //the current cache better have exclusive access to this line

            // if we write directly to the l2/llc, we want all children to be invalidated
            if (!((flags & MemReq::PKTIN && childId > MAX_CACHE_CHILDREN) || (flags & MemReq::PKTOUT))) {
                // If child is in sharers list (this is an upgrade miss), take it out
                if (e->sharers[childId]) {
                    assert_msg(!e->isExclusive(), "Spurious GETX, childId=%d numSharers=%d isExcl=%d excl=%d", childId, e->numSharers, e->isExclusive(), e->exclusive);
                    e->sharers[childId] = false;
                     assert(e->numSharers>0);
                    e->numSharers--;
                }
            }

            // Invalidate all other copies
            respCycle = sendInvalidates(lineAddr, lineId, INV, inducedWriteback, cycle, srcId);
            
            if (flags & MemReq::PKTOUT)
                e->exclusive = false;       // this is an egress access that invalidates the LLC too
            else
                e->exclusive = true;

            if (!(((flags & MemReq::PKTIN) && childId > MAX_CACHE_CHILDREN)|| (flags & MemReq::PKTOUT)))  {
                // Set current sharer, mark exclusive
                e->sharers[childId] = true;
                e->numSharers++;

                assert(e->numSharers == 1);

                *childState = M; //give in M directly
            }

            break;

        case CLEAN:
            {
                if (e->sharers[childId]) {
                    e->sharers[childId] = false;
                    assert(e->numSharers>0);
                    e->numSharers--;
                }

                // Invalidate all copies
                if (e->numSharers > 0)
                    respCycle = sendInvalidates(lineAddr, lineId, INV, inducedWriteback, cycle, srcId);

                //*childState = I;
                e->clear();
            }
            break;
        case CLEAN_S:
            {
                /*
                if(!nonInclusiveHack) {
                    if (e->sharers[childId]) {
                    e->sharers[childId] = false;
                    assert(e->numSharers>0);
                    e->numSharers--;
                }

                // Invalidate all copies
                if (e->numSharers > 0)
                    respCycle = sendInvalidates(lineAddr, lineId, INV, inducedWriteback, cycle, srcId);

                //*childState = I;
                e->clear();
                }

                else {
                    if (e->sharers[childId]) {
                    e->sharers[childId] = false;
                    assert(e->numSharers>0);
                    e->numSharers--; 
                    e->exclusive = false;
                    if (e->numSharers == 0)
                        e->clear();
                    }
                }*/

                bool include = false;
                if (e->sharers[childId]) {
                    e->sharers[childId] = false;
                    assert(e->numSharers>0);
                    e->numSharers--; 
                    include = true;   
                }

                // Invalidate all copies

                if (e->isExclusive()) {
                    //Downgrade the exclusive sharer
                    respCycle = sendInvalidates(lineAddr, lineId, INVX, inducedWriteback, cycle, srcId);
                }

                assert_msg(!e->isExclusive(), "Can't have exclusivity here. isExcl=%d excl=%d numSharers=%d", e->isExclusive(), e->exclusive, e->numSharers);

                if(include) {
                    assert(!e->sharers[childId]);
                    e->sharers[childId] = true;
                    e->numSharers++;
                    e->exclusive = false; //dsm: Must set, we're explicitly non-exclusive
                    *childState = S;
                }
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


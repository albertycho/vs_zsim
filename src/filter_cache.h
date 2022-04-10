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

#ifndef FILTER_CACHE_H_
#define FILTER_CACHE_H_

#include "bithacks.h"
#include "cache.h"
#include "galloc.h"
#include "zsim.h"

/* Extends Cache with an L0 direct-mapped cache, optimized to hell for hits
 *
 * L1 lookups are dominated by several kinds of overhead (grab the cache locks,
 * several virtual functions for the replacement policy, etc.). This
 * specialization of Cache solves these issues by having a filter array that
 * holds the most recently used line in each set. Accesses check the filter array,
 * and then go through the normal access path. Because there is one line per set,
 * it is fine to do this without grabbing a lock.
 */


////helper function to find if matrix addr
//bool is_mat_addr(Address LineAddr) {
//    glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
//    uint32_t matN = nicInfo->mat_N;
//    uint32_t matLen = matN * matN;
//    if (matLen == 0) {
//        return false;
//    }
//    Address matAbot = (Address) nicInfo->matA;
//    Address matAtop = (Address) &(nicInfo->matA[matLen-1]);
//
//    Address matBbot = (Address) nicInfo->matB;
//    Address matBtop = (Address) &(nicInfo->matB[matLen - 1]);
//
//    Address matCbot = (Address) nicInfo->matC;
//    Address matCtop = (Address) &(nicInfo->matC[matLen - 1]);
//
//    Address shiftedAddr = LineAddr << lineBits;
//    if (shiftedAddr >= matAbot && shiftedAddr <= matAtop) {
//        return true;
//    }
//    if (shiftedAddr >= matBbot && shiftedAddr <= matBtop) {
//        return true;
//    }
//    if (shiftedAddr >= matCbot && shiftedAddr <= matCtop) {
//        return true;
//    }
//    return false;

}


class FilterCache : public Cache {
    private:
        struct FilterEntry {
            volatile Address rdAddr;
            volatile Address wrAddr;
            volatile uint64_t availCycle;

            void clear() {wrAddr = 0; rdAddr = 0; availCycle = 0;}
        };

        //Replicates the most accessed line of each set in the cache
        FilterEntry* filterArray;
        Address setMask;
        uint32_t numSets;
        uint32_t srcId; //should match the core
        uint32_t reqFlags;

        lock_t filterLock;
        uint64_t fGETSHit, fGETXHit;

        uint32_t extra_latency;

    public:
        FilterCache(uint32_t _numSets, uint32_t _numLines, CC* _cc, CacheArray* _array,
                ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, g_string& _name, int _level, uint32_t _extra_latency)
            : Cache(_numLines, _cc, _array, _rp, _accLat, _invLat, _name, _level)
        {
            numSets = _numSets;
            setMask = numSets - 1;
            filterArray = gm_memalign<FilterEntry>(CACHE_LINE_BYTES, numSets);
            for (uint32_t i = 0; i < numSets; i++) filterArray[i].clear();
            futex_init(&filterLock);
            fGETSHit = fGETXHit = 0;
            srcId = -1;
            reqFlags = 0;
            extra_latency = _extra_latency;
        }

        void setSourceId(uint32_t id) {
            srcId = id;
        }

        void setFlags(uint32_t flags) {
            reqFlags = flags;
        }

        void initStats(AggregateStat* parentStat) {
            AggregateStat* cacheStat = new AggregateStat();
            cacheStat->init(name.c_str(), "Filter cache stats");

            ProxyStat* fgetsStat = new ProxyStat();
            fgetsStat->init("fhGETS", "Filtered GETS hits", &fGETSHit);
            ProxyStat* fgetxStat = new ProxyStat();
            fgetxStat->init("fhGETX", "Filtered GETX hits", &fGETXHit);
            cacheStat->append(fgetsStat);
            cacheStat->append(fgetxStat);

            initCacheStats(cacheStat);
            parentStat->append(cacheStat);
        }

        //helper function to find if matrix addr
        bool is_mat_addr(Address LineAddr) {
            glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
            uint32_t matN = nicInfo->mat_N;
            uint32_t matLen = matN * matN;
            if (matLen == 0) {
                return false;
            }
            Address matAbot = (Address)nicInfo->matA;
            Address matAtop = (Address) & (nicInfo->matA[matLen - 1]);

            Address matBbot = (Address)nicInfo->matB;
            Address matBtop = (Address) & (nicInfo->matB[matLen - 1]);

            Address matCbot = (Address)nicInfo->matC;
            Address matCtop = (Address) & (nicInfo->matC[matLen - 1]);

            Address shiftedAddr = LineAddr << lineBits;
            if (shiftedAddr >= matAbot && shiftedAddr <= matAtop) {
                return true;
            }
            if (shiftedAddr >= matBbot && shiftedAddr <= matBtop) {
                return true;
            }
            if (shiftedAddr >= matCbot && shiftedAddr <= matCtop) {
                return true;
            }
            return false;

        }


        // source: the id of the core issuing the request, used to signify which recorder is used

        inline uint64_t load(Address vAddr, uint64_t curCycle, uint16_t lvl = 8, uint32_t source = 1742, uint32_t flags = 0) {
            Address vLineAddr = vAddr >> lineBits;
            uint32_t idx = vLineAddr & setMask;
            uint64_t availCycle = filterArray[idx].availCycle; //read before, careful with ordering to avoid timing races
            if ((lvl == 8) || (lvl == level)) {
				//if ideal case
				//if vLineAddr in recv_buf range for this core
                glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());	
                //assume cq and wq return immediately
                uint64_t wq_base = (uint64_t) (nicInfo->nic_elem[srcId].wq);
                uint64_t wq_top = wq_base + sizeof(rmc_wq_t);
                uint64_t cq_base = (uint64_t) (nicInfo->nic_elem[srcId].cq);
                uint64_t cq_top = cq_base + sizeof(rmc_cq_t);
                if((vAddr >= wq_base) && (vAddr<=wq_top)){
                    return curCycle;
                }
                if((vAddr >= cq_base) && (vAddr<=cq_top)){
                    return curCycle;
                }
                if (vLineAddr == filterArray[idx].rdAddr) {
                    fGETSHit++;
                    return MAX(curCycle, availCycle);
                } 
            }

            if (lvl == 42) {       // ideal ingress coming from app core, if the data is nic-related return 0 latency
                Address gm_base_addr = 0x00ABBA000000; // defined in galloc.cpp
				//Address gm_seg_size = 1<<30; //TODO: just use default? or wire it from init
            	glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());	
            	Address gm_seg_size = nicInfo->gm_size;
                Address nicLineAddr_bot = gm_base_addr >> lineBits;
                Address nicLineAddr_top = (gm_base_addr + gm_seg_size) >> lineBits;
                if (vLineAddr >= nicLineAddr_bot && vLineAddr <= nicLineAddr_top) {
                    return curCycle + extra_latency;
                }
                else {
                    lvl = level;
                }
            }

            if (source == 1742)
                return replace(vLineAddr, idx, true, curCycle, srcId, 0, flags, (lvl == 8) ? level : lvl);
            else {
                return replace(vLineAddr, idx, true, curCycle, source, MAX_CACHE_CHILDREN+1, flags, (lvl == 8) ? level : lvl); 
            }
        }

        inline uint64_t store(Address vAddr, uint64_t curCycle, uint16_t lvl = 8, uint32_t source = 1742, uint32_t flags = 0) {
            Address vLineAddr = vAddr >> lineBits;
            uint32_t idx = vLineAddr & setMask;
            uint64_t availCycle = filterArray[idx].availCycle; //read before, careful with ordering to avoid timing races
            if ((lvl == 8) || (lvl == level)) {
                glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());	
                //assume cq and wq return immediately
                uint64_t wq_base = (uint64_t) (nicInfo->nic_elem[srcId].wq);
                uint64_t wq_top = wq_base + sizeof(rmc_wq_t);
                uint64_t cq_base = (uint64_t) (nicInfo->nic_elem[srcId].cq);
                uint64_t cq_top = cq_base + sizeof(rmc_cq_t);
                if((vAddr >= wq_base) && (vAddr<=wq_top)){
                    return curCycle;
                }
                if((vAddr >= cq_base) && (vAddr<=cq_top)){
                    return curCycle;
                }
                if (vLineAddr == filterArray[idx].wrAddr) {
                    fGETXHit++;
                    //NOTE: Stores don't modify availCycle; we'll catch matches in the core
                    //filterArray[idx].availCycle = curCycle; //do optimistic store-load forwarding
                    return MAX(curCycle, availCycle);
                } 
            }
            if (lvl == 42) {       // ideal ingress coming from app core, if the data is nic-related return 0 latency
                Address gm_base_addr = 0x00ABBA000000; // defined in galloc.cpp
                //Address gm_seg_size = 1<<30; //TODO: just use default? or wire it from init
            	glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());	
            	Address gm_seg_size = nicInfo->gm_size;
                Address nicLineAddr_bot = gm_base_addr >> lineBits;
                Address nicLineAddr_top = (gm_base_addr + gm_seg_size) >> lineBits;
                if (vLineAddr >= nicLineAddr_bot && vLineAddr <= nicLineAddr_top) {
                    return curCycle + extra_latency;
                }
                else {
                    lvl = level;
                }
            }
            if (source == 1742)
                return replace(vLineAddr, idx, false, curCycle, srcId, 0, flags, (lvl == 8) ? level : lvl);
            else {
                return replace(vLineAddr, idx, false, curCycle, source, MAX_CACHE_CHILDREN+1, flags, (lvl == 8) ? level : lvl);
            }
        }

        inline uint64_t clean(Address vAddr, uint64_t curCycle, uint32_t clean_type, uint32_t flags = 0) {
            Address vLineAddr = vAddr >> lineBits;
            uint32_t idx = vLineAddr & setMask;

            if (vLineAddr == filterArray[idx].rdAddr) {
                filterArray[idx].rdAddr = -1L;
            } 
            if (vLineAddr == filterArray[idx].wrAddr) {
                filterArray[idx].wrAddr = -1L;
            } 

            Address procMask_f = 0;
            Address pLineAddr = procMask_f | vLineAddr;
            MESIState dummyState = MESIState::I;
            futex_lock(&filterLock);
            
            MemReq req;
            if (clean_type == 1) {      // turn to I
                req = {pLineAddr, CLEAN, 0, &dummyState, curCycle, &filterLock, dummyState, srcId, flags | (level<<16)};
            }
            else if (clean_type == 2) { // turn to S
                req = {pLineAddr, CLEAN_S, 0, &dummyState, curCycle, &filterLock, dummyState, srcId, flags | (level<<16)};
            }
            
            uint64_t respCycle  = access(req);
            futex_unlock(&filterLock);
            return respCycle;
        }

        uint64_t replace(Address vLineAddr, uint32_t idx, bool isLoad, uint64_t curCycle, uint32_t source, uint32_t childId, uint32_t flags, int lvl) {
            Address procMask_f = procMask;
            //Don't apply mask if it's a NIC related address
            Address gm_base_addr = 0x00ABBA000000; // defined in galloc.cpp
            //Address gm_seg_size = 1<<30; //TODO: just use default? or wire it from init
            glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());	
            Address gm_seg_size = nicInfo->gm_size;
			//info("filtercache: gm_seg_size: %d",gm_seg_size);
            Address nicLineAddr_bot = gm_base_addr >> lineBits;
            Address nicLineAddr_top = (gm_base_addr + gm_seg_size) >> lineBits;
            
            //if (vLineAddr >= nicLineAddr_bot && vLineAddr <= nicLineAddr_top) {
            //if ((vLineAddr >= nicLineAddr_bot && vLineAddr <= nicLineAddr_top) && (source!=0)) { //just counting core access
            //    //std::cout << "app accessing nic element " << std::hex << vLineAddr << std::endl;
            //    procMask_f = 0;
                //flags = flags | MemReq::NETRELATED;
            //}
            
            int i=3;
            //glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
            while (i < nicInfo->expected_core_count + 3){
                Address base_ing = (Address)(nicInfo->nic_elem[i].recv_buf) >> lineBits;
                uint64_t size_ing = nicInfo->recv_buf_pool_size; 
                Address top_ing = ((Address)(nicInfo->nic_elem[i].recv_buf) + size_ing) >> lineBits;
                Address base_egr = (Address)(nicInfo->nic_elem[i].lbuf) >> lineBits;
                uint64_t size_egr = 256*nicInfo->forced_packet_size;
                Address top_egr = ((Address)(nicInfo->nic_elem[i].lbuf) + size_egr) >> lineBits;
                if (vLineAddr >= base_ing && vLineAddr <= top_ing) {
                    assert(vLineAddr >= nicLineAddr_bot && vLineAddr <= nicLineAddr_top);
                    flags = flags | MemReq::NETRELATED_ING;
                    procMask_f = 0;
                    break;
                }
                else if (vLineAddr >= base_egr && vLineAddr <= top_egr) {
                    assert(vLineAddr >= nicLineAddr_bot && vLineAddr <= nicLineAddr_top);
                    flags = flags | MemReq::NETRELATED_EGR;
                    procMask_f = 0;
                    break;
                }
                i++;
            }           
            
            
            Address pLineAddr = procMask_f | vLineAddr;
            MESIState dummyState = MESIState::I;
            futex_lock(&filterLock);
            MemReq req = {pLineAddr, isLoad? GETS : GETX, childId, &dummyState, curCycle, &filterLock, dummyState, source, reqFlags | (lvl << 16) | flags};
            
            uint64_t respCycle  = access(req);

            //Due to the way we do the locking, at this point the old address might be invalidated, but we have the new address guaranteed until we release the lock

            if (lvl == level) {
                //Careful with this order
                Address oldAddr = filterArray[idx].rdAddr;
                filterArray[idx].wrAddr = isLoad? -1L : vLineAddr;
                filterArray[idx].rdAddr = vLineAddr;

                //For LSU simulation purposes, loads bypass stores even to the same line if there is no conflict,
                //(e.g., st to x, ld from x+8) and we implement store-load forwarding at the core.
                //So if this is a load, it always sets availCycle; if it is a store hit, it doesn't
                if (oldAddr != vLineAddr) filterArray[idx].availCycle = respCycle;
            }
            futex_unlock(&filterLock);
            return respCycle;
        }

        // NO_RECORD versions of load/store/replace
        uint64_t replace_norecord(Address vLineAddr, uint32_t idx, bool isLoad, uint64_t curCycle) {
            Address pLineAddr = procMask | vLineAddr;
            MESIState dummyState = MESIState::I;
            futex_lock(&filterLock);
            uint32_t nr_flags = reqFlags | 1; //NORECORD
            MemReq req = { pLineAddr, isLoad ? GETS : GETX, 0, &dummyState, curCycle, &filterLock, dummyState, srcId, nr_flags};
            uint64_t respCycle = access(req);

            //Due to the way we do the locking, at this point the old address might be invalidated, but we have the new address guaranteed until we release the lock

            //Careful with this order
            Address oldAddr = filterArray[idx].rdAddr;
            filterArray[idx].wrAddr = isLoad ? -1L : vLineAddr;
            filterArray[idx].rdAddr = vLineAddr;

            //For LSU simulation purposes, loads bypass stores even to the same line if there is no conflict,
            //(e.g., st to x, ld from x+8) and we implement store-load forwarding at the core.
            //So if this is a load, it always sets availCycle; if it is a store hit, it doesn't
            if (oldAddr != vLineAddr) filterArray[idx].availCycle = respCycle;

            futex_unlock(&filterLock);
            return respCycle;
        }

        inline uint64_t load_norecord(Address vAddr, uint64_t curCycle) {
            Address vLineAddr = vAddr >> lineBits;
            uint32_t idx = vLineAddr & setMask;
            uint64_t availCycle = filterArray[idx].availCycle; //read before, careful with ordering to avoid timing races
            if (vLineAddr == filterArray[idx].rdAddr) {
                fGETSHit++;
                return MAX(curCycle, availCycle);
            }
            else {
                return replace_norecord(vLineAddr, idx, true, curCycle);
            }
        }

        inline uint64_t store_norecord(Address vAddr, uint64_t curCycle) {
            Address vLineAddr = vAddr >> lineBits;
            uint32_t idx = vLineAddr & setMask;
            uint64_t availCycle = filterArray[idx].availCycle; //read before, careful with ordering to avoid timing races
            if (vLineAddr == filterArray[idx].wrAddr) {
                fGETXHit++;
                //NOTE: Stores don't modify availCycle; we'll catch matches in the core
                //filterArray[idx].availCycle = curCycle; //do optimistic store-load forwarding
                return MAX(curCycle, availCycle);
            }
            else {
                return replace_norecord(vLineAddr, idx, false, curCycle);
            }
        }

        ///////////////////////////

        uint64_t invalidate(const InvReq& req) {
            Cache::startInvalidate();  // grabs cache's downLock
            futex_lock(&filterLock);
            uint32_t idx = req.lineAddr & setMask; //works because of how virtual<->physical is done...
            if ((filterArray[idx].rdAddr | procMask) == req.lineAddr) { //FIXME: If another process calls invalidate(), procMask will not match even though we may be doing a capacity-induced invalidation!
                filterArray[idx].wrAddr = -1L;
                filterArray[idx].rdAddr = -1L;
            }
            uint64_t respCycle = Cache::finishInvalidate(req); // releases cache's downLock
            futex_unlock(&filterLock);
            return respCycle;
        }

        void contextSwitch() {
            futex_lock(&filterLock);
            for (uint32_t i = 0; i < numSets; i++) filterArray[i].clear();
            futex_unlock(&filterLock);
        }
};

#endif  // FILTER_CACHE_H_

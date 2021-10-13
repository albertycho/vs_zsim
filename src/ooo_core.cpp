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

#include "ooo_core.h"
#include <algorithm>
#include <queue>
#include <string>
#include "bithacks.h"
#include "decoder.h"
#include "filter_cache.h"
#include "zsim.h"

#include "core_nic_api.h"

/* Uncomment to induce backpressure to the IW when the load/store buffers fill up. In theory, more detailed,
 * but sometimes much slower (as it relies on range poisoning in the IW, potentially O(n^2)), and in practice
 * makes a negligible difference (ROB backpressures).
 */
//#define LSU_IW_BACKPRESSURE

#define DEBUG_MSG(args...)
//#define DEBUG_MSG(args...) info(args)

// Core parameters
// TODO(dsm): Make OOOCore templated, subsuming these

// Stages --- more or less matched to Westmere, but have not seen detailed pipe diagrams anywhare
#define FETCH_STAGE 1
#define DECODE_STAGE 4  // NOTE: Decoder adds predecode delays to decode
#define ISSUE_STAGE 7
#define DISPATCH_STAGE 13  // RAT + ROB + RS, each is easily 2 cycles

#define L1D_LAT 4  // fixed, and FilterCache does not include L1 delay
#define FETCH_BYTES_PER_CYCLE 16
#define ISSUES_PER_CYCLE 4
#define RF_READS_PER_CYCLE 3

//OOOCore::OOOCore(FilterCache* _l1i, FilterCache* _l1d, int _core_id, uint32_t _domain, g_string& _name) : Core(_name), l1i(_l1i), l1d(_l1d), cRec(_domain, _name) {
OOOCore::OOOCore(FilterCache* _l1i, FilterCache* _l1d, uint32_t _domain, g_string& _name, uint32_t _coreIdx, 
                FilterCache* _l1i_caches[], FilterCache* _l1d_caches[],/* Cache* _l2_caches[], TimingCache* _llc_cache[], MemObject* _memory, */
                uint32_t _no_cores/*, uint32_t _no_llc_banks, uint32_t _no_priv_levels*/) 
: Core(_name), l1i(_l1i), l1d(_l1d), core_id(_coreIdx), cRec(_domain, _name) {
 
    core_id = _coreIdx;
    
    decodeCycle = DECODE_STAGE;  // allow subtracting from it
    curCycle = 0;
    phaseEndCycle = zinfo->phaseLength;

    for (uint32_t i = 0; i < MAX_REGISTERS; i++) {
        regScoreboard[i] = 0;
    }
    prevBbl = nullptr;

    lastStoreCommitCycle = 0;
    lastStoreAddrCommitCycle = 0;
    curCycleRFReads = 0;
    curCycleIssuedUops = 0;
    branchPc = 0;

    instrs = uops = bbls = approxInstrs = mispredBranches = 0;

    for (uint32_t i = 0; i < FWD_ENTRIES; i++) fwdArray[i].set((Address)(-1L), 0);

    /* Accessing different caches / memory banks
        private cache of core i: l1i_caches[i], l1d_caches[i], l2_caches[i] (if private l2 present)
        bank i of shared last level cache: llc_cache[i]
        memory: mem
        * Note * in a 2-level hierarchy, access the l2 through the llc_cache structure
    */

    for (uint32_t i=0; i<_no_cores; i++) {        
        l1d_caches[i] = _l1d_caches[i];
    }
/*
    for (uint32_t i=0; i<_no_cores; i++) {
        l1i_caches[i] = _l1i_caches[i];
    }

    if(_no_priv_levels > 1) {
        for (uint32_t i=0; i<_no_cores; i++) {
            l2_caches[i] = _l2_caches[i];
        }
    }    

    for (uint32_t i=0; i<_no_llc_banks; i++ ) {
        llc_cache[i] = _llc_cache[i];
    }
 
    memory = dynamic_cast<SimpleMemory*>(_memory);
*/
}

void OOOCore::initStats(AggregateStat* parentStat) {
    AggregateStat* coreStat = new AggregateStat();
    coreStat->init(name.c_str(), "Core stats");

    auto x = [this]() { return cRec.getUnhaltedCycles(curCycle); };
    LambdaStat<decltype(x)>* cyclesStat = new LambdaStat<decltype(x)>(x);
    cyclesStat->init("cycles", "Simulated unhalted cycles");

    auto y = [this]() { return cRec.getContentionCycles(); };
    LambdaStat<decltype(y)>* cCyclesStat = new LambdaStat<decltype(y)>(y);
    cCyclesStat->init("cCycles", "Cycles due to contention stalls");

    ProxyStat* instrsStat = new ProxyStat();
    instrsStat->init("instrs", "Simulated instructions", &instrs);
    ProxyStat* uopsStat = new ProxyStat();
    uopsStat->init("uops", "Retired micro-ops", &uops);
    ProxyStat* bblsStat = new ProxyStat();
    bblsStat->init("bbls", "Basic blocks", &bbls);
    ProxyStat* approxInstrsStat = new ProxyStat();
    approxInstrsStat->init("approxInstrs", "Instrs with approx uop decoding", &approxInstrs);
    ProxyStat* mispredBranchesStat = new ProxyStat();
    mispredBranchesStat->init("mispredBranches", "Mispredicted branches", &mispredBranches);

    coreStat->append(cyclesStat);
    coreStat->append(cCyclesStat);
    coreStat->append(instrsStat);
    coreStat->append(uopsStat);
    coreStat->append(bblsStat);
    coreStat->append(approxInstrsStat);
    coreStat->append(mispredBranchesStat);

#ifdef OOO_STALL_STATS
    profFetchStalls.init("fetchStalls",  "Fetch stalls");  coreStat->append(&profFetchStalls);
    profDecodeStalls.init("decodeStalls", "Decode stalls"); coreStat->append(&profDecodeStalls);
    profIssueStalls.init("issueStalls",  "Issue stalls");  coreStat->append(&profIssueStalls);
#endif

    parentStat->append(coreStat);
}

uint64_t OOOCore::getInstrs() const {return instrs;}
uint64_t OOOCore::getPhaseCycles() const {return curCycle % zinfo->phaseLength;}

void OOOCore::contextSwitch(int32_t gid) {
    if (gid == -1) {
        // Do not execute previous BBL, as we were context-switched
        prevBbl = nullptr;

        // Invalidate virtually-addressed filter caches
        l1i->contextSwitch();
        l1d->contextSwitch();
    }
}


//InstrFuncPtrs OOOCore::GetFuncPtrs() {return {LoadFunc, StoreFunc, BblFunc, BranchFunc, PredLoadFunc, PredStoreFunc, FPTR_ANALYSIS, {0}};}
InstrFuncPtrs OOOCore::GetFuncPtrs() { return { LoadFunc, StoreFunc, BblFunc, BranchFunc, PredLoadFunc, PredStoreFunc, NicMagicFunc, FPTR_ANALYSIS}; }

inline void OOOCore::load(Address addr) {
    loadAddrs[loads++] = addr;
}

void OOOCore::store(Address addr) {
    storeAddrs[stores++] = addr;
}

// Predicated loads and stores call this function, gets recorded as a 0-cycle op.
// Predication is rare enough that we don't need to model it perfectly to be accurate (i.e. the uops still execute, retire, etc), but this is needed for correctness.
void OOOCore::predFalseMemOp() {
    // I'm going to go out on a limb and assume just loads are predicated (this will not fail silently if it's a store)
    loadAddrs[loads++] = -1L;
}

void OOOCore::branch(Address pc, bool taken, Address takenNpc, Address notTakenNpc) {
    branchPc = pc;
    branchTaken = taken;
    branchTakenNpc = takenNpc;
    branchNotTakenNpc = notTakenNpc;
}

inline void OOOCore::bbl(Address bblAddr, BblInfo* bblInfo) {
    if (!prevBbl) {
        // This is the 1st BBL since scheduled, nothing to simulate
        prevBbl = bblInfo;
        // Kill lingering ops from previous BBL
        loads = stores = 0;
        return;
    }

    /* Simulate execution of previous BBL */

    uint32_t bblInstrs = prevBbl->instrs;
    DynBbl* bbl = &(prevBbl->oooBbl[0]);
    prevBbl = bblInfo;

    uint32_t loadIdx = 0;
    uint32_t storeIdx = 0;

    uint32_t prevDecCycle = 0;
    uint64_t lastCommitCycle = 0;  // used to find misprediction penalty

    // Run dispatch/IW
    for (uint32_t i = 0; i < bbl->uops; i++) {
        DynUop* uop = &(bbl->uop[i]);

        // Decode stalls
        uint32_t decDiff = uop->decCycle - prevDecCycle;
        decodeCycle = MAX(decodeCycle + decDiff, uopQueue.minAllocCycle());
        if (decodeCycle > curCycle) {
            //info("Decode stall %ld %ld | %d %d", decodeCycle, curCycle, uop->decCycle, prevDecCycle);
            uint32_t cdDiff = decodeCycle - curCycle;
#ifdef OOO_STALL_STATS
            profDecodeStalls.inc(cdDiff);
#endif
            curCycleIssuedUops = 0;
            curCycleRFReads = 0;
            for (uint32_t i = 0; i < cdDiff; i++) insWindow.advancePos(curCycle, core_id);
        }
        prevDecCycle = uop->decCycle;
        uopQueue.markLeave(curCycle);

        // Implement issue width limit --- we can only issue 4 uops/cycle
        if (curCycleIssuedUops >= ISSUES_PER_CYCLE) {
#ifdef OOO_STALL_STATS
            profIssueStalls.inc();
#endif
            // info("Advancing due to uop issue width");
            curCycleIssuedUops = 0;
            curCycleRFReads = 0;
            insWindow.advancePos(curCycle, core_id);
        }
        curCycleIssuedUops++;

        // Kill dependences on invalid register
        // Using curCycle saves us two unpredictable branches in the RF read stalls code
        regScoreboard[0] = curCycle;

        uint64_t c0 = regScoreboard[uop->rs[0]];
        uint64_t c1 = regScoreboard[uop->rs[1]];

        // RF read stalls
        // if srcs are not available at issue time, we have to go thru the RF
        curCycleRFReads += ((c0 < curCycle)? 1 : 0) + ((c1 < curCycle)? 1 : 0);
        if (curCycleRFReads > RF_READS_PER_CYCLE) {
            curCycleRFReads -= RF_READS_PER_CYCLE;
            curCycleIssuedUops = 0;  // or 1? that's probably a 2nd-order detail
            insWindow.advancePos(curCycle, core_id);
        }

        uint64_t c2 = rob.minAllocCycle();
        uint64_t c3 = curCycle;

        uint64_t cOps = MAX(c0, c1);

        // Model RAT + ROB + RS delay between issue and dispatch
        uint64_t dispatchCycle = MAX(cOps, MAX(c2, c3) + (DISPATCH_STAGE - ISSUE_STAGE));

        // info("IW 0x%lx %d %ld %ld %x", bblAddr, i, c2, dispatchCycle, uop->portMask);
        // NOTE: Schedule can adjust both cur and dispatch cycles
        insWindow.schedule(curCycle, dispatchCycle, uop->portMask, uop->extraSlots, core_id);

        // If we have advanced, we need to reset the curCycle counters
        if (curCycle > c3) {
            curCycleIssuedUops = 0;
            curCycleRFReads = 0;
        }

        uint64_t commitCycle;

        // LSU simulation
        // NOTE: Ever-so-slightly faster than if-else if-else if-else
        switch (uop->type) {
            case UOP_GENERAL:
                commitCycle = dispatchCycle + uop->lat;
                break;

            case UOP_LOAD:
                {
                    // dispatchCycle = MAX(loadQueue.minAllocCycle(), dispatchCycle);
                    uint64_t lqCycle = loadQueue.minAllocCycle();
                    if (lqCycle > dispatchCycle) {
#ifdef LSU_IW_BACKPRESSURE
                        insWindow.poisonRange(curCycle, lqCycle, 0x4 /*PORT_2, loads*/, core_id);
#endif
                        dispatchCycle = lqCycle;
                    }

                    // Wait for all previous store addresses to be resolved
                    dispatchCycle = MAX(lastStoreAddrCommitCycle+1, dispatchCycle);

                    Address addr = loadAddrs[loadIdx++];
                    uint64_t reqSatisfiedCycle = dispatchCycle;
                    if (addr != ((Address)-1L)) {
                        reqSatisfiedCycle = l1d->load(addr, dispatchCycle) + L1D_LAT;
                        cRec.record(curCycle, dispatchCycle, reqSatisfiedCycle);
                        

                    }
                    

                    // Enforce st-ld forwarding
                    uint32_t fwdIdx = (addr>>2) & (FWD_ENTRIES-1);
                    if (fwdArray[fwdIdx].addr == addr) {
                        // info("0x%lx FWD %ld %ld", addr, reqSatisfiedCycle, fwdArray[fwdIdx].storeCycle);
                        /* Take the MAX (see FilterCache's code) Our fwdArray
                         * imposes more stringent timing constraints than the
                         * l1d, b/c FilterCache does not change the line's
                         * availCycle on a store. This allows FilterCache to
                         * track per-line, not per-word availCycles.
                         */
                        reqSatisfiedCycle = MAX(reqSatisfiedCycle, fwdArray[fwdIdx].storeCycle);
                    }

                    commitCycle = reqSatisfiedCycle;
                    loadQueue.markRetire(commitCycle);
                }
                break;

            case UOP_STORE:
                {
                    // dispatchCycle = MAX(storeQueue.minAllocCycle(), dispatchCycle);
                    uint64_t sqCycle = storeQueue.minAllocCycle();
                    if (sqCycle > dispatchCycle) {
#ifdef LSU_IW_BACKPRESSURE
                        insWindow.poisonRange(curCycle, sqCycle, 0x10 /*PORT_4, stores*/, core_id);
#endif
                        dispatchCycle = sqCycle;
                    }

                    // Wait for all previous store addresses to be resolved (not just ours :))
                    dispatchCycle = MAX(lastStoreAddrCommitCycle+1, dispatchCycle);

                    Address addr = storeAddrs[storeIdx++];
                    uint64_t reqSatisfiedCycle = l1d->store(addr, dispatchCycle) + L1D_LAT;
                    cRec.record(curCycle, dispatchCycle, reqSatisfiedCycle);

                    // Fill the forwarding table
                    fwdArray[(addr>>2) & (FWD_ENTRIES-1)].set(addr, reqSatisfiedCycle);

                    commitCycle = reqSatisfiedCycle;
                    lastStoreCommitCycle = MAX(lastStoreCommitCycle, reqSatisfiedCycle);
                    storeQueue.markRetire(commitCycle);
                }
                break;

            case UOP_STORE_ADDR:
                commitCycle = dispatchCycle + uop->lat;
                lastStoreAddrCommitCycle = MAX(lastStoreAddrCommitCycle, commitCycle);
                break;

            //case UOP_FENCE:  //make gcc happy
            default:
                assert((UopType) uop->type == UOP_FENCE);
                commitCycle = dispatchCycle + uop->lat;
                // info("%d %ld %ld", uop->lat, lastStoreAddrCommitCycle, lastStoreCommitCycle);
                // force future load serialization
                lastStoreAddrCommitCycle = MAX(commitCycle, MAX(lastStoreAddrCommitCycle, lastStoreCommitCycle + uop->lat));
                // info("%d %ld %ld X", uop->lat, lastStoreAddrCommitCycle, lastStoreCommitCycle);
        }

        // Mark retire at ROB
        rob.markRetire(commitCycle);

        // Record dependences
        regScoreboard[uop->rd[0]] = commitCycle;
        regScoreboard[uop->rd[1]] = commitCycle;

        lastCommitCycle = commitCycle;

        //info("0x%lx %3d [%3d %3d] -> [%3d %3d]  %8ld %8ld %8ld %8ld", bbl->addr, i, uop->rs[0], uop->rs[1], uop->rd[0], uop->rd[1], decCycle, c3, dispatchCycle, commitCycle);
    }

    instrs += bblInstrs;
    uops += bbl->uops;
    bbls++;
    approxInstrs += bbl->approxInstrs;

#ifdef BBL_PROFILING
    if (approxInstrs) Decoder::profileBbl(bbl->bblIdx);
#endif

    // Check full match between expected and actual mem ops
    // If these assertions fail, most likely, something's off in the decoder
    assert_msg(loadIdx == loads, "%s: loadIdx(%d) != loads (%d)", name.c_str(), loadIdx, loads);
    assert_msg(storeIdx == stores, "%s: storeIdx(%d) != stores (%d)", name.c_str(), storeIdx, stores);
    loads = stores = 0;


    /* Simulate frontend for branch pred + fetch of this BBL
     *
     * NOTE: We assume that the instruction length predecoder and the IQ are
     * weak enough that they can't hide any ifetch or bpred stalls. In fact,
     * predecoder stalls are incorporated in the decode stall component (see
     * decoder.cpp). So here, we compute fetchCycle, then use it to adjust
     * decodeCycle.
     */

    // Model fetch-decode delay (fixed, weak predec/IQ assumption)
    uint64_t fetchCycle = decodeCycle - (DECODE_STAGE - FETCH_STAGE);
    uint32_t lineSize = 1 << lineBits;

    // Simulate branch prediction
    if (branchPc && !branchPred.predict(branchPc, branchTaken)) {
        mispredBranches++;

        /* Simulate wrong-path fetches
         *
         * This is not for a latency reason, but sometimes it increases fetched
         * code footprint and L1I MPKI significantly. Also, we assume a perfect
         * BTB here: we always have the right address to missfetch on, and we
         * never need resteering.
         *
         * NOTE: Resteering due to BTB misses is done at the BAC unit, is
         * relatively rare, and carries an 8-cycle penalty, which should be
         * partially hidden if the branch is predicted correctly --- so we
         * don't simulate it.
         *
         * Since we don't have a BTB, we just assume the next branch is not
         * taken. With a typical branch mispred penalty of 17 cycles, we
         * typically fetch 3-4 lines in advance (16B/cycle). This sets a higher
         * limit, which can happen with branches that take a long time to
         * resolve (because e.g., they depend on a load). To set this upper
         * bound, assume a completely backpressured IQ (18 instrs), uop queue
         * (28 uops), IW (36 uops), and 16B instr length predecoder buffer. At
         * ~3.5 bytes/instr, 1.2 uops/instr, this is about 5 64-byte lines.
         */

        // info("Mispredicted branch, %ld %ld %ld | %ld %ld", decodeCycle, curCycle, lastCommitCycle,
        //         lastCommitCycle-decodeCycle, lastCommitCycle-curCycle);
        Address wrongPathAddr = branchTaken? branchNotTakenNpc : branchTakenNpc;
        uint64_t reqCycle = fetchCycle;
        for (uint32_t i = 0; i < 5*64/lineSize; i++) {
            uint64_t fetchLat = l1i->load(wrongPathAddr + lineSize*i, curCycle) - curCycle;
            cRec.record(curCycle, curCycle, curCycle + fetchLat);
            uint64_t respCycle = reqCycle + fetchLat;
            if (respCycle > lastCommitCycle) {
                break;
            }
            // Model fetch throughput limit
            reqCycle = respCycle + lineSize/FETCH_BYTES_PER_CYCLE;
        }

        fetchCycle = lastCommitCycle;
    }
    branchPc = 0;  // clear for next BBL

    // Simulate current bbl ifetch
    Address endAddr = bblAddr + bblInfo->bytes;
    for (Address fetchAddr = bblAddr; fetchAddr < endAddr; fetchAddr += lineSize) {
        // The Nehalem frontend fetches instructions in 16-byte-wide accesses.
        // Do not model fetch throughput limit here, decoder-generated stalls already include it
        // We always call fetches with curCycle to avoid upsetting the weave
        // models (but we could move to a fetch-centric recorder to avoid this)
       
        uint64_t fetchLat = l1i->load(fetchAddr, curCycle) - curCycle;
        cRec.record(curCycle, curCycle, curCycle + fetchLat);
        fetchCycle += fetchLat;
    } 

    // If fetch rules, take into account delay between fetch and decode;
    // If decode rules, different BBLs make the decoders skip a cycle
    decodeCycle++;
    uint64_t minFetchDecCycle = fetchCycle + (DECODE_STAGE - FETCH_STAGE);
    if (minFetchDecCycle > decodeCycle) {
#ifdef OOO_STALL_STATS
        profFetchStalls.inc(decodeCycle - minFetchDecCycle);
#endif
        decodeCycle = minFetchDecCycle;
    }
}

// Timing simulation code
void OOOCore::join() {
    DEBUG_MSG("[%s] Joining, curCycle %ld phaseEnd %ld", name.c_str(), curCycle, phaseEndCycle);
    uint64_t targetCycle = cRec.notifyJoin(curCycle);
    if (targetCycle > curCycle) advance(targetCycle);
    phaseEndCycle = zinfo->globPhaseCycles + zinfo->phaseLength;
    // assert(targetCycle <= phaseEndCycle);
    DEBUG_MSG("[%s] Joined, curCycle %ld phaseEnd %ld", name.c_str(), curCycle, phaseEndCycle);
}

void OOOCore::leave() {
    DEBUG_MSG("[%s] Leaving, curCycle %ld phaseEnd %ld", name.c_str(), curCycle, phaseEndCycle);
    cRec.notifyLeave(curCycle);
}

void OOOCore::cSimStart() {
    uint64_t targetCycle = cRec.cSimStart(curCycle);
    assert(targetCycle >= curCycle);
    if (targetCycle > curCycle) advance(targetCycle);
}

void OOOCore::cSimEnd() {
    uint64_t targetCycle = cRec.cSimEnd(curCycle);
    assert(targetCycle >= curCycle);
    if (targetCycle > curCycle) advance(targetCycle);
}

void OOOCore::advance(uint64_t targetCycle) {
    assert(targetCycle > curCycle);
    decodeCycle += targetCycle - curCycle;
    insWindow.longAdvance(curCycle, targetCycle, core_id);
    curCycleRFReads = 0;
    curCycleIssuedUops = 0;
    assert(targetCycle == curCycle);
    /* NOTE: Validation with weave mems shows that not advancing internal cycle
     * counters in e.g., the ROB does not change much; consider full-blown
     * rebases though if weave models fail to validate for some app.
     */
}

// Pin interface code

void OOOCore::LoadFunc(THREADID tid, ADDRINT addr) {static_cast<OOOCore*>(cores[tid])->load(addr);}
void OOOCore::StoreFunc(THREADID tid, ADDRINT addr) {static_cast<OOOCore*>(cores[tid])->store(addr);}

void OOOCore::PredLoadFunc(THREADID tid, ADDRINT addr, BOOL pred) {
    OOOCore* core = static_cast<OOOCore*>(cores[tid]);
    if (pred) core->load(addr);
    else core->predFalseMemOp();
}

void OOOCore::PredStoreFunc(THREADID tid, ADDRINT addr, BOOL pred) {
    OOOCore* core = static_cast<OOOCore*>(cores[tid]);
    if (pred) core->store(addr);
    else core->predFalseMemOp();
}

int flag = 1;

void OOOCore::BblFunc(THREADID tid, ADDRINT bblAddr, BblInfo* bblInfo) {
    OOOCore* core = static_cast<OOOCore*>(cores[tid]);
    
	//dbgprint for checking proc to core mapping
	//uint64_t core_id = getCid(tid); // using processID to identify nicCore for now
	//if(nicInfo->nic_init_done){
	//	info("pid: %d, cid: %d", procIdx, core_id);
	//}

    core->bbl(bblAddr, bblInfo);
    
    glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());

    //TODO check which nic (ingress or egress) should handle this
    //as of now we stick with one NIC core doing both ingress and egress work
    if ((nicInfo->nic_ingress_pid == procIdx) && (nicInfo->nic_init_done)) {
        ///////////////CALL DEQ_DPQ//////////////////////
        uint32_t srcId = getCid(tid);
        deq_dpq(srcId, core, &(core->cRec), core->l1d/*MemObject* dest*/, core->curCycle);
    }
    if ((nicInfo->nic_egress_pid == procIdx) && (nicInfo->nic_init_done)) {
        //nic_egress_routine(tid);
    }
    
    // Simple synchronization mechanism for enforcing producer consumer order for NIC_Ingress and other cores
    if ((nicInfo->nic_ingress_pid != procIdx) && (nicInfo->nic_init_done)) {
        if (nicInfo->nic_egress_pid == procIdx) {
            //don't need to adjust egress core clock here
        }
        else {
            // Sometime this check gets stuck at the end of the phase, adding safety break
            int safety_counter = 0;
            while (core->curCycle > (((OOOCore*)(nicInfo->nicCore_ingress))->getCycles_forSynch())) {
            //while (core->curCycle > ((((OOOCore*)(nicInfo->nicCore_ingress))->getCycles()) + 50) ) { 
                // +50this could be a performance optmiziation, not sure how significant correctness hazard is
                //info("thisCore curCycle = %lu, nicCore curcycle = %lu", core->curCycle, ((OOOCore*)(nicInfo->nicCore_ingress))->getCycles_forSynch());
                usleep(10); // short delay seems to work sufficient
                safety_counter++;
                if (safety_counter > 2) { // >2 seems to work in current env. May need to be adjusted when running on different machine
                    break;
                }
            }
        }
    }


    while (core->curCycle > core->phaseEndCycle) {
        core->phaseEndCycle += zinfo->phaseLength;

        if (core->curCycle <= core->phaseEndCycle) {
            /* Do the nic remote packet injection routine once a phase */
            /* execute this code only for the NIC process && nic init is done */
            if ((nicInfo->nic_ingress_pid == procIdx) && (nicInfo->nic_init_done)) {

                /* check if cores finished their processes and exit if so */
                if (nicInfo->registered_core_count == 0) {
                    if (nicInfo->nic_ingress_proc_on) {
                        info("ooo_core.cpp - turn off nic proc");
                        nicInfo->nic_ingress_proc_on = false;
                        nicInfo->nic_egress_proc_on = false;
                    }
                }
                else{
                    //Inject packets for this phase
                    nic_ingress_routine(tid);
                    
                }
            }
            //else if ((nicInfo->nic_egress_pid == procIdx) && (nicInfo->nic_init_done)) {
            //    //call egress routine
            //    nic_egress_routine(tid);
            //}
        }
        
        uint32_t cid = getCid(tid);
        // NOTE: TakeBarrier may take ownership of the core, and so it will be used by some other thread. If TakeBarrier context-switches us,
        // the *only* safe option is to return inmmediately after we detect this, or we can race and corrupt core state. However, the information
        // here is insufficient to do that, so we could wind up double-counting phases.
        uint32_t newCid = TakeBarrier(tid, cid);
        // NOTE: Upon further observation, we cannot race if newCid == cid, so this code should be enough.
        // It may happen that we had an intervening context-switch and we are now back to the same core.
        // This is fine, since the loop looks at core values directly and there are no locals involved,
        // so we should just advance as needed and move on.
        if (newCid != cid) break;  /*context-switch, we do not own this context anymore*/
    }
}

void OOOCore::BranchFunc(THREADID tid, ADDRINT pc, BOOL taken, ADDRINT takenNpc, ADDRINT notTakenNpc) {
    static_cast<OOOCore*>(cores[tid])->branch(pc, taken, takenNpc, notTakenNpc);
}

void OOOCore::NicMagicFunc(THREADID tid, ADDRINT val, ADDRINT field) {

    uint64_t core_id = getCid(tid);
    glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());

    switch (field) {
    case 0://WQ

        if (NICELEM.wq_valid == true) {
            info("duplicate WQ register for core %lu", core_id);
        }

        *static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(nicInfo->nic_elem[core_id].wq);
        NICELEM.wq->head = 0;
        NICELEM.wq->SR = 1;
        NICELEM.nwq_SR = 1;
        for (int i = 0; i < MAX_NUM_WQ; i++) {
            NICELEM.wq->q[i].SR = 0;
        }
        NICELEM.wq_tail = 0;
        NICELEM.wq_valid = true;
        info("core %d registered WQ at addrs %lld", core_id, nicInfo->nic_elem[core_id].wq);
        break;
    case 1://CQ
        
        info("core %lu registered CQ", core_id);

        NICELEM.cq->tail = 0;
        NICELEM.cq->SR = 1;
        NICELEM.ncq_SR = 1;
        for (int i = 0; i < MAX_NUM_WQ; i++) {
            NICELEM.cq->q[i].SR = 0;
        }

        NICELEM.cq_valid = true;
        *static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(nicInfo->nic_elem[core_id].cq);
        nicInfo->registered_core_count = nicInfo->registered_core_count + 1;
        if (nicInfo->registered_core_count == nicInfo->expected_core_count) {
            if (nicInfo->nic_ingress_proc_on) {
                nicInfo->nic_init_done = true;
            }
        }
        info("core %d registered CQ at addrs %lld", core_id, nicInfo->nic_elem[core_id].cq);
        break;
    case 2: // lbuf
        *static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(&(nicInfo->nic_elem[core_id].lbuf[0]));
        info("core %d registered LBUF at addrs %lld", core_id, nicInfo->nic_elem[core_id].lbuf);
        break;

    case NOTIFY_WQ_WRITE://NOTIFY WQ WRITE from application
        info("notify_wq_write")
        nic_rgp_action(core_id, nicInfo);
        break;
    case 0xB: //indicate app is nic_proxy_process (INGRESS)
        *static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(&(nicInfo->nic_ingress_proc_on));
        nicInfo->nic_ingress_pid = procIdx;
        nicInfo->nic_ingress_proc_on = true;
        info("nic ingress pid:%d, cid:%lu", procIdx, core_id);
        info("packet injection rate:%lu", nicInfo->packet_injection_rate)
        if (nicInfo->registered_core_count == nicInfo->expected_core_count) {
            nicInfo->nic_init_done = true;
        }
        nicInfo->nicCore_ingress = (void*) cores[tid];

        
        break;

    case 0xC: //indicate app is nic_proxy_process (EGRESS)
        *static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(&(nicInfo->nic_egress_proc_on));
        nicInfo->nic_egress_pid = procIdx;
        nicInfo->nic_egress_proc_on = true;
        info("nic egress  pid:%d, cid:%lu", procIdx, core_id);
        if (nicInfo->registered_core_count == nicInfo->expected_core_count) {
            nicInfo->nic_init_done = true;
        }
        nicInfo->nicCore_egress = (void*)cores[tid];
        break;
    case 0xdead: //invalidate entries after test app terminates
        nicInfo->registered_core_count = nicInfo->registered_core_count - 1;

        //nicInfo->nic_elem[core_id].cq_valid = false;

        //setting cq_valid = false causes occasional crashes
        //may want to look into debugging it
        nicInfo->nic_elem[core_id].wq_tail = 0;
        nicInfo->nic_elem[core_id].cq_head = 0;
        nicInfo->nic_elem[core_id].wq_valid = false;
        nicInfo->nic_elem[core_id].cq_valid = false;
        NICELEM.wq->head = 0;
        NICELEM.cq->tail = 0;
        //TODO - iterate for rb_dir len
        //for (int i = 0; i < 100; i++) {
        for (int i = 0; i < RECV_BUF_POOL_SIZE; i++) {
            NICELEM.rb_dir[i].in_use = false;
            NICELEM.rb_dir[i].is_head = false;
            NICELEM.rb_dir[i].len = 0;
        }

        info("proc %d deregistered with NIC", procIdx);
        std::cout << "cycle: " << zinfo->globPhaseCycles << std::endl;
        break;
    default:
        break;
    }
    return;
}

int aggr = 0;

void cycle_increment_routine(uint64_t& curCycle, int core_id) {
/*
* cycle_increment_routine
*       checks CEQ and RCP-EQ every cycle
*       Process entries that are due (by creating CQ_entries)
*/
    
    glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());

    /* if statement is for preventing crash at the end of simulation */
    if (core_id > ((zinfo->numCores) - 1)) {
        return;  
    }

    if (!(nicInfo->nic_elem[core_id].cq_valid)) {
        return;
    }


    core_ceq_routine(curCycle, nicInfo, core_id);

    RCP_routine(curCycle, nicInfo, core_id);
    
    return;


}

uint32_t assign_core(uint32_t in_core_iterator) {
    glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
    uint64_t min_ceq_size = ~0;//RECV_BUF_POOL_SIZE; // assign some very large number
    uint32_t ret_core_id = in_core_iterator;
    uint32_t core_iterator = in_core_iterator;

    uint32_t numCores = zinfo->numCores; //(nicInfo->expected_core_count + 2);
    
    for (uint32_t i = 0; i < numCores; i++) {
        if (nicInfo->nic_elem[core_iterator].cq_valid == false) {

        }
        else {
            if (nicInfo->nic_elem[core_iterator].ceq_size < min_ceq_size) {
                ret_core_id = core_iterator;
                min_ceq_size = nicInfo->nic_elem[core_iterator].ceq_size;
                if (min_ceq_size == 0) {
                    //info("ret_core_id: %d", ret_core_id);
                    return ret_core_id;
                }
            }

        }

        core_iterator++;
        if (core_iterator >= zinfo->numCores) {
            core_iterator = 0;
        }

    }
    //info("ret_core_id: %d, min_ceq_size: %lu", ret_core_id, min_ceq_size);
    return ret_core_id;

}

int OOOCore::nic_ingress_routine(THREADID tid) {

    OOOCore* core = static_cast<OOOCore*>(cores[tid]);

    glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
    void* lg_p = static_cast<void*>(gm_get_lg_ptr());
    uint64_t packet_rate = nicInfo->packet_injection_rate;

    if (((load_generator*)lg_p)->next_cycle == 0) {
        ((load_generator*)lg_p)->next_cycle = core->curCycle;
        nicInfo->sim_start_time = std::chrono::system_clock::now();
        info("starting sim time count");

    }
    uint32_t core_iterator = 0;

    uint32_t inject_fail_counter = 0;

    for (uint64_t i = 0; i < packet_rate; i++) {

        /* assign core_id in round robin */
        core_iterator++;
        if (core_iterator >= zinfo->numCores) {
            core_iterator = 0;
        }

        //TODO:: load balancing for choosing core
        /* find next valid core that is still running */
        /*
        int drop_count = 0;
        while (!(nicInfo->nic_elem[core_iterator].cq_valid)) {
            core_iterator++;
            if (core_iterator >= zinfo->numCores) {
                core_iterator = 0;
            }
            drop_count++;
            if (drop_count > (nicInfo->expected_core_count)) { 
                std::cout << "other cores deregistered NIC" << std::endl;
                break;
            }
            //DBG code
            if (core_iterator >= zinfo->numCores) {
                info("nic_ingress_routine (line803) - core_iterator out of bound: %d, cycle: %lu", core_iterator, core->curCycle);
            }
        }
        */

        core_iterator = assign_core(core_iterator);

        /* Inject packet (call core function) */
        uint32_t srcId = getCid(tid);
        int inj_attempt = inject_incoming_packet(core->curCycle, nicInfo, lg_p, core_iterator, srcId, core, &(core->cRec), core->l1d);
        if (inj_attempt == -1) {
            //core out of recv buffer. stop injecting for this phase
            inject_fail_counter++;
            if (inject_fail_counter >= (nicInfo->registered_core_count - 1)) {
                break;
            }
        }

    }

    return 0;
}

int OOOCore::nic_egress_routine(THREADID tid) {
    OOOCore* core = static_cast<OOOCore*>(cores[tid]);
    glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
    uint32_t empty_wq_count = 0;
    uint32_t core_iterator = 0;

    while (empty_wq_count < (nicInfo->registered_core_count)) {
        if (core->curCycle < ((OOOCore*)(zinfo->cores[core_iterator]))->getCycles_forSynch()) {
            empty_wq_count++;
        }
        else if (nicInfo->nic_elem[core_iterator].wq_valid) {
            while (check_wq(core_iterator, nicInfo)) {
                wq_entry_t cur_wq_entry = deq_wq_entry(core_iterator, nicInfo);
                process_wq_entry(cur_wq_entry, core_iterator, nicInfo);
                //ISSUE - dequeuing from WQ can have race condition with core. can't use mutex since APP is enqueueing wq?
                        //maybe not, since server and NIC don't WRITE to same structure
            }
            //else {
             //   empty_wq_count++;
            //}
            empty_wq_count++;
        }
        else {
            empty_wq_count++;
        }

        core_iterator++;
        if (core_iterator >= zinfo->numCores) {
            core_iterator = 0;
        }
    }

    return 0;
 
}

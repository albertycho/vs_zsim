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
#include <iostream>
#include <fstream>

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

//#define L1D_LAT 4  // fixed, and FilterCache does not include L1 delay
#define FETCH_BYTES_PER_CYCLE 16
#define ISSUES_PER_CYCLE 4
#define RF_READS_PER_CYCLE 3

//OOOCore::OOOCore(FilterCache* _l1i, FilterCache* _l1d, int _core_id, uint32_t _domain, g_string& _name) : Core(_name), l1i(_l1i), l1d(_l1d), cRec(_domain, _name) {
OOOCore::OOOCore(FilterCache* _l1i, FilterCache* _l1d, uint32_t _domain, g_string& _name, uint32_t _coreIdx, string ingr, string egr) 
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

		if (ingr == "l1") {
			ingr_type = 3;
		} else if (ingr == "l2") {
			ingr_type = 2;
		}
		else if (ingr == "llc") {
			ingr_type = 1;
		}
		else if (ingr == "mem") {
			ingr_type = 0;
		}
		else if (ingr == "ideal") {
			ingr_type = 42;
		}
		else {
			panic("Wrong ingress placement type");
		}

		if (egr == "llc_inval") {
			egr_type = 1;
			egr_inval = 1;
		} else if (egr == "llc_non_inval") {
			egr_type = 1;
			egr_inval = 0;
		} else if (egr == "mem") {
			egr_type = 0;
		}
		else if (egr == "ideal") {
			egr_type = 42;
		}
		else {
			panic("Wrong egress placement type");
		}

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
	robStalls.init("robStalls",  "rob stalls");  coreStat->append(&robStalls);
	robStallCycles.init("robStallCycles",  "rob stall cycles");  coreStat->append(&robStallCycles);

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
InstrFuncPtrs OOOCore::GetFuncPtrs() { return { LoadFunc, StoreFunc, BblFunc, BranchFunc, PredLoadFunc, PredStoreFunc, NicMagicFuncWrapper, FPTR_ANALYSIS}; }

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
		magic_ops=0;
		return;
	}

	/* Simulate execution of previous BBL */

	uint32_t bblInstrs = prevBbl->instrs;
	DynBbl* bbl = &(prevBbl->oooBbl[0]);
	prevBbl = bblInfo;

	uint32_t loadIdx = 0;
	uint32_t storeIdx = 0;
	uint32_t magicOpIdx = 0;

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
						if(core_id > 0) {
							if (ingr_type == 42) //ideal ingress, app core should always hit in l1 for nic-relate data
								reqSatisfiedCycle = l1d->load(addr, dispatchCycle, ingr_type) + L1D_LAT;
							else
								reqSatisfiedCycle = l1d->load(addr, dispatchCycle) + L1D_LAT;
							cRec.record(curCycle, dispatchCycle, reqSatisfiedCycle);
						}
						else {
							reqSatisfiedCycle = l1d->load(addr, dispatchCycle) + L1D_LAT;
							//info("%lld",reqSatisfiedCycle-dispatchCycle);
							cRec.record(curCycle, dispatchCycle, reqSatisfiedCycle);
						}
						//prefetch experiment
						//uint64_t rsp2=l1d->load(addr+(1<<lineBits), dispatchCycle+1)+L1D_LAT;
						//bool doNLPF = true;
						//if(doNLPF){
						if(zinfo->NLPF){
							uint64_t last_access_time=reqSatisfiedCycle - dispatchCycle;
							//info("access took %d",(last_access_time));
							if((reqSatisfiedCycle - dispatchCycle) > (L1D_LAT + 10)){ // l1 miss
								uint64_t pf_n = zinfo->NLPF_n;
								for(int i=0; i<pf_n; i++){
									Address nl_addr = addr + ((i+1) << lineBits);
									if (!(l1d->LineInCache((nl_addr>>lineBits)))) {
										uint64_t rsp2 = l1d->load(nl_addr, dispatchCycle +1+ i, 2) + L1D_LAT; //level=1, pass to l2
										cRec.record(curCycle, dispatchCycle +1+ i, rsp2);
									}
								}
							}
						}

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
					uint64_t reqSatisfiedCycle;
					if(core_id > 0){
						if (ingr_type == 42){ //ideal ingress, app core should always hit in l1 for nic-relate data
							reqSatisfiedCycle = l1d->store(addr, dispatchCycle, ingr_type) + L1D_LAT;
						}
						else{
							reqSatisfiedCycle = l1d->store(addr, dispatchCycle) + L1D_LAT;
						}
						cRec.record(curCycle, dispatchCycle, reqSatisfiedCycle);
					}
					else {
						reqSatisfiedCycle = l1d->store(addr, dispatchCycle) + L1D_LAT;
						//info("%lld", reqSatisfiedCycle-dispatchCycle);
						cRec.record(curCycle, dispatchCycle, reqSatisfiedCycle);
					}
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
			case UOP_NIC_MAGIC:
				{                        
					uint64_t m_core_id = magic_core_ids[magicOpIdx];
					OOOCore* m_core = magic_cores[magicOpIdx];
					ADDRINT val = magic_vals[magicOpIdx];
					ADDRINT field = magic_fields[magicOpIdx];
					//glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
					glob_nic_elements* nicInfo=NULL;
					if (field == 0x16) {
						if ((nicInfo->clean_recv) && (!(nicInfo->zeroCopy))) {

							uint64_t size = nicInfo->forced_packet_size;
							size += CACHE_BLOCK_SIZE - 1;
							size >>= CACHE_BLOCK_BITS;  //number of cache lines

							uint64_t sqCycle = storeQueue.minAllocCycle();
							if (sqCycle > dispatchCycle) {
#ifdef LSU_IW_BACKPRESSURE
								insWindow.poisonRange(curCycle, sqCycle, 0x10 /*PORT_4, stores*/, core_id);
#endif
								dispatchCycle = sqCycle;
							}

							// Wait for all previous store addresses to be resolved (not just ours :))
							dispatchCycle = MAX(lastStoreAddrCommitCycle+1, dispatchCycle);

							Address addr = val;

							uint64_t reqSatisfiedCycle = dispatchCycle;

							while (size) {
								reqSatisfiedCycle = max(l1d->clean(addr, dispatchCycle, nicInfo->clean_recv) + L1D_LAT, reqSatisfiedCycle);
								cRec.record(curCycle, dispatchCycle, reqSatisfiedCycle);
								addr += 64;
								size--;
							}

							commitCycle = reqSatisfiedCycle;

							lastStoreCommitCycle = MAX(lastStoreCommitCycle, reqSatisfiedCycle);

							storeQueue.markRetire(commitCycle);
						}
					}
					else {
						commitCycle = dispatchCycle;
						if (field == 0x13) {
							val = dispatchCycle;
						}
						NicMagicFunc(m_core_id, m_core, val, field);
					}
					magicOpIdx++;
				}
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
		//rob.markRetire(commitCycle);

		uint64_t rob_retire_stall = rob.markRetire_returnStall(commitCycle);
		assert(rob_retire_stall >0);
		if(rob_retire_stall > 0){

			robStalls.inc();
			robStallCycles.inc(rob_retire_stall);
		}


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
	//cornercase where decoder recognizes some inst incorrectly. Print warning instead of assertion
	//assert_msg(loadIdx == loads, "%s: loadIdx(%d) != loads (%d)", name.c_str(), loadIdx, loads);
	//assert_msg(storeIdx == stores, "%s: storeIdx(%d) != stores (%d)", name.c_str(), storeIdx, stores);
	if((loadIdx!=loads) || (storeIdx!=stores)){
		//info("WARNING - %s:  loadIdx(%d) != loads  (%d)", name.c_str(), loadIdx, loads);
		//info("WARNING - %s: storeIdx(%d) != stores (%d)", name.c_str(), storeIdx, stores);
		assert_msg(loads+stores == loadIdx+storeIdx, "loads+stores (%d) DO NOT MATCH loadIdx + StoreIdx (%d)", loads+stores,loadIdx+storeIdx);
		//if(loads+stores == loadIdx+storeIdx){
		//	info("loads+stores MATCH loadIdx + StoreIdx")
		//}
		//else{
		//	info("loads+stores DO NOT MATCH loadIdx + StoreIdx")
		//}

		
	}
	loads = stores = 0;
	magic_ops = 0;


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
			uint64_t fetchLat = 0;
			if(core_id > 0 ) {
				fetchLat = l1i->load(wrongPathAddr + lineSize*i, curCycle) - curCycle;
				cRec.record(curCycle, curCycle, curCycle + fetchLat);
			}
			else {
				fetchLat = l1i->load(wrongPathAddr + lineSize*i, curCycle) - curCycle;
				//info("%lld", fetchLat);
				cRec.record(curCycle, curCycle, curCycle + fetchLat);
			}

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
		uint64_t fetchLat = 0;
		if(core_id > 0) {
			fetchLat = l1i->load(fetchAddr, curCycle) - curCycle;
			cRec.record(curCycle, curCycle, curCycle + fetchLat);
		}
		else {
			fetchLat = l1i->load(fetchAddr, curCycle) - curCycle;
			//info("%lld",fetchLat);
			cRec.record(curCycle, curCycle, curCycle + fetchLat);

		}
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
	//dbgprint
	//if(core_id == 0 && start_cnt_phases)
	//    info("CSimStart called");
	//assert(cycle_adj_idx<100000);
	if(start_cnt_phases)
		cycle_adj_queue[cycle_adj_idx++] = curCycle;
	uint64_t targetCycle = cRec.cSimStart(curCycle);
	//if(core_id == 0){
	//    info("CSimstart called, curCycle %lld, targetCycle %lld",curCycle,targetCycle);
	//}
	assert(targetCycle >= curCycle);
	if (targetCycle > curCycle) advance(targetCycle);
}

void OOOCore::cSimEnd() {

	uint64_t targetCycle = cRec.cSimEnd(curCycle);
	//if(core_id == 0){
	//    info("CSimEnd called, curCycle %lld, targetCycle %lld",curCycle,targetCycle);
	//}
	assert(targetCycle >= curCycle);
	if (targetCycle > curCycle) advance(targetCycle);
	//assert(cycle_adj_idx<100000);
	if(start_cnt_phases)
		cycle_adj_queue[cycle_adj_idx++] = curCycle;
	/*
	if(core_id == 0 && nicInfo->nic_init_done && nicInfo->ready_for_inj==nicInfo->registered_core_count) {
		nicInfo->ready_for_inj = 0xabcd;
	}
	*/
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
	//uint64_t core_id = getCid(tid); // using processID to identify nicCore for now
	core->bbl(bblAddr, bblInfo);

	
	// glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());

	// //TODO check which nic (ingress or egress) should handle this
	// //as of now we stick with one NIC core doing both ingress and egress work
	// if ((nicInfo->nic_ingress_pid == procIdx) && (nicInfo->nic_init_done)) {
	// 	///////////////CALL DEQ_DPQ//////////////////////
	// }
	// // if ((nicInfo->nic_egress_pid == procIdx) && (nicInfo->nic_init_done) && nicInfo->ready_for_inj==0xabcd) {
	// //     uint32_t srcId = getCid(tid);
	// //     //assert(srcId == 0);
	// //     deq_dpq(srcId, core, &(core->cRec), core->l1d/*MemObject* dest*/, core->curCycle, core->egr_type);
	// // }

	// // Simple synchronization mechanism for enforcing producer consumer order for NIC_Ingress and other cores
	// if ((nicInfo->nic_ingress_pid != procIdx) && (nicInfo->nic_init_done)) {
	// 	if (nicInfo->nic_egress_pid == procIdx) {
	// 		//don't need to adjust egress core clock here
	// 	}
	// 	else {
	// 		// Sometime this check gets stuck at the end of the phase, adding safety break
	// 		int safety_counter = 0;
	
			
	// 		while (core->curCycle > (((OOOCore*)(nicInfo->nicCore_ingress))->getCycles_forSynch())+100) {
	// 			struct timespec tim, tim2;
   	// 			tim.tv_sec = 0;
   	// 			tim.tv_nsec = 10;
	// 			nanosleep(&tim, &tim2); // short delay seems to work sufficient
	// 			safety_counter++;
	// 			if (safety_counter > 10) { // >2 seems to work in current env. May need to be adjusted when running on different machine
	// 				break;
	// 			}
	// 		}
			

	// 	}
	// }


	int temp = 0;

	while (core->curCycle > core->phaseEndCycle) {
		//info("while loop for phase sync in bbl");
		core->phaseEndCycle += zinfo->phaseLength;

		//RESIDUE of batch injection per phase. 
		//  Keeping code until we get confidence with per-cycle injection
		// if (core->curCycle <= core->phaseEndCycle) {
		//     /* Do the nic remote packet injection routine once a phase */
		//     /* execute this code only for the NIC process && nic init is done */
		//     if ((nicInfo->nic_ingress_pid == procIdx) && (nicInfo->nic_init_done)) {

		//         /* check if cores finished their processes and exit if so */
		//         if (nicInfo->registered_core_count == 0) {
		//             if (nicInfo->nic_ingress_proc_on) {
		//                 info("ooo_core.cpp - turn off nic proc");
		//                 nicInfo->nic_ingress_proc_on = false;
		//                 nicInfo->nic_egress_proc_on = false;
		//             }
		//         }
		//         else{
		//             //Inject packets for this phase
		//             nic_ingress_routine(tid);

		//         }
		//     }
		//     //else if ((nicInfo->nic_egress_pid == procIdx) && (nicInfo->nic_init_done)) {
		//     //    //call egress routine
		//     //    nic_egress_routine(tid);
		//     //}
		// }

		uint32_t cid = getCid(tid);
		// NOTE: TakeBarrier may take ownership of the core, and so it will be used by some other thread. If TakeBarrier context-switches us,
		// the *only* safe option is to return inmmediately after we detect this, or we can race and corrupt core state. However, the information
		// here is insufficient to do that, so we could wind up double-counting phases.
		uint32_t newCid = TakeBarrier(tid, cid);
		// NOTE: Upon further observation, we cannot race if newCid == cid, so this code should be enough.
		// It may happen that we had an intervening context-switch and we are now back to the same core.
		// This is fine, since the loop looks at core values directly and there are no locals involved,
		// so we should just advance as needed and move on.
		//if (newCid != cid) break;  /*context-switch, we do not own this context anymore*/
		if (newCid != cid){
			info("newCid!=cid");
			break;
		} 
		if (temp) {
			//info("core %d in bbl while loop for %d times",newCid,temp);
		}
		temp++;
	}
}

void OOOCore::BranchFunc(THREADID tid, ADDRINT pc, BOOL taken, ADDRINT takenNpc, ADDRINT notTakenNpc) {
	static_cast<OOOCore*>(cores[tid])->branch(pc, taken, takenNpc, notTakenNpc);
}


void OOOCore::NicMagicFuncWrapper(THREADID tid, ADDRINT val, ADDRINT field) {
	//info("NicMagicFuncWrapper");
	static_cast<OOOCore*>(cores[tid])->NicMagicFunc_on_trigger(tid, val, field);
}

inline void OOOCore::NicMagicFunc_on_trigger(THREADID tid, ADDRINT val, ADDRINT field) {
	//info("NicMagicFunc_on_trigger");
	magic_core_ids[magic_ops] = getCid(tid);
	magic_fields[magic_ops] = field;
	magic_vals[magic_ops] = val;
	magic_cores[magic_ops] = static_cast<OOOCore*>(cores[tid]);
	magic_ops++;
	//info("NicMagicFunc_on_trigger done");
	return;
}

//void OOOCore::NicMagicFunc(THREADID tid, ADDRINT val, ADDRINT field) {
void OOOCore::NicMagicFunc(uint64_t core_id, OOOCore* core, ADDRINT val, ADDRINT field) {
	info("for non sweeper, not expecting this function to be called. field:%lx, val:%lx",field, val);
	//uint64_t core_id = getCid(tid);
	//glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
	//void* lg_p_vp = static_cast<void*>(gm_get_lg_ptr());
	//load_generator* lg_p = (load_generator*)lg_p_vp;
	glob_nic_elements* nicInfo=NULL;
	load_generator* lg_p = NULL;

	uint64_t num_cline=0;
	switch (field) {
		case 0://WQ

			if (NICELEM.wq_valid == true) {
				info("duplicate WQ register for core %lu", core_id);
			}

			info("core_id:%d",core_id);
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

			NICELEM.cq->tail = 0;
			NICELEM.cq->SR = 1;
			NICELEM.ncq_SR = 1;
			for (int i = 0; i < MAX_NUM_WQ; i++) {
				NICELEM.cq->q[i].SR = 0;
			}

			NICELEM.cq_valid = true;
			*static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(nicInfo->nic_elem[core_id].cq);
			futex_lock(&nicInfo->nic_lock);
			nicInfo->registered_core_count = nicInfo->registered_core_count + 1;
			futex_unlock(&nicInfo->nic_lock);
			if ((nicInfo->registered_core_count == nicInfo->expected_core_count) && (nicInfo->registered_non_net_core_count == nicInfo->expected_non_net_core_count)) {
				if (nicInfo->nic_ingress_proc_on) {
					nicInfo->nic_init_done = true;
					info("nic init completed");
				}
			}
			//info("registered core count: %d, expected core count: %d, reg nonnetcore: %d, exp nonntecore: %d, ingress_on:%d",nicInfo->registered_core_count, nicInfo->expected_core_count, nicInfo->registered_non_net_core_count, nicInfo->expected_non_net_core_count, nicInfo->nic_ingress_proc_on ? 1:0);
			info("core %d registered CQ at addrs %lld", core_id, nicInfo->nic_elem[core_id].cq);
			break;
		case 2: // lbuf
			*static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(&(nicInfo->nic_elem[core_id].lbuf[0]));
			info("core %d registered LBUF at addrs %lld", core_id, nicInfo->nic_elem[core_id].lbuf);
			break;

		case 3: //get buf_size for lbuf. Is called before case 2 (but this was coded later)
			num_cline = (((UINT64)(val)) / (sizeof(z_cacheline))); //+1 in case remainder..
			//num_cline = num_cline * 4;
			//NICELEM.lbuf = gm_calloc<z_cacheline>(num_cline);
			NICELEM.lbuf = gm_memalign<z_cacheline>(CACHE_LINE_BYTES, num_cline);
			break;

		case NOTIFY_WQ_WRITE://NOTIFY WQ WRITE from application
			//info("notify_wq_write")
			//nic_rgp_action call moved to cycle routine
			//nic_rgp_action(core_id, nicInfo);
			break;
		case 0xB: //indicate app is nic_proxy_process (INGRESS)
			*static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(&(nicInfo->nic_ingress_proc_on));
			nicInfo->nic_ingress_pid = procIdx;
			nicInfo->nic_ingress_proc_on = true;
			info("nic ingress pid:%d, cid:%lu", procIdx, core_id);
			if ((nicInfo->registered_core_count == nicInfo->expected_core_count) && (nicInfo->registered_non_net_core_count == nicInfo->expected_non_net_core_count)) {
				nicInfo->nic_init_done = true;
			}
			//nicInfo->nicCore_ingress = (void*) cores[tid];
			nicInfo->nicCore_ingress = (void*) core;
			break;

		case 0xC: //indicate app is nic_proxy_process (EGRESS)
			*static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(&(nicInfo->nic_egress_proc_on));
			nicInfo->nic_egress_pid = procIdx;
			nicInfo->nic_egress_proc_on = true;
			info("nic egress  pid:%d, cid:%lu", procIdx, core_id);
			if ((nicInfo->registered_core_count == nicInfo->expected_core_count) && (nicInfo->registered_non_net_core_count == nicInfo->expected_non_net_core_count)) {
				nicInfo->nic_init_done = true;
			}
			//nicInfo->nicCore_egress = (void*)cores[tid];
			nicInfo->nicCore_egress = (void*)core;
			break;

		case 0xD: //send all client_done boolean to the server app to monitor for termination condition
			*static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(&(lg_p->all_packets_completed));
			//info("after handling monitor_client_done");
			futex_lock(&nicInfo->nic_lock);
			nicInfo->ready_for_inj++;
			futex_unlock(&nicInfo->nic_lock);
			start_cnt_phases = zinfo->numPhases;
			break;

		case 0xE:
			assert(nicInfo->nic_elem[core_id].service_in_progress==false);
			nicInfo->nic_elem[core_id].service_in_progress=true;
			nicInfo->nic_elem[core_id].cur_service_start_time=core->curCycle;
			break;
		case 0xF:
			assert(nicInfo->nic_elem[core_id].service_in_progress==true);
			nicInfo->nic_elem[core_id].service_times[(nicInfo->nic_elem[core_id].st_size)]=
				(core->curCycle) - (nicInfo->nic_elem[core_id].cur_service_start_time);
			nicInfo->nic_elem[core_id].st_size = nicInfo->nic_elem[core_id].st_size + 1;
			nicInfo->nic_elem[core_id].service_in_progress=false;
			break;
		case 0x10:
			nicInfo->nic_elem[core_id].cq_check_inner_loop_count+=(uint64_t)val;
			break;

		case 0x11:
			nicInfo->nic_elem[core_id].cq_check_spin_count+=(uint64_t)val;
			break;

		case 0x12:
			nicInfo->nic_elem[core_id].cq_check_outer_loop_count += (uint64_t)val;
			break;

		case 0x13:      //timestamp
			nicInfo->nic_elem[core_id].ts_queue[nicInfo->nic_elem[core_id].ts_idx++] = val;
			break;
		case 0x14:      //closed-loop injection
			assert(core_id==this->core_id);
			if(nicInfo->send_in_loop){
				assert(nicInfo->nic_elem[core_id].packet_pending==true);
				futex_lock(&nicInfo->nic_elem[core_id].packet_pending_lock);
				nicInfo->nic_elem[core_id].packet_pending=false;
				futex_unlock(&nicInfo->nic_elem[core_id].packet_pending_lock);
			}
			break;
		// case 0x15:      //registering non-network cores
		// 	{
		// 		futex_lock(&nicInfo->nic_lock);
		// 		nicInfo->registered_non_net_core_count++;
		// 		futex_unlock(&nicInfo->nic_lock);
		// 		if ((nicInfo->registered_core_count == nicInfo->expected_core_count) && (nicInfo->registered_non_net_core_count == nicInfo->expected_non_net_core_count)) {
		// 			if (nicInfo->nic_ingress_proc_on) {
		// 				nicInfo->nic_init_done = true;
		// 			}
		// 		}
		// 		info("registered_non_net_core_count: %d, expected_non_net_core_count: %d\n", nicInfo->expected_non_net_core_count ,nicInfo->expected_core_count);
		// 		break;
		// 	}

		case 0x17: //register all_packets_SENT for l3fwd. used to prevent hang when batching
			*static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(&(lg_p->all_packets_sent));

			break;
		case 0x18: //register all_packets_done for memhog, to track termination
			*static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(&(lg_p->all_packets_completed));
			info("MEMHOG application registered");

			break;

		case 0x30: //30~32: register matrix for matrix mult
			nicInfo->matA[10] = 0xc0ffee; // connect check
			*static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(nicInfo->matA);
			info("matA registered");
			break;
		case 0x31: //30~32: register matrix for matrix mult
			*static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(nicInfo->matB);
			info("matB registered");
			break;
		case 0x32: //30~32: register matrix for matrix mult
			*static_cast<UINT64*>((UINT64*)(val)) = (UINT64)(nicInfo->matC);
			info("matC registered");
			break;
		// case 0x33: //0x33 informs init done for MM
		// 	futex_lock(&nicInfo->mm_core_lock);
		// 	nicInfo->registered_mm_cores++;
		// 	futex_unlock(&nicInfo->mm_core_lock);
		// 	info("MM init done");
		// 	break;

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


	bool check_cq_depth(uint32_t lg_i, uint64_t injection_cycle){
		//check if cq_depth is shorter than depth specified to maintain
		//if shorter, schedule incoming packet for this cycle
		//(will hang if target depth is smaller than batchsize of the app)


		glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
		void* lg_p_vp = static_cast<void*>(gm_get_lg_ptr());
		load_generator* lg_p = (load_generator*)lg_p_vp;
		uint32_t numCores = lg_p->lgs[lg_i].num_cores;
		uint32_t core_iterator = lg_p->lgs[lg_i].last_core;

		for (uint32_t i = 0; i < numCores; i++) {
			uint32_t core_id_l = lg_p->lgs[lg_i].core_ids[core_iterator];
			if (nicInfo->nic_elem[core_id_l].cq_valid == true) {
				uint32_t cq_head = nicInfo->nic_elem[core_id_l].cq_head;
				uint32_t cq_tail = nicInfo->nic_elem[core_id_l].cq->tail;
				if (cq_head < cq_tail) {
					cq_head += MAX_NUM_WQ;
				}
				uint32_t cq_size = cq_head - cq_tail;
				uint32_t total_q_size = cq_size + nicInfo->nic_elem[core_id_l].ceq_size;
				if ((total_q_size) < lg_p->lgs[lg_i].q_depth) {
					//there is a core with q_depth smaller than target.
					lg_p->lgs[lg_i].next_cycle=injection_cycle;			
					return true;

				}

			}

			core_iterator++;
			if (core_iterator >= numCores) {
				core_iterator = 0;
			}

		}
		//all cores are at wanted q_depth. set next_cycle far
		// (this function will reset it to cur_cycle when needed)
		lg_p->lgs[lg_i].next_cycle=injection_cycle+1000;	
		return false;


	}

	uint32_t assign_core(uint32_t lg_i) {

		glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
		void* lg_p_vp = static_cast<void*>(gm_get_lg_ptr());
		load_generator* lg_p = (load_generator*)lg_p_vp;

		uint64_t min_q_size = ~0;//RECV_BUF_POOL_SIZE; // assign some very large number
		uint32_t ret_core_id = lg_p->lgs[lg_i].last_core;
		uint32_t core_iterator = lg_p->lgs[lg_i].last_core;
		//core_iterator and last_core will have index for core_ids, not the core_id itself!

		uint32_t numCores = lg_p->lgs[lg_i].num_cores; //(nicInfo->expected_core_count + 2);

		if(nicInfo->load_balance==1){ // random
			core_iterator = std::rand() % lg_p->lgs[lg_i].num_cores;
			uint32_t core_id_r = lg_p->lgs[lg_i].core_ids[core_iterator];
			uint32_t tmp = 0;
			while(nicInfo->nic_elem[core_id_r].cq_valid!=true){
				core_iterator++;
				if(core_iterator>= lg_p->lgs[lg_i].num_cores){
					core_iterator=0;
				}
				core_id_r = lg_p->lgs[lg_i].core_ids[core_iterator];
				tmp++;
				if (tmp > lg_p->lgs[lg_i].num_cores) {
					info("assign core: no active cores");
					return core_id_r;
				}

			}

			//return core_iterator;
			return core_id_r;
		}

		//increment it once at beginning for round-robin fairness
		core_iterator++;
		if (core_iterator >= numCores) {
			core_iterator = 0;
		}


		for (uint32_t i = 0; i < numCores; i++) {
			uint32_t core_id_l = lg_p->lgs[lg_i].core_ids[core_iterator];
			if (nicInfo->nic_elem[core_id_l].cq_valid == true) {
				uint32_t cq_head = nicInfo->nic_elem[core_id_l].cq_head;
				uint32_t cq_tail = nicInfo->nic_elem[core_id_l].cq->tail;
				if (cq_head < cq_tail) {
					cq_head += MAX_NUM_WQ;
				}
				uint32_t cq_size = cq_head - cq_tail;
				if ((nicInfo->nic_elem[core_id_l].ceq_size + cq_size) < min_q_size) {
					ret_core_id = core_id_l;
					min_q_size = nicInfo->nic_elem[core_id_l].ceq_size + cq_size;
					if (min_q_size == 0) {
						//info("ret_core_id: %d", ret_core_id);
						lg_p->lgs[lg_i].last_core = core_iterator;
						return ret_core_id;
					}
				}

			}

			core_iterator++;
			if (core_iterator >= numCores) {
				core_iterator = 0;
			}

		}
		//info("ret_core_id: %d, min_cq_size: %lu, min_ceq_size:%lu", ret_core_id, min_q_size-nicInfo->nic_elem[ret_core_id].ceq_size, nicInfo->nic_elem[ret_core_id].ceq_size);
		lg_p->lgs[lg_i].last_core = core_iterator;
		return ret_core_id;

	}


	uint32_t find_idle_core(uint32_t in_core_iterator=0) {
		glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
		uint32_t ret_core_id = in_core_iterator;
		uint32_t core_iterator = in_core_iterator;

		uint32_t numCores = zinfo->numCores; //(nicInfo->expected_core_count + 2);

		//increment it once at beginning for round-robin fairness
		core_iterator++;
		if (core_iterator >= numCores) {
			core_iterator = 0;
		}

		for (uint32_t i = 0; i < numCores; i++) {
			if (nicInfo->nic_elem[core_iterator].cq_valid && !(nicInfo->nic_elem[core_iterator].ceq_size)) {
				ret_core_id = core_iterator;
			}

			core_iterator++;
			if (core_iterator >= numCores) {
				core_iterator = 0;
			}

		}
		//info("ret_core_id: %d, min_ceq_size: %lu", ret_core_id, min_ceq_size);
		return ret_core_id;

	}



	//int OOOCore::nic_ingress_routine_per_cycle(THREADID tid) {
	bool flag_t;
	int arr[MAX_NUM_CORES];

	int OOOCore::nic_ingress_routine_per_cycle(uint32_t srcId) {
		//OOOCore* core = static_cast<OOOCore*>(cores[tid]);
		glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
		void* lg_p_vp = static_cast<void*>(gm_get_lg_ptr());
		load_generator* lg_p = (load_generator*) lg_p_vp;
		OOOCore* core = static_cast<OOOCore*>(nicInfo->nicCore_ingress);

		if (nicInfo->num_mm_cores > nicInfo->registered_mm_cores) {
			return 0;
		}
		if ((nicInfo->nic_ingress_pid == procIdx) && (nicInfo->nic_init_done)) { //only run for nic_core
			//assert(srcId == 0);
			if (nicInfo->registered_core_count == 0) { // we're done, don't do anything
				if (nicInfo->nic_ingress_proc_on) {
					//info("ooo_core.cpp - turn off nic proc");
					//nicInfo->nic_ingress_proc_on = false;
					nicInfo->nic_egress_proc_on = false;
				}
			}
			else if (!(lg_p->all_packets_sent) && nicInfo->ready_for_inj==0xabcd){
				//Inject packets if next packet is due according to loadgen->next_cycle            
				if (lg_p->num_loadgen > 0) {
					if (lg_p->lgs[0].next_cycle == 0) {
						for (int ii = 0; ii < lg_p->num_loadgen; ii++) {
							lg_p->lgs[ii].next_cycle = core->curCycle + ii;
						}
						nicInfo->sim_start_time = std::chrono::system_clock::now();
						info("starting sim time count");
						core->start_cnt_phases = zinfo->numPhases;
					}
				}
				if(lg_p->sent_packets==0){
					for (int ii = 0; ii < lg_p->num_loadgen; ii++) {
						lg_p->lgs[ii].next_cycle = core->curCycle + ii;
					}
				}
				if(nicInfo->send_in_loop){
					//assumed we'll only use send_in_loop with 1 loadgen
					if (lg_p->num_loadgen > 1) {
						panic("don't support send_in_loop with multiple Load Gen");
					}
					//info("if send_in_loop: ooo_core.cpp line 973");
					uint64_t injection_cycle = core->curCycle;
					for(int ii=3; ii<(nicInfo->registered_core_count+3); ii++){
						if(!(nicInfo->nic_elem[ii].packet_pending)){
							futex_lock(&nicInfo->nic_elem[ii].packet_pending_lock);
							nicInfo->nic_elem[ii].packet_pending = true;
							futex_unlock(&nicInfo->nic_elem[ii].packet_pending_lock);
							int inj_attempt;
							if (core->ingr_type < 2) {
								inj_attempt = inject_incoming_packet(injection_cycle, nicInfo, lg_p, ii, srcId, core, &(core->cRec), core->l1d, core->ingr_type, 0);
							}
							else {
								inj_attempt = inject_incoming_packet(injection_cycle, nicInfo, lg_p, ii, srcId, core, &(core->cRec), l1d_caches[ii], core->ingr_type, 0);
							}
						}
					}
				}
				else{
					if(nicInfo->first_injection<1000*nicInfo->registered_core_count) {
						uint64_t injection_cycle = core->curCycle;
						for (int ii = 0; ii < lg_p->num_loadgen; ii++) {
							for (int jj = 0; jj < lg_p->lgs[ii].num_cores; jj++) {
								uint32_t core_id = lg_p->lgs[ii].core_ids[jj];
								if (!(nicInfo->nic_elem[core_id].packet_pending) && arr[core_id] < 1000) {
									futex_lock(&nicInfo->nic_elem[core_id].packet_pending_lock);
									nicInfo->nic_elem[core_id].packet_pending = true;
									futex_unlock(&nicInfo->nic_elem[core_id].packet_pending_lock);
									int inj_attempt;
									arr[core_id]++;
									if (core->ingr_type < 2) {
										inj_attempt = inject_incoming_packet(injection_cycle, nicInfo, lg_p, core_id, srcId, core, &(core->cRec), core->l1d, core->ingr_type, ii);
									}
									else {
										inj_attempt = inject_incoming_packet(injection_cycle, nicInfo, lg_p, core_id, srcId, core, &(core->cRec), l1d_caches[core_id], core->ingr_type, ii);
									}
									nicInfo->first_injection++;
									for(uint32_t mm=0; mm<lg_p->num_loadgen;mm++){
										lg_p->lgs[mm].next_cycle = injection_cycle;
									}
								}
							}
						}
						if(nicInfo->first_injection==(1000*nicInfo->registered_core_count)) {
							flag_t = true;
						}
					}
					else {
						uint64_t injection_cycle = core->curCycle;
						for (int ii = 0; ii < lg_p->num_loadgen; ii++) {
							if(flag_t){
								info("done with closed loop warmup!, sampling phase: %d", nicInfo->sampling_phase_index);
								lg_p->lgs[ii].next_cycle = core->curCycle + 1;
								flag_t=false;
								info("Spillover count during warmup: %d",nicInfo->spillover_count)
								/*
								   if(nicInfo->closed_loop_done==false){
								   uint32_t cycle_diff = injection_cycle- (lg_p->lgs[0].next_cycle);
								   info("loadgen behind by %d cycles afte warmup\n",cycle_diff);
								   for(uint32_t mm=0; mm<lg_p->num_loadgen;mm++){
								   lg_p->lgs[mm].next_cycle = injection_cycle;
								   }

								   info("done with closed loop warmup!, sampling phase: %d", nicInfo->sampling_phase_index);
								   nicInfo->closed_loop_done=true;
								   */
							}
							//////// target_cq_len /////////////
							// check cq len of target cores, if len < target, set next injection cycle to cur_cycle +1
							// how to handle multi cores? - schedule injection if ANY core has len < target,
							//  let assign_core prioritize core with len<target
							//  @update_loadgen, just set next cycle far away, the above checker will reschedule when needed
							if(lg_p->lgs[ii].arrival_dist==3){ //sustain q_depth
								check_cq_depth(ii, injection_cycle);
							}
							if (lg_p->lgs[ii].next_cycle <= injection_cycle /*&& idle_core > 1*/) {
								//uint32_t core_iterator = assign_core(core_iterator);
								uint32_t core_iterator = assign_core(ii);
								if (nicInfo->nic_elem[core_iterator].packet_pending == false) {
									//uint32_t srcId = getCid(tid);
									int inj_attempt;
									if (core->ingr_type < 2)
										inj_attempt = inject_incoming_packet(injection_cycle, nicInfo, lg_p, core_iterator, srcId, core, &(core->cRec), core->l1d, core->ingr_type, ii);
									else
										inj_attempt = inject_incoming_packet(injection_cycle, nicInfo, lg_p, core_iterator, srcId, core, &(core->cRec), l1d_caches[core_iterator], core->ingr_type,ii);
									return inj_attempt;
								}
							}
							}

						}
					}
				}    
			}

			return 0;
		}


		void cycle_increment_routine(uint64_t& curCycle, int core_id) {
			/*
			 * cycle_increment_routine
			 *       checks CEQ and RCP-EQ every cycle
			 *       Process entries that are due (by creating CQ_entries)
			 */

			//This function is for when NIC is used (developed for sweeper)
			//For CXL study, don't do anything
			return;

			glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
			if(nicInfo->nic_ingress_proc_on==false){ // don't do anything for non-nic simulation
				return;
			}

			/* if statement is for preventing crash at the end of simulation */
			if (core_id > ((zinfo->numCores) - 1)) {
				return;  
			}


			if(procIdx==nicInfo->nic_ingress_pid){
				//std::cout<<"procIdx available in cycle_increment routine"<<std::endl;
				((OOOCore *)(nicInfo->nicCore_ingress))->nic_ingress_routine_per_cycle(core_id);
				if(nicInfo->pd_flag){
					//info("returned from nic_ingress_routine_per_cycle");
				}
				/*void* lg_p_vp = static_cast<void*>(gm_get_lg_ptr());
				  load_generator* lg_p = (load_generator*)lg_p_vp;
				  if (lg_p->all_packets_sent && !(lg_p->all_packets_completed)) {
				  info("outstanding packets to be completed: %d",(lg_p->target_packet_count - nicInfo->latencies_size));
				  }*/
			}


			if ((nicInfo->nic_egress_pid == procIdx) && (nicInfo->nic_init_done) && nicInfo->ready_for_inj==0xabcd) {
				//uint32_t srcId = getCid(tid);
				//assert(srcId == 0);
				OOOCore* egcore = (OOOCore*)(nicInfo->nicCore_egress);
				OOOCoreRecorder* egcRec = egcore->get_cRec_ptr();
				nic_rgp_action(curCycle, nicInfo);
				deq_dpq(1, egcore, egcRec, egcore->l1d/*MemObject* dest*/, curCycle, egcore->egr_type);
			}

			if (!(nicInfo->nic_elem[core_id].cq_valid)) {
				return;
			}


			//nic_ingress_routine_per_cycle(core_id);

			core_ceq_routine(curCycle, nicInfo, core_id);

			RCP_routine(curCycle, nicInfo, core_id);
			
			if(nicInfo->pd_flag){
			//info("returning from cycle_increment routine");
			nicInfo->pd_flag=false;
			}

			return;


		}


		//we didn't go with a separate egress nic core, so unused for
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

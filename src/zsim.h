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

#ifndef ZSIM_H_
#define ZSIM_H_

#include <stdint.h>
#include <sys/time.h>
#include "constants.h"
#include "debug.h"
#include "locks.h"
#include "pad.h"

#define MAX_NUM_WQ 8

#define NICELEM 		nicInfo->nic_elem[procIdx]
#define NWQ_VAL 		nicInfo->nic_elem[procIdx].wq_valid
#define NCQ_VAL 		nicInfo->nic_elem[procIdx].cq_valid

#define NWQ_HEAD 		nicInfo->nic_elem[procIdx].wq_head
#define NWQ_TAIL 		nicInfo->nic_elem[procIdx].wq_tail
#define NCQ_HEAD 		nicInfo->nic_elem[procIdx].cq_head
#define NCQ_TAIL 		nicInfo->nic_elem[procIdx].cq_tail

#define NWQ_Q 			nicInfo->nic_elem[procIdx].wq
#define NCQ_Q 			nicInfo->nic_elem[procIdx].cq


#define NICELEM_P(a) 	nicInfo->nic_elem[a]
#define NWQ_val_P(a) 	nicInfo->nic_elem[a].wq_valid
#define NCQ_VAL_P(a)	nicInfo->nic_elem[a].cq_valid

#define NWQ_HEAD_P(a) 	nicInfo->nic_elem[a].wq_head
#define NWQ_TAIL_P(a) 	nicInfo->nic_elem[a].wq_tail
#define NCQ_HEAD_P(a) 	nicInfo->nic_elem[a].cq_head
#define NCQ_TAIL_P(a) 	nicInfo->nic_elem[a].cq_tail

#define NWQ_Q_P(a)		nicInfo->nic_elem[a].wq
#define NCQ_Q_P(a)		nicInfo->nic_elem[a].cq


#define RMC_READ                1
#define RMC_WRITE               2
#define RMC_RMW                 3
#define RMC_SABRE	            4
#define RMC_RECV                5
#define RMC_MSG_NACK            6

#define SEND_OP_SHIFT           4
#define PAIRED_SEND_SHIFT       0
#define INCOMING_SEND_SHIFT     1
#define INCOMING_RESP_SHIFT     2
#define RMC_SEND                (1<<SEND_OP_SHIFT)
#define RMC_PAIRED_SEND         (1<<SEND_OP_SHIFT) | (1<<PAIRED_SEND_SHIFT)
#define RMC_INCOMING_SEND       (1<<SEND_OP_SHIFT) | (1<<INCOMING_SEND_SHIFT)
#define RMC_INCOMING_RESP       (1<<SEND_OP_SHIFT) | (1<<INCOMING_RESP_SHIFT)
#define RMC_INVAL               42

#define RECV_BUF_POOL_SIZE 100

class Core;
class Scheduler;
class AggregateStat;
class StatsBackend;
class ProcessTreeNode;
class ProcessStats;
class ProcStats;
class EventQueue;
class ContentionSim;
class EventRecorder;
class PinCmd;
class PortVirtualizer;
class VectorCounter;
class AccessTraceWriter;
class TraceDriver;
template <typename T> class g_vector;

struct ClockDomainInfo {
    uint64_t realtimeOffsetNs;
    uint64_t monotonicOffsetNs;
    uint64_t processOffsetNs;
    uint64_t rdtscOffset;
    lock_t lock;
};

class TimeBreakdownStat;
enum ProfileStates {
    PROF_INIT = 0,
    PROF_BOUND = 1,
    PROF_WEAVE = 2,
    PROF_FF = 3,
};

enum ProcExitStatus {
    PROC_RUNNING = 0,
    PROC_EXITED = 1,
    PROC_RESTARTME  = 2
};



typedef struct wq_entry{
	//first double-word (8 bytes)
	uint32_t op;        //up to 64 soNUMA ops
	volatile bool SR;        //sense reverse bit
	volatile bool valid;    //set with a new WQ entry, unset when entry completed. Required for pipelining async ops
	uint64_t buf_addr;
	uint32_t cid;
	uint32_t nid;
	//second double-word (8 bytes)
	uint64_t offset;
	uint64_t length;
} wq_entry_t;

typedef struct cq_entry{
    volatile bool SR;     //sense reverse bit
	volatile bool valid;
    uint32_t success; /* Success bit/type */
    //volatile unsigned int tid; /* Uses tid to specify incoming send id and qp */
    uint32_t tid; /* Uses tid to specify incoming send id and qp */
    //volatile uint64_t recv_buf_addr; /* Incoming recv buf block address (42 bits) */
    uint64_t recv_buf_addr; /* Incoming recv buf block address (42 bits) */
} cq_entry_t;

typedef struct rmc_wq {
    wq_entry_t q[MAX_NUM_WQ];
    uint32_t head;
    bool SR ;    //sense reverse bit
} rmc_wq_t;

typedef struct rmc_cq {
    cq_entry_t q[MAX_NUM_WQ];
    uint32_t tail;
    bool SR ;    //sense reverse bit
} rmc_cq_t;

typedef struct recv_buf_dir {
	bool in_use;
	bool is_head;
	uint32_t len;
} recv_buf_dir_t;

struct nic_element {
	//rmc_wq_t wq;
	//rmc_cq_t cq;
	rmc_wq_t *wq;
	rmc_cq_t *cq;
	uint64_t wq_tail;
	uint64_t cq_head;
	bool wq_valid;
	bool cq_valid;
	bool nwq_SR;
	bool ncq_SR;
	PAD();
	uint32_t recv_buf[RECV_BUF_POOL_SIZE];
	recv_buf_dir_t rb_dir[RECV_BUF_POOL_SIZE];
	uint32_t lbuf[RECV_BUF_POOL_SIZE];
};


struct glob_nic_elements {
	nic_element nic_elem[MAX_THREADS];
};

struct GlobSimInfo {
    //System configuration values, all read-only, set at initialization
    uint32_t numCores;
    uint32_t lineSize;

    //Cores
    Core** cores;

    PAD();

    EventQueue* eventQueue;
    Scheduler* sched;

    //Contention simulation
    uint32_t numDomains;
    ContentionSim* contentionSim;
    EventRecorder** eventRecorders; //CID->EventRecorder* array

    PAD();

    //World-readable
    uint32_t phaseLength;
    uint32_t statsPhaseInterval;
    uint32_t freqMHz;

    //Maxima/termination conditions
    uint64_t maxPhases; //terminate when this many phases have been reached
    uint64_t maxMinInstrs; //terminate when all threads have reached this many instructions
    uint64_t maxTotalInstrs; //terminate when the aggregate number of instructions reaches this number
    uint64_t maxSimTimeNs; //terminate when the simulation time (bound+weave) exceeds this many ns
    uint64_t maxProcEventualDumps; //term if the number of heartbeat-triggered process dumps reached this (MP/MT)

    bool ignoreHooks;
    bool blockingSyscalls;
    bool perProcessCpuEnum; //if true, cpus are enumerated according to per-process masks (e.g., a 16-core mask in a 64-core sim sees 16 cores)
    bool oooDecode; //if true, Decoder does OOO (instr->uop) decoding

    PAD();

    //Writable, rarely read, unshared in a single phase
    uint64_t numPhases;
    uint64_t globPhaseCycles; //just numPhases*phaseCycles. It behooves us to precompute it, since it is very frequently used in tracing code.

    uint64_t procEventualDumps;

    PAD();

    ClockDomainInfo clockDomainInfo[MAX_CLOCK_DOMAINS];
    PortVirtualizer* portVirt[MAX_PORT_DOMAINS];

    lock_t ffLock; //global, grabbed in all ff entry/exit ops.

    volatile uint32_t globalActiveProcs; //used for termination
    //Counters below are used for deadlock detection
    volatile uint32_t globalSyncedFFProcs; //count of processes that are in synced FF
    volatile uint32_t globalFFProcs; //count of processes that are in either synced or unsynced FF

    volatile bool terminationConditionMet;

    const char* outputDir; //all the output files mst be dumped here. Stored because complex workloads often change dir, then spawn...

    AggregateStat* rootStat;
    g_vector<StatsBackend*>* statsBackends; // used for termination dumps
    StatsBackend* periodicStatsBackend;
    StatsBackend* eventualStatsBackend;
    ProcessStats* processStats;
    ProcStats* procStats;

    TimeBreakdownStat* profSimTime;
    VectorCounter* profHeartbeats; //global b/c number of processes cannot be inferred at init time; we just size to max

    uint64_t trigger; //code with what triggered the current stats dump

    ProcessTreeNode* procTree;
    ProcessTreeNode** procArray; //a flat view of the process tree, where each process is indexed by procIdx
    ProcExitStatus* procExited; //starts with all set to PROC_RUNNING, each process sets to PROC_EXITED or PROC_RESTARTME on exit. Used to detect untimely deaths (that don;t go thropugh SimEnd) in the harness and abort.
    uint32_t numProcs;
    uint32_t numProcGroups;

    PinCmd* pinCmd; //enables calls to exec() to modify Pin's calling arguments, see zsim.cpp

    // If true, threads start as shadow and have no effect on simulation until they call the register magic op
    bool registerThreads;

    //If true, do not output vectors in stats -- they're bulky and we barely need them
    bool skipStatsVectors;

    //If true, all the regular aggregate stats are summed before dumped, e.g. getting one thread record with instrs&cycles for all the threads
    bool compactPeriodicStats;

    bool attachDebugger;
    int harnessPid; //used for debugging purposes

    struct LibInfo libzsimAddrs;

    bool ffReinstrument; //true if we should reinstrument on ffwd, works fine with ST apps and it's faster since we run with basically no instrumentation, but it's not precise with MT apps

    //fftoggle stuff
    lock_t ffToggleLocks[256]; //f*ing Pin and its f*ing inability to handle external signals...
    lock_t pauseLocks[256]; //per-process pauses
    volatile bool globalPauseFlag; //if set, pauses simulation on phase end
    volatile bool externalTermPending;

    // Trace writers (stored globally because they need to be deleted when the simulation ends)
    g_vector<AccessTraceWriter*>* traceWriters;

    // Trace-driven simulation (no cores)
    bool traceDriven;
    TraceDriver* traceDriver;
};

extern bool cq_valid[MAX_THREADS];
extern bool wq_valid[MAX_THREADS];


//Process-wide global variables, defined in zsim.cpp
extern Core* cores[MAX_THREADS]; //tid->core array
extern uint32_t procIdx;
extern uint32_t lineBits; //process-local for performance, but logically global
extern uint64_t procMask;

extern GlobSimInfo* zinfo;

extern glob_nic_elements* nicInfo;

//Process-wide functions, defined in zsim.cpp
uint32_t getCid(uint32_t tid);
uint32_t TakeBarrier(uint32_t tid, uint32_t cid);
void SimEnd(); //only call point out of zsim.cpp should be watchdog threads

#endif  // ZSIM_H_

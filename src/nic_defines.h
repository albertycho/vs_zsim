#include "galloc.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <string>
#include "pad.h"
#include <unistd.h>
#include "locks.h"
#include "RPCGenerator.hpp"
#include "libzsim/zsim_nic_defines.hpp"
#include <map>
#include <memory>
#include <queue>
#include "g_std/g_list.h"
#include <chrono>
#include "g_std/g_unordered_map.h"

#ifndef _NIC_DEFINES_H_
#define _NIC_DEFINES_H_

//#define MAX_NUM_CORES 128 //probably will support more
#define MAX_NUM_CORES 70 //probably will support more

//#define MAX_NUM_WQ 8

#define NICELEM 		nicInfo->nic_elem[core_id]
#define NWQ_VAL 		nicInfo->nic_elem[core_id].wq_valid
#define NCQ_VAL 		nicInfo->nic_elem[core_id].cq_valid

#define NWQ_HEAD 		nicInfo->nic_elem[core_id].wq_head
#define NWQ_TAIL 		nicInfo->nic_elem[core_id].wq_tail
#define NCQ_HEAD 		nicInfo->nic_elem[core_id].cq_head
#define NCQ_TAIL 		nicInfo->nic_elem[core_id].cq_tail

#define NWQ_Q 			nicInfo->nic_elem[core_id].wq
#define NCQ_Q 			nicInfo->nic_elem[core_id].cq


#define NICELEM_P(a) 	nicInfo->nic_elem[a]
#define NWQ_val_P(a) 	nicInfo->nic_elem[a].wq_valid
#define NCQ_VAL_P(a)	nicInfo->nic_elem[a].cq_valid

#define NWQ_HEAD_P(a) 	nicInfo->nic_elem[a].wq_head
#define NWQ_TAIL_P(a) 	nicInfo->nic_elem[a].wq_tail
#define NCQ_HEAD_P(a) 	nicInfo->nic_elem[a].cq_head
#define NCQ_TAIL_P(a) 	nicInfo->nic_elem[a].cq_tail

#define NWQ_Q_P(a)		nicInfo->nic_elem[a].wq
#define NCQ_Q_P(a)		nicInfo->nic_elem[a].cq

#define CQ_WR_EV_Q		nicInfo->nic_elem[core_id].cq_wr_event_q

#define RCP_EQ			nicInfo->nic_elem[core_id].rcp_eq

#define NF0 0
#define NF1 1
#define NNF 2 //non network function server

/*
#define NOTIFY_WQ_WRITE 0xA

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
*/


//#define RECV_BUF_POOL_SIZE 524288//1048576 		// 1MB of buffer space per core
#define RECV_BUF_POOL_SIZE (nicInfo->recv_buf_pool_size)

#define LAT_ARR_SIZE 6000

#if 0
typedef struct wq_entry {
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

typedef struct cq_entry {
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
	bool SR;    //sense reverse bit
} rmc_wq_t;

typedef struct rmc_cq {
	cq_entry_t q[MAX_NUM_WQ];
	uint32_t tail;
	bool SR;    //sense reverse bit
} rmc_cq_t;
#endif

typedef struct recv_buf_dir {
	bool in_use;
	bool is_head;
	uint32_t len;
	uint32_t use_count; //for dbg
} recv_buf_dir_t;

typedef struct cq_wr_event {
	uint64_t q_cycle;
	cq_entry_t cqe;
	cq_wr_event* next;
} cq_wr_event;

//class cq_wr_event : public GlobAlloc {
//};

typedef struct rcp_event {
	uint64_t q_cycle;
	uint64_t lbuf_addr;
	uint64_t lbuf_data; //may not need
	rcp_event* next;
} rcp_event;

typedef struct z_cacheline {
	uint8_t line_seg[1];
} z_cacheline;

struct nic_element {
	//rmc_wq_t wq;
	//rmc_cq_t cq;
	rmc_wq_t* wq;
	rmc_cq_t* cq;
	uint64_t wq_tail;
	uint64_t cq_head;
	bool wq_valid;
	bool cq_valid;
	bool nwq_SR;
	bool ncq_SR;
	PAD();
	//uint64_t recv_buf[RECV_BUF_POOL_SIZE];
	//z_cacheline recv_buf[RECV_BUF_POOL_SIZE];
	//dbg counter
	int rb_left;
	z_cacheline* recv_buf;
	z_cacheline *lbuf;
	uint64_t * rb_pad;
	//recv_buf_dir_t rb_dir[RECV_BUF_POOL_SIZE];
	recv_buf_dir_t* rb_dir;
	uint32_t rb_iterator;
	uint64_t cq_check_spin_count;
	uint64_t cq_check_inner_loop_count;
	uint64_t cq_check_outer_loop_count;
	PAD();

	cq_wr_event* cq_wr_event_q;
	uint64_t ceq_size; //keep count for load balancing
	rcp_event* rcp_eq;
	lock_t rcp_lock;
	lock_t ceq_lock;
	lock_t rb_lock;
	PAD();

	//uint64_t* service_times;
	uint32_t service_times[10000000]; //as of now, I can't run more than 60k packets :(
	uint64_t st_size;
	//uint64_t st_capa;
	uint32_t cur_service_start_time;
	bool service_in_progress;
	PAD();

	//debugging old appmis vs new appmiss ratio count diff
	uint64_t app_l3_access_flag = 0;

	// per-core
	uint32_t ts_queue[100000000];	//timestamps coming from app calling timestamp() + core request pickup
	uint32_t ts_nic_queue[100000000];	// timestamps recorded in nic functions (injection, answer pickup)
	uint32_t bbl_queue[10000000];	// amount of bbls between injection and completion of a request
	uint32_t phase_nic_queue[10000000];	// bound phase recording per request: injection, answer pickup
	uint32_t phase_queue[10000000];	// bound phase recording per request: core pickup
	int ts_idx = 0, ts_nic_idx = 0, phase_idx = 0, phase_nic_idx = 0, bbl_idx = 0;

	bool packet_pending;
	lock_t packet_pending_lock;
	PAD();
};

typedef struct done_packet_info {
	uint64_t end_cycle;
	uint64_t lbuf_addr;
	uint64_t  tag;
	uint64_t len;
	uint32_t ending_phase;
	done_packet_info* next;
	done_packet_info* prev;
} done_packet_info;


struct glob_nic_elements {
	lock_t nic_lock;
	uint64_t nic_ingress_pid;
	uint64_t nic_egress_pid;
	void* nicCore_ingress;
	void* nicCore_egress;
	//RPCGenerator* RPCGen;
	done_packet_info* done_packet_q_head;
	done_packet_info* done_packet_q_tail;
	lock_t dpq_lock;

	uint64_t rb_set_hist[2048]; //debug
	uint64_t rb_bank_hist[24]; //debug
	uint64_t non_rb_bank_hist[24]; //debug

	uint64_t dpq_size=0;
	uint64_t recv_buf_pool_size;
	//bool  clean_recv;
	uint32_t  clean_recv;

	uint32_t expected_core_count;
	uint32_t registered_core_count;
	bool nic_ingress_proc_on=false;
	bool nic_egress_proc_on;
	bool nic_init_done;
	bool record_nic_access;

	uint32_t expected_non_net_core_count;
	uint32_t registered_non_net_core_count;

	//need to find a way to wire this into existing zsim stats
	uint64_t* latencies;
	uint64_t latencies_size;
	uint64_t latencies_capa;

	//var for histogram processing
	uint32_t hist_interval;
	uint64_t max_latency;
	//debug data structures, to be commented out
	//uint64_t* latencies_list;
	//uint64_t latencies_list_capa;

	uint32_t ceq_delay;
	uint32_t nw_roundtrip_delay;

	uint32_t memtype=1;

	uint32_t inval_read_rb=0; //0: none, 1: inval at read, 2: inval at free_recb_buf

	uint32_t getParentId_policy=0;
	uint32_t pp_policy;
	bool send_in_loop;
	bool zeroCopy = false;
	bool out_of_rbuf=false;
	bool spillover = false;
	bool allow_packet_drop = false;
	bool pd_flag = false;
	uint64_t spillover_count=0;
	uint64_t dropped_packets=0;
	uint32_t num_ddio_ways=2;
	
	bool closed_loop_done=false;
	uint32_t load_balance=0;
	uint32_t forced_packet_size=0;
	uint32_t num_controllers=6;
	uint64_t gm_size=0;

	uint32_t tx2ev_i=0;
	uint32_t tx2ev[10]; //originally 100000000, unused so making it small
	
	lock_t txts_lock;	   // keeping code for DBG/Comparison purpose
	
	uint64_t ** txts_map; //lineaddr, timestamp pair


	//for stat printing
	uint32_t IR_per_phase[100000]; //for plotting IR vs SR
	uint32_t SR_per_phase[100000]; //Service rate
	uint32_t cq_size_per_phase[100000]; 
	uint32_t ceq_size_per_phase[100000]; 
	float 	 mem_bw_sampled[100000];
	int		 remaining_rb[100000];
	uint32_t next_phase_sampling_cycle=0;
	//dbg
	uint32_t lg_clk_slack[100000];
	uint32_t cq_size_cores_per_phase[18][100000];
	uint64_t last_zsim_pahse;

	uint32_t last_phase_sent_packets=0;
	uint32_t last_phase_done_packets=0;
	uint32_t sampling_phase_index=0;
	std::chrono::system_clock::time_point sim_start_time;
	std::chrono::system_clock::time_point sim_end_time;

	//matrixes for mat mult. Malloc here and pass down, because malloc from pin launched app is slow
	uint64_t* matA; 
	uint64_t* matB;
	uint64_t* matC;
	uint32_t mat_N; //lenght of row and column (NxN matrix)
	uint32_t num_mm_cores = 0;
	uint32_t registered_mm_cores = 0;
	lock_t mm_core_lock;

	//EGR cap
	uint64_t egr_interval=0;
	uint64_t next_egr_cycle=0;

	//temp debug counters
	uint64_t free_rb_call_count=0;
	uint64_t process_wq_entry_count=0;
	uint64_t rmc_send_count=0;
	uint64_t deq_dpq_count=0;
	uint64_t enq_dpq_count=0;
	uint64_t conseq_valid_deq_dpq_count=0;
	bool last_deq_dpq_call_valid=false;
	int delta_dpq_size=0;
	int last_dpq_size=0;
	int delta_dpq_sizes[1000];
	uint64_t delta_dpq_index=0;
	lock_t ptag_dbug_lock;

	PAD();
	nic_element nic_elem[MAX_NUM_CORES];

	uint32_t ready_for_inj=0;
	uint32_t first_injection = 0;
	uint64_t ffinst_flag = 0;
	//adding additional elements to this struct below nic_elem causes segfault at gm_calloc for unknown reason
};



typedef struct p_time_card {
	uint64_t issue_cycle;
	uint64_t ptag;
	p_time_card* next;
} p_time_card;

typedef struct timestamp_str {
	uint64_t core_id;
	uint64_t phase;
	uint64_t nic_enq_cycle;
} timestamp;

typedef struct load_gen_mod {
	uint32_t lg_type;
	uint64_t next_cycle;
	uint32_t interval;
	uint32_t burst_count=0;
	uint32_t burst_len=0;
	
	uint32_t arrival_dist=0;
	uint32_t q_depth=1; //for arrival dist==3, sustain q_depth
	uint64_t sum_interval;
	uint32_t prev_cycle;
	uint64_t sent_packets=0;

	RPCGenerator* RPCGen;
	uint64_t last_core;
	uint32_t num_cores;
	uint32_t core_ids[64]; //assume max core count 64
}load_gen_mod;

struct load_generator {
	//int next_cycle;
	//int interval;
	//int prev_cycle;
	uint32_t arrival_dist=0;
	uint32_t num_loadgen;
	load_gen_mod* lgs;
	//dbg
	uint64_t sum_interval;
	uint32_t prev_cycle;
	

	//int message; //may replace this to appropriate type
	bool all_packets_sent=0, all_packets_completed=0;
	uint64_t target_packet_count;
	uint64_t sent_packets;
	//uint64_t last_core;
	uint64_t ptag;
	lock_t ptc_lock;	   // keeping code for DBG/Comparison purpose

	timestamp* tc_map;
	//RPCGenerator* RPCGen;
};


#endif // _NIC_DEFINES_H_



/* don't know how to make a class accessible globally
class load_generator {
public:
	load_generator();
	int get_next_cycle();
	int get_message(); //may replace this to appropriate type
	void tread();

private:
	int next_cycle;
	int message;
};

load_generator::load_generator() {
	next_cycle = 10000;
	message = 0;
}
int load_generator::get_message() {
	return message;
}
int load_generator::get_next_cycle() {
	return next_cycle;
}
void load_generator::tread() {
	next_cycle += 10000;
	message += 1;
}
*/

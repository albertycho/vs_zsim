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

#include <chrono>


#ifndef _NIC_DEFINES_H_
#define _NIC_DEFINES_H_

#define MAX_NUM_CORES 64 //probably will support more

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


#define RECV_BUF_POOL_SIZE 2000
//#define RECV_BUF_POOL_SIZE 32000

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
	uint64_t line_seg[8];
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
	z_cacheline recv_buf[RECV_BUF_POOL_SIZE];
	//uint64_t lbuf[RECV_BUF_POOL_SIZE];
	//z_cacheline lbuf[RECV_BUF_POOL_SIZE];
	z_cacheline *lbuf;
	recv_buf_dir_t rb_dir[RECV_BUF_POOL_SIZE];
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
	uint32_t service_times[65000]; //as of now, I can't run more than 60k packets :(
	uint64_t st_size;
	//uint64_t st_capa;
	uint32_t cur_service_start_time;
	bool service_in_progress;
	PAD();



};

typedef struct done_packet_info {
	uint64_t end_cycle;
	uint64_t lbuf_addr;
	uint64_t  tag;
	
	done_packet_info* next;
	done_packet_info* prev;
} done_packet_info;


struct glob_nic_elements {
	uint64_t nic_ingress_pid;
	uint64_t nic_egress_pid;
	void* nicCore_ingress;
	void* nicCore_egress;
	//RPCGenerator* RPCGen;
	done_packet_info* done_packet_q_head;
	done_packet_info* done_packet_q_tail;
	lock_t dpq_lock;

	uint64_t dpq_size;

	uint64_t packet_injection_rate;
	uint32_t expected_core_count;
	uint32_t registered_core_count;
	bool nic_ingress_proc_on;
	bool nic_egress_proc_on;
	bool nic_init_done;
	bool record_nic_access;

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

	uint32_t pp_policy;

	std::chrono::system_clock::time_point sim_start_time;
	std::chrono::system_clock::time_point sim_end_time;

	PAD();
	nic_element nic_elem[MAX_NUM_CORES];
	//adding additional elements to this struct below nic_elem causes segfault at gm_calloc for unknown reason
};



typedef struct p_time_card {
	uint64_t issue_cycle;
	uint64_t ptag;
	p_time_card* next;
} p_time_card;



struct load_generator {
	int next_cycle;
	int interval;
	int message; //may replace this to appropriate type
	bool all_packets_sent;
	uint64_t target_packet_count;
	//uint64_t sent_packets;
	uint64_t last_core;
	uint64_t ptag;
	p_time_card* ptc_head; // this linked list packet_time_card is not used, 
	lock_t ptc_lock;	   // keeping code for DBG/Comparison purpose
	//std::map<uint64_t, uint64_t> * tc_map;
	std::shared_ptr<std::map<uint64_t, uint64_t>> tc_map;
	RPCGenerator* RPCGen;
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
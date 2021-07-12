#include "galloc.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <string>
#include "pad.h"
#include <unistd.h>
#include "locks.h"


#ifndef _NIC_DEFINES_H_
#define _NIC_DEFINES_H_

#define MAX_NUM_CORES 64 //probably will support more

#define MAX_NUM_WQ 8

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

#define RECV_BUF_POOL_SIZE 16000
//#define RECV_BUF_POOL_SIZE 32000

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
	uint64_t recv_buf[RECV_BUF_POOL_SIZE];
	recv_buf_dir_t rb_dir[RECV_BUF_POOL_SIZE];
	uint64_t lbuf[RECV_BUF_POOL_SIZE];
	
	cq_wr_event* cq_wr_event_q;
	rcp_event* rcp_eq;
	lock_t ceq_lock;
	lock_t rb_lock;
	PAD();

};


struct glob_nic_elements {
	uint64_t nic_pid;
	
	uint64_t packet_injection_rate;
	uint32_t expected_core_count;
	uint32_t registered_core_count;
	bool nic_proc_on;
	bool nic_init_done;
	bool record_nic_access;
	PAD();
	nic_element nic_elem[MAX_NUM_CORES];
	
	//cq_wr_event* cq_wr_event_q[MAX_NUM_CORES];
	//adding additional elements to this struct causes segfault at gm_calloc for unknown reason
};

struct load_generator {
	int next_cycle;
	int message; //may replace this to appropriate type
};


#endif // _NIC_DEFINS_H_



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
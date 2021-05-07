#include <stdint.h>

#define MAX_NUM_WQ 8
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
    //uint32_t SR ;    //sense reverse bit
} rmc_cq_t;

typedef struct {
	uint32_t success, op;
	uint32_t tid;
	uint64_t recv_buf_addr;
} successStruct;

//int register_buffer(void * val, void* field)
//{
////variables: start addr of WQ/CQ
////			 size of WQ/CQ
////can distinguish the type of variable depending on value of rbx?
//	int dummy;
//	asm(
//		"movq %1, %%rbx;"
//		"movq %2, %%rcx;"
//		"xchg %%rbx, %%rbx;"
//		:"=r" (dummy)
//		:"r"(val), "r"(field)
//		:"%rbx","%rcx" //clobbered registers
//	);
//	return 0;
//}

int register_buffer(void * val, void* field);
int reg_wq(rmc_wq_t ** wq);
int reg_cq(rmc_cq_t **cq);

//int reg_wq(rmc_wq_t ** wq){
//	register_buffer((void*) (wq), (void*) 0);
//	return 0;
//}
//
//int reg_cq(rmc_cq_t **cq){
//	register_buffer((void*) (cq), (void*) 1);
//	return 0;
//}

successStruct rmc_check_cq(rmc_wq_t *wq, rmc_cq_t *cq);
int rmc_hw_send(rmc_wq_t *wq, uint32_t ctx_id, void *data_address, uint64_t length, int nid);

void create_wq_entry(uint32_t op, bool SR, uint32_t cid, uint32_t nid,
            uint64_t buf_addr, uint64_t offset, uint64_t length,
            uint64_t wq_entry_addr);
int rmc_hw_recv(rmc_wq_t *wq, uint32_t ctx_id, void *recv_buf, uint64_t length);

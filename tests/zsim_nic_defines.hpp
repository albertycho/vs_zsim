#include <cstddef>
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


//defining WQ and CQ structs

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


int register_buffer(void * val, void* field);
int reg_wq(rmc_wq_t ** wq);
int reg_cq(rmc_cq_t **cq);


successStruct rmc_check_cq(rmc_wq_t *wq, rmc_cq_t *cq);
int rmc_hw_send(rmc_wq_t *wq, uint32_t ctx_id, void *data_address, uint64_t length, int nid);

void create_wq_entry(uint32_t op, bool SR, uint32_t cid, uint32_t nid,
            uint64_t buf_addr, uint64_t offset, uint64_t length,
            uint64_t wq_entry_addr);
int rmc_hw_recv(rmc_wq_t *wq, uint32_t ctx_id, void *recv_buf, uint64_t length);


///////////MICA HERD defines////////////
/* Fixed-size 16 byte keys */

#define MICA_OP_GET 111
#define MICA_OP_PUT 112
#define HERD_MICA_OFFSET 10
#define HERD_OP_PUT (MICA_OP_PUT + HERD_MICA_OFFSET)
#define HERD_OP_GET (MICA_OP_GET + HERD_MICA_OFFSET)
#define HERD_VALUE_SIZE 32
#define MICA_MAX_VALUE \
  (64 - (sizeof(struct mica_key) + sizeof(uint8_t) + sizeof(uint8_t)))


struct mica_key {
    unsigned long long __unused : 64;
    unsigned int bkt : 32;
    unsigned int server : 16;
    unsigned int tag : 16;
};

struct mica_op {
    struct mica_key key; /* This must be the 1st field and 16B aligned */
    uint8_t opcode;
    uint8_t val_len;
    uint8_t value[MICA_MAX_VALUE];
};

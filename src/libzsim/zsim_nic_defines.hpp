#include <stdint.h>
//#include "RMCdefines.h"
//#ifndef H_RMC_DEFINES
//#define H_RMC_DEFINES
//#endif
#include <pthread.h>
#include <stdbool.h> /* Msutherl: Std c11 */

//#define ZSIM

#ifdef DEBUG
#define DLog(M, ...) fprintf(stdout, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define DLog(M, ...)
#endif

#define MAX_NUM_WQ 128
#define NOTIFY_WQ_WRITE 0xA

/* op types */
#define RMC_READ                1
#define RMC_WRITE               2
#define RMC_RMW                 3
#define RMC_SABRE	              4
#define RMC_RECV                5
#define RMC_MSG_NACK            6

/* Reorganize SEND types to use masks:
 * - The field of rmc_op is 6b wide, so SENDs use 1<<4 to keep all of the values
 *   beneath RMC_INVAL (42)
 */
#define SEND_OP_SHIFT           4
#define PAIRED_SEND_SHIFT       0
#define INCOMING_SEND_SHIFT     1
#define INCOMING_RESP_SHIFT     2
#define RMC_SEND                (1<<SEND_OP_SHIFT)
#define RMC_PAIRED_SEND         (1<<SEND_OP_SHIFT) | (1<<PAIRED_SEND_SHIFT)
#define RMC_INCOMING_SEND       (1<<SEND_OP_SHIFT) | (1<<INCOMING_SEND_SHIFT)
#define RMC_INCOMING_RESP       (1<<SEND_OP_SHIFT) | (1<<INCOMING_RESP_SHIFT)
#define RMC_INVAL               42
#define PADBYTES                60



#define MAX_NUM_SHARED_CQ 10*MAX_NUM_WQ  


#define DEFAULT_CTX_VAL 0 //123

#define SR_CTX_ID 7   //the context ID used for messaging
/*
//breakpoint IDs
#define WQUEUE              1
#define CQUEUE              2
#define BUFFER              3
#define PTENTRY             4
#define NEWWQENTRY          5
#define WQENTRYDONE         6
#define ALL_SET             7
#define TID_COUNTERS        8
#define CONTEXT             9
#define CONTEXTMAP          10
#define WQHEAD              11
#define WQTAIL              12
#define CQHEAD              13
#define CQTAIL              14
#define SIM_BREAK           15
#define NETPIPE_START       16
#define NETPIPE_END         17
#define RMC_DEBUG_BP        18
#define BENCHMARK_END       19
#define BUFFER_SIZE         20
#define CONTEXT_SIZE        21
#define NEWWQENTRY_START    22
#define NEW_SABRE           23
#define SABRE_SUCCESS       24
#define SABRE_ABORT         25
#define OBJECT_WRITE        26	//used by writers in SABRe experiments, to count number of object writes
#define LOCK_SPINNING	    27
#define CS_START            28
//29 and 30 reserved for SABRes measurement breakpoints (legacy)
#define SEND_BUF            31  //base address of SEND_BUF
#define RECV_BUF            32  //base address of RECV_BUF
#define MSG_BUF_ENTRY_SIZE  33  //the size of each send/recv buffers
#define NUM_NODES           34  //the number of nodes participating in this messaging domain
#define MSG_BUF_ENTRY_COUNT 35  //the number of send/recv entries per node pair
#define SENDREQRECEIVED     36
#define SENDENQUEUED        37
#define SHARED_CQUEUE       38
#define MCS_BREAKPOINT      39
#define MICA_BREAKPOINT     40
#define MEASUREMENT	        99
#define QFLEX_READY_FOR_TIMING 999

#define CONTROL_FLOW    101
*/
//PT parameters for Page Walks
#define PT_I 3
#define PT_J 10
#define PT_K 4


// TODO: configure this thing @ compile time. cmake or autoconf?
#define CACHE_BLOCK_BITS 6
#ifdef QFLEX 
    #define EMULATOR_SW_PAGE_SIZE 4096
    #define EMULATOR_SW_PAGE_BITS 0xfffffffffffff000
#else
    #if defined(FLEXUS) || defined(ZSIM) // simflex
        #define EMULATOR_SW_PAGE_SIZE 8192
        #define EMULATOR_SW_PAGE_BITS 0xffffffffffffe000
    #else // linux or vm
        #define EMULATOR_SW_PAGE_SIZE 4096
        #define EMULATOR_SW_PAGE_BITS 0xfffffffffffff000
    #endif
#endif

//WQ entry field offsets - for non-compacted version
#define WQ_TYPE_OFFSET          0
#define WQ_NID_OFFSET           8
#define WQ_TID_OFFSET           16
#define WQ_CID_OFFSET           24
#define WQ_OFFSET_OFFSET        32
#define WQ_BUF_LENGTH_OFFSET    48
#define WQ_BUF_ADDRESS_OFFSET   64




#define RECV_ID   255     //the tid value that indicates the reception of a RECV msg in the CQ

////////////////////////// KAL DEFINES/////////////////////////////////
/*
#define KAL_REG_WQ      1
#define KAL_UNREG_WQ    6
#define KAL_REG_CQ      5
#define KAL_REG_CTX     3
#define KAL_PIN_BUFF    4
#define KAL_PIN         14
*/
#define RMC_KILL        10

#define CACHE_BLOCK_SIZE      64
#define BBUFF_SIZE      256 //credits for outstanding msgs (in # of cache blocks) when messaging
#define PL_SIZE         60 //payload size for software-only messaging

//for messaging (software-only send/recv)
#if defined FLEXUS || defined QFLEX || defined ZSIM
  #define RW_THR      100000 //256//16//16384//4096//16 //16384//256 //8192 //256
#else
  #define RW_THR      1024//16//16384//4096//16 //16384//256 //8192 //256
#endif

//defining WQ and CQ structs

typedef struct wq_entry{
	//first double-word (8 bytes)
	uint32_t op;        //up to 64 soNUMA ops
	volatile bool SR;        //sense reverse bit
	volatile bool valid;    //set with a new WQ entry, unset when entry completed. Required for pipelining async ops
	uint64_t buf_addr;
	uint32_t cid;
	uint64_t nid;
	//second double-word (8 bytes)
	uint64_t offset;
	uint64_t length;
} wq_entry_t;

typedef struct cq_entry{
    volatile bool SR;     //sense reverse bit
	volatile bool valid;
    volatile uint32_t success; /* Success bit/type */
    //volatile unsigned int tid; /* Uses tid to specify incoming send id and qp */
    volatile uint32_t tid; /* Uses tid to specify incoming send id and qp */
    volatile uint64_t recv_buf_addr; /* Incoming recv buf block address (42 bits) */
} cq_entry_t;

typedef struct rmc_wq {
    wq_entry_t q[MAX_NUM_WQ];
    uint32_t head;
    volatile bool SR ;    //sense reverse bit
} rmc_wq_t;

typedef struct rmc_cq {
    cq_entry_t q[MAX_NUM_WQ];
    uint32_t tail;
    volatile bool SR ;    //sense reverse bit
    //uint32_t SR ;    //sense reverse bit
} rmc_cq_t;

typedef struct {
	uint32_t success, op;
	uint32_t tid;
	uint64_t recv_buf_addr;
} successStruct;

typedef struct rpcArgument {
    void* pointerToAppData; // cast me
    bool is_nack;
} rpcArg_t;
typedef void (async_handler)(uint8_t tid, wq_entry_t *head, void *owner);
typedef void (receiveCallback)(uint8_t* rawRecvBufferPtr, rpcArg_t* argPointer);

void register_buffer(void * val, void* field);
int reg_wq(rmc_wq_t ** wq);
int reg_cq(rmc_cq_t **cq);


successStruct rmc_check_cq(rmc_wq_t *wq, rmc_cq_t *cq);
int rmc_hw_send(rmc_wq_t *wq, uint32_t ctx_id, void *data_address, uint64_t length, uint64_t nid);

void create_wq_entry(uint32_t op, bool SR, uint32_t cid, uint64_t nid,
            uint64_t buf_addr, uint64_t offset, uint64_t length,
            uint64_t wq_entry_addr);
int rmc_hw_recv(rmc_wq_t *wq, uint32_t ctx_id, void *recv_buf, uint64_t length);


//stuff coming from RMCdefines.h (sonuma)

//send req passes pointer to a send_buf_entry
//RMC reads data from data_address and sends it to dest
//When RECV is posted by remote node, RRPP frees send_buf_entry by writing to SR
typedef struct send_buf_entry{  //total 32B
  volatile uint8_t valid;  //only one bit used
  uint8_t padding[15];
  uint64_t data_size;
  uint8_t *data_address;
//  wq_entry_t *wq_entry;
} send_buf_entry_t;

//One entry per dest node to keep track of used send buffers
typedef struct send_buf_management{
  //pthread_mutex_t mutex;
  //uint8_t padding[36];
  uint16_t head, tail;    //tail unused
  volatile uint8_t lock;
  uint8_t padding[59];
} send_buf_management_t;    //a cache block each, to avoid false sharing

//New structure (protocol v2.3) to associate a context with a send/recv buffer
typedef struct context{
  uint16_t ctx_id;
  uint8_t *ctx_base_addr;
  //uint64_t ctx_size;
  send_buf_entry_t *send_buf_addr;
  void *recv_buf_addr;
  uint16_t num_nodes;
  uint64_t msg_entry_size;
  uint64_t msgs_per_dest;
  send_buf_management_t *buf_management;
} ctx_entry_t;

int kal_reg_send_recv_bufs(int kfd,ctx_entry_t *ctx_entry, send_buf_entry_t **send_buf_base,uint64_t send_buf_entries, uint64_t msg_entry_size, uint16_t num_nodes, uint32_t **recv_buf_base,send_buf_management_t **send_ctrl);


// from sonuma.h

/**
 * This func registers context buffer with KAL or Flexus.
 * Warning: the func pins the memory to avoid swapping to
 *          the disk (only for Flexus); allocation is done within an app
 */
int kal_reg_ctx(int kfd, int ctx_id, uint8_t **ctx_ptr, uint32_t num_pages);
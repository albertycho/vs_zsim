#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include "../src/libzsim/zsim_nic_defines.hpp"
#include "qp_test_thread_ver.hpp"
//#include "zsim_nic_defines.hpp"

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


using namespace std;

void* qp_test(void* inarg) {

	thread_params* casted_inarg = (thread_params*) inarg;
	int core_id = 2;

	core_id = casted_inarg->core_id;

	//if(argc>1){
	//	core_id=atoi(argv[1]);
	//}
	//else{
	//	std::cout<<"qp_test: no core_id specified"<<std::endl;
	//}

	pid_t pid = getpid();

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	
	CPU_SET(core_id, &cpuset);

	int setresult = sched_setaffinity(pid, sizeof(cpuset), &cpuset);

	if(setresult!=0){
		std::cout<<"qp_test - sched_setaffinity failed"<<std::endl;
	}

	std::cout << "qp_test - core_id = " << core_id << std::endl;

	rmc_wq_t * wq;
	rmc_cq_t * cq;

	uint32_t * lbuf_base;
	uint32_t * lbuf_ptr;
	//register lbuf_base
	uint64_t msgs_per_node = 256;
	uint64_t msg_size = 512;
	uint64_t buf_size = msgs_per_node * msg_size;

	register_buffer((void*)buf_size, (void*)3);
	register_buffer((void*) (&lbuf_base), (void*) 2);
	register_buffer((void*) (&wq), (void*) 0);
	register_buffer((void*) (&cq), (void*) 1);


	std::cout << "qp_test - QP register done " << core_id << std::endl;
	uint64_t send_count=0;
	uint64_t send_serviced=0;


	int ctx_id = 0;
	int msg_entry_size = 1;

	uint64_t sum = 0;
	uint64_t put_req_count = 0;

	bool* client_done;
	register_buffer((void*)(&client_done), (void*)0xD);

	std::cout << "qp_test - before entering while loop " << core_id << std::endl;

	while (!(*client_done))
	//while (send_count <= 4000)
	{
		std::cout << "qp_test - inside while loop " << core_id << std::endl;
		successStruct recv_completion;
		do{
			std::cout << "calling rmc check" << std::endl;
			recv_completion = rmc_check_cq(wq,cq);
			//debug print
			//NOTE - adding this dbg print causes hang at the end of test.. why?
			/*
			if (recv_completion.op != (RMC_INCOMING_SEND)) {
				std::cout << "APP recvd REQUEST COPMPLETE, msg: " << std::hex << *(uint64_t*)(recv_completion.recv_buf_addr)<<"success:"<<recv_completion.op << std::endl;
			}
			*/

		} while (recv_completion.op != (RMC_INCOMING_SEND));

		std::cout<<"APP - recv_completion.op="<<recv_completion.op<<std::endl;

		send_serviced++;
		
		//test_prints
		if (recv_completion.recv_buf_addr & 0xffff000000 != 0xabba000000) {
			std::cout << "incorrect recv_buf_addr" << std::endl;
		}
		
		std::cout << "APP: recvd incoming msg.              recv_count:"<<std::dec << send_serviced << ", rbuf_addr:" <<std::hex<< recv_completion.recv_buf_addr << ", rbuf_val:" << *(uint64_t*)(recv_completion.recv_buf_addr) << std::endl;
		sum += *(uint64_t*)(recv_completion.recv_buf_addr);

		mica_op* mp = (mica_op*)recv_completion.recv_buf_addr;
		if (mp->opcode == HERD_OP_PUT) {
			put_req_count++;
		}
		if (mp->opcode != HERD_OP_PUT && mp->opcode != HERD_OP_GET) {
			std::cout << "INCORRECT HERD OPCODE" << std::endl;
		}
		//san check code
		
		//else {
		//	if (mp->opcode == HERD_OP_PUT) std::cout << "HERD_OP_PUT, send serviced:"<<send_serviced << std::endl;
		//	if (mp->opcode == HERD_OP_GET) std::cout << "HERD_OP_GE, Tsend serviced:"<<send_serviced << std::endl;
		//}
		
		uint32_t target_node = recv_completion.tid;

		//calcualte lbuf_ptr address

		//std::cout<<"TOY APP: dbgprint 134"<<std::endl;

		int send_ret;
		//FIXME: figure out what to do with msg_entry_size
		uint64_t msg_entry_size=1;
		//std::cout<<"TOY APP: dbgprint 139"<<std::endl;

		//lbuf_ptr=lbuf_base+(send_count % 16);
		lbuf_ptr = lbuf_base + ((send_count % msgs_per_node) * msg_size);

		uint64_t offset = ((send_count % msgs_per_node) * msg_size);
		
		std::cout << "TOY APP: count = " << send_count << ", offset: " << offset << ", lbuf_Ptr = " << lbuf_ptr << ", lbuf_base = "<<lbuf_base << std::endl;
		
		*lbuf_ptr=0xabcd00+send_count;
		//dbgprint
		std::cout << "before rmc_hw_send in toy app" << std::endl;
		
		do{
			send_ret=rmc_hw_send(wq, ctx_id, lbuf_ptr, msg_entry_size, target_node);
		} while (send_ret);

		std::cout << "after rmc_hw_send in toy app, before rmc_hw_recv" << std::endl;

		send_count++;
		std::cout<<"APP: send_count="<<send_count<<std::endl;
		rmc_hw_recv(wq, ctx_id, (void*) recv_completion.recv_buf_addr, msg_entry_size);
		std::cout << "after rmc_hw_recv" << std::endl;
	}

	uint64_t put_req_ratio = put_req_count * 100 / send_count;

	std::cout << "APP: SUM = " << std::dec << sum << std::endl;
	std::cout << "APP: serviced = " << std::dec << send_serviced << std::endl;
	std::cout << "APP - PUT REQ Ratio: " << std::dec << put_req_ratio << "%" << std::endl;

	register_buffer((void*) 0, (void*) 0xdead);
	std::cout<<"APP - terminating"<<std::endl;
    return NULL;
}



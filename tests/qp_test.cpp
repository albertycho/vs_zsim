#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include "../src/libzsim/zsim_nic_defines.hpp"
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



using namespace std;

int main(int argc, char* argv[]) {

	int core_id = 2;

	if(argc>1){
		core_id=atoi(argv[1]);
	}
	else{
		std::cout<<"qp_test: no core_id specified"<<std::endl;
	}

	pid_t pid = getpid();

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	
	CPU_SET(core_id, &cpuset);

	int setresult = sched_setaffinity(pid, sizeof(cpuset), &cpuset);

	if(setresult!=0){
		std::cout<<"qp_test - sched_setaffinity failed"<<std::endl;
	}

	rmc_wq_t * wq;
	rmc_cq_t * cq;

	uint32_t * lbuf_base;
	uint32_t * lbuf_ptr;
	//register lbuf_base

	register_buffer((void*)1024, (void*) 3);
	register_buffer((void*) (&lbuf_base), (void*) 2);
	register_buffer((void*) (&wq), (void*) 0);
	register_buffer((void*) (&cq), (void*) 1);

	uint64_t send_count=0;
	uint64_t send_serviced=0;


	int ctx_id = 0;
	int msg_entry_size = 1;

	uint64_t sum = 0;
	uint64_t put_req_count = 0;

	//while(send_count<=10000)
	while (send_count <= 4000)
	{
		successStruct recv_completion;
		do{
			recv_completion = rmc_check_cq(wq,cq);
			//debug print
			//NOTE - adding this dbg print causes hang at the end of test.. why?
			/*
			if (recv_completion.op != (RMC_INCOMING_SEND)) {
				std::cout << "APP recvd REQUEST COPMPLETE, msg: " << std::hex << *(uint64_t*)(recv_completion.recv_buf_addr)<<"success:"<<recv_completion.op << std::endl;
			}
			*/

		} while (recv_completion.op != (RMC_INCOMING_SEND));

		//std::cout<<"APP - recv_completion.op="<<recv_completion.op<<std::endl;

		send_serviced++;
		
		//test_prints
		if (recv_completion.recv_buf_addr & 0xffff000000 != 0xabba000000) {
			std::cout << "incorrect recv_buf_addr" << std::endl;
		}
		
		//std::cout << "APP: recvd incoming msg.              recv_count:"<<std::dec << send_serviced << ", rbuf_addr:" <<std::hex<< recv_completion.recv_buf_addr << ", rbuf_val:" << *(uint64_t*)(recv_completion.recv_buf_addr) << std::endl;
		sum += *(uint64_t*)(recv_completion.recv_buf_addr);

		mica_op* mp = (mica_op*)recv_completion.recv_buf_addr;
		if (mp->opcode == HERD_OP_PUT) {
			put_req_count++;
		}
		if (mp->opcode != HERD_OP_PUT && mp->opcode != HERD_OP_GET) {
			std::cout << "INCORRECT HERD OPCODE" << std::endl;
		}
		//san check code
		
		else {
			if (mp->opcode == HERD_OP_PUT) std::cout << "HERD_OP_PUT, send serviced:"<<send_serviced << std::endl;
			if (mp->opcode == HERD_OP_GET) std::cout << "HERD_OP_GE, Tsend serviced:"<<send_serviced << std::endl;
		}
		
		uint32_t target_node = recv_completion.tid;

		//calcualte lbuf_ptr address

		int send_ret;
		//FIXME: figure out what to do with msg_entry_size
		uint64_t msg_entry_size=1;
		lbuf_ptr=lbuf_base+send_count;
		//std::cout<<"APP: lbuf_Ptr="<<lbuf_ptr<<std::endl;
		*lbuf_ptr=0xabcd00+send_count;
		do{
			send_ret=rmc_hw_send(wq, ctx_id, lbuf_ptr, msg_entry_size, target_node);
		} while (send_ret);
		send_count++;
		//std::cout<<"APP: send_count="<<send_count<<std::endl;
		rmc_hw_recv(wq, ctx_id, (void*) recv_completion.recv_buf_addr, msg_entry_size);
	}

	uint64_t put_req_ratio = put_req_count * 100 / send_count;

	std::cout << "APP - SUM = " << std::dec << sum << std::endl;
	std::cout << "APP - PUT REQ Ratio: " << std::dec << put_req_ratio << "%" << std::endl;

	register_buffer((void*) 0, (void*) 0xdead);
	std::cout<<"APP - terminating"<<std::endl;
    return 0;
}



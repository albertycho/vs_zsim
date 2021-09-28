#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include "zsim_nic_defines.hpp"

using namespace std;

int main() {

	//pid_t pid = getpid();

	//cpu_set_t cpuset;
	//CPU_ZERO(&cpuset);
	//std::cout<<"calling getaffinity"<<std::endl;
	////int getaffinity_result = pthread_getaffinity_np(pid, sizeof(cpuset), &cpuset);
	//int getaffinity_result = sched_getaffinity(pid, sizeof(cpuset), &cpuset);

	//if(getaffinity_result!=0) {
	//	std::cout<<"getaffinity_result failed: "<<getaffinity_result<<std::endl;
	//}
	//
	//for(int i=0; i<64;i++){
	//	if(CPU_ISSET(i, &cpuset)){
	//		std::cout<<"cpu "<<i<<" is set"<<std::endl;
	//	}
	//}

	rmc_wq_t * wq;
	rmc_cq_t * cq;

	uint32_t * lbuf_base;
	uint32_t * lbuf_ptr;
	//register lbuf_base

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
		/*
		else {
			if (mp->opcode == HERD_OP_PUT) std::cout << "HERD_OP_PUT" << std::endl;
			if (mp->opcode == HERD_OP_GET) std::cout << "HERD_OP_GET" << std::endl;
		}
		*/
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



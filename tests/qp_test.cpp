#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include "zsim_nic_defines.hpp"

using namespace std;

int main() {


	rmc_wq_t * wq;
	rmc_cq_t * cq;

	uint32_t * lbuf_base;
	uint32_t * lbuf_ptr;
	//register lbuf_base

	register_buffer((void*) (&lbuf_base), (void*) 2);
	register_buffer((void*) (&wq), (void*) 0);
	register_buffer((void*) (&cq), (void*) 1);

	//std::cout<<"APP: wq="<<std::hex<<wq<<std::endl;

	uint64_t send_count=0;
	uint64_t send_serviced=0;


	int ctx_id = 0;
	int msg_entry_size = 1;
	//////////////////////////////////////////////////
	//////// CHECK ONLY RGP&RCP - BEGIN///////////////
	//////////////////////////////////////////////////
	while (send_serviced <= 32) {

		int send_ret;
		//FIXME: figure out what to do with msg_entry_size
		uint64_t msg_entry_size = 1;
		lbuf_ptr = lbuf_base + send_count;
		//std::cout<<"APP: lbuf_Ptr="<<lbuf_ptr<<std::endl;
		*lbuf_ptr = 0xabcd0 + send_count;
		do {
			send_ret = rmc_hw_send(wq, ctx_id, lbuf_ptr, msg_entry_size, 1);
		} while (send_ret);
		send_count++;

		successStruct recv_completion;
		do {
			recv_completion = rmc_check_cq(wq, cq);
		} while (recv_completion.op != (RMC_INCOMING_RESP));
		std::cout << "APP:cq_resp:" << std::hex << (*(recv_completion.recv_buf_addr));

		send_serviced++;
	}

	//////////////////////////////////////////////////
	//////// CHECK ONLY RGP&RCP - END/////////////////
	//////////////////////////////////////////////////

	//////////////////////////////////////////////////
	//////// CHECK ONLY RRPP - BEGIN//////////////////
	//////////////////////////////////////////////////
	while (send_serviced <= 32)
	{
		successStruct recv_completion;
		do {
			recv_completion = rmc_check_cq(wq, cq);
		} while (recv_completion.op != (RMC_INCOMING_SEND));

		//std::cout<<"APP - recv_completion.op="<<recv_completion.op<<std::endl;
		std::cout << "APP: recvd incoming msg.              recv_count:" << std::dec << send_serviced << ", rbuf_addr:" << std::hex << recv_completion.recv_buf_addr << ", rbuf_val:" << *(uint32_t*)(recv_completion.recv_buf_addr) << std::endl;

		send_serviced++;
		rmc_hw_recv(wq, ctx_id, (void*)recv_completion.recv_buf_addr, msg_entry_size);
	}


	return 0;

	//////////////////////////////////////////////////
	//////// CHECK ONLY RRPP - END////////////////////
	//////////////////////////////////////////////////

	while(send_count<=32)
	{
		successStruct recv_completion;
		do{
			recv_completion = rmc_check_cq(wq,cq);
		} while (recv_completion.op != (RMC_INCOMING_SEND));

		//std::cout<<"APP - recv_completion.op="<<recv_completion.op<<std::endl;

		send_serviced++;
		
		//test_prints
		std::cout << "APP: recvd incoming msg.              recv_count:"<<std::dec << send_serviced << ", rbuf_addr:" <<std::hex<< recv_completion.recv_buf_addr << ", rbuf_val:" << *(uint32_t*)(recv_completion.recv_buf_addr) << std::endl;
		uint32_t target_node = recv_completion.tid;

		//calcualte lbuf_ptr address
		int ctx_id=0;
		int send_ret;
		//FIXME: figure out what to do with msg_entry_size
		uint64_t msg_entry_size=1;
		lbuf_ptr=lbuf_base+send_count;
		//std::cout<<"APP: lbuf_Ptr="<<lbuf_ptr<<std::endl;
		*lbuf_ptr=0xabcd0+send_count;
		do{
			send_ret=rmc_hw_send(wq, ctx_id, lbuf_ptr, msg_entry_size, target_node);
		} while (send_ret);
		send_count++;
		//std::cout<<"APP: send_count="<<send_count<<std::endl;
		rmc_hw_recv(wq, ctx_id, (void*) recv_completion.recv_buf_addr, msg_entry_size);
	}

	register_buffer((void*) 0, (void*) 0xdead);
	std::cout<<"APP - terminating"<<std::endl;
    return 0;
}

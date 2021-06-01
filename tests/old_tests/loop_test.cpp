#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include "zsim_nic_defines.hpp"

using namespace std;

int main() {

	rmc_cq_t * cq;

	register_buffer((void*) (&cq), (void*) 1);

	//std::cout<<"APP: wq="<<std::hex<<wq<<std::endl;

	uint64_t count = 0;
	std::cout << "APP: before while loop" << std::endl;
	cq->q[0].success = 3;
	
	
	//while(send_count<=32)
	while (cq->q[0].valid == false) {
		count++;
	}
	cq->q[0].tid = 3;

	std::cout << "APP: count=" << count << std::endl;
	//test how long bbl will go

	uint64_t cq_rbuf_addr = cq->q[0].recv_buf_addr;
	uint64_t tid = cq->q[0].tid;
	count += tid;
	count += cq_rbuf_addr;
	count = count + count;

	register_buffer((void*) 0, (void*) 0xdead);
	std::cout<<"APP - terminating"<<std::endl;
    return 0;
}

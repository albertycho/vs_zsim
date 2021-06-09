#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include "zsim_nic_defines.hpp"

using namespace std;

int main() {

	/*
	rmc_wq_t * wq;
	rmc_cq_t * cq;

	uint32_t * lbuf_base;
	uint32_t * lbuf_ptr;
	//register lbuf_base

	register_buffer((void*) (&lbuf_base), (void*) 2);
	register_buffer((void*) (&wq), (void*) 0);
	register_buffer((void*) (&cq), (void*) 1);

	
	//std::cout<<"APP: wq="<<std::hex<<wq<<std::endl;

	*/

	bool* nic_proc_on;

	register_buffer((void*)(&nic_proc_on), (void*)0xB);

	std::cout << "nic_proc_on addr: " << std::hex << nic_proc_on << std::endl;

	int dummy;
	while ((*nic_proc_on)) {
		dummy = 0;
	}



	register_buffer((void*) 0, (void*) 0xdead);
	std::cout<<"nic proxy app - terminating"<<std::endl;
    return 0;
}

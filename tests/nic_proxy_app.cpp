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

	bool* zsim_done;

	register_buffer((void*)(&zsim_done), (void*)0xB);

	int dummy;
	while (!(*zsim_done)) {
		dummy = 0;
	}



	register_buffer((void*) 0, (void*) 0xdead);
	std::cout<<"nic proxy app - terminating"<<std::endl;
    return 0;
}

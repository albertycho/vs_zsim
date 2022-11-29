#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include "zsim_nic_defines.hpp"
#include <ctime>
#include <chrono>

//#define ARR_SIZE 1000000
#define ARR_SIZE 512
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

	auto start = std::chrono::system_clock::now();

	uint64_t long_array[12];

	for (int i = 0; i < 12; i++) {
		long_array[i] = i;
	}
	std::cout << "APP before reading rbuf_addr" << std::endl;
	uint64_t * rbuf = (uint64_t *) (cq->q[0].recv_buf_addr);

	std::cout << "rbuf addr:" << std::hex << rbuf << std::endl;

	//uint64_t rbuf_val = *(rbuf);

	//std::cout << "APP: rbuf_val = " << std::hex << rbuf_val;



	auto end = std::chrono::system_clock::now();
	std::chrono::duration<double> elapsed_seconds = end - start;
	std::cout << "elapsed time: " << std::dec << elapsed_seconds.count() << std::endl;
	register_buffer((void*)0, (void*)0xdead);

	return 0;

}

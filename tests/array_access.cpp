#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include "zsim_nic_defines.hpp"
#include <ctime>
#include <chrono>

#define ARR_SIZE 1000000
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

	uint64_t long_array[ARR_SIZE];

	for (int i = 0; i < ARR_SIZE; i++) {
		long_array[i] = i;
	}

	uint64_t sum = 0;
	for (int j = 0; j < 100; j++) {
		for (int i = 0; i < ARR_SIZE; i++) {
			sum += long_array[i];
		}
	}
	std::cout << "sum=" << sum << std::endl;

	auto end = std::chrono::system_clock::now();
	std::chrono::duration<double> elapsed_seconds = end - start;
	std::cout << "elapsed time: " << elapsed_seconds.count() << std::endl;

	return 0;

}

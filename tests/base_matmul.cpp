#include <iostream>
#include <fstream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include "memhog_mt.hpp"
//#include "zsim_nic_defines.hpp"
#include "../src/libzsim/zsim_nic_defines.hpp"                                  
#include <sys/syscall.h>                                                        
#include <getopt.h>       

using namespace std;

//////////////////////////// CODE START ///////////////////

uint64_t get_sqrt_uint(uint64_t sqval){
	
	for(uint64_t i=1;i<512;i++){
		if(i*i > sqval){
			return i;
		}
	}

	std::cout<<"matmul: given ws size is larger than 32MB, which is excessive. Going with 512*512 matrix"<<std::endl;
	return 512;
}

void* matmul_thread(void* inarg) {

	thread_params* casted_inarg = (thread_params*) inarg;
	int core_id = 3;
	uint64_t ws_size = 33554432;
	core_id = casted_inarg->core_id;
	ws_size = casted_inarg->ws_size;

	pid_t pid = getpid();

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);

	CPU_SET(core_id, &cpuset);

	int setresult = sched_setaffinity(pid, sizeof(cpuset), &cpuset);

	if(setresult!=0){
		std::cout<<"matmul - sched_setaffinity failed"<<std::endl;
	}

	uint64_t mlen = ws_size / sizeof(uint64_t);
	mlen = mlen / 2; //2 matrices (+ mult but less frequently accessed)
	mlen = get_sqrt_uint(mlen);

	std::cout << "matmul - core_id = " << core_id<<", mlen = "<<mlen<<", ws size = "<<(mlen*mlen*64) << std::endl;
	
	bool* zsim_done;

	register_buffer((void*)(&zsim_done), (void*)0x18);

	/////do matmul

	//uint64_t ** arrA = malloc(sizeof(uint64_t)*mlen*mlen);
	//uint64_t ** arrB = malloc(sizeof(uint64_t)*mlen*mlen);
	//uint64_t ** mul = malloc(sizeof(uint64_t)*mlen*mlen);
	
	uint64_t arrA[512][512];
	uint64_t arrB[512][512];
	uint64_t mul[512][512];
	uint64_t dummy=0;
	
	while (!(*zsim_done)) {
		int i,j,k;
		for(i=0;i<mlen;i++){
			for(j=0;j<mlen;j++){
				for(k=0;k<mlen;k++){
					mul[i][j] += arrA[i][k]*arrB[k][j];
					dummy++;
					if(*zsim_done){
						break;
					}
				}
				if(*zsim_done){
					break;
				}
			}
			if(*zsim_done){
				break;
			}
		}
	}

	//print random element from mul - avoid possible unwatned optimization?
	dummy=dummy % (mlen*mlen);
	uint64_t di=dummy / mlen;
	uint64_t dj=dummy % mlen;
	uint64_t rdp = mul[di][dj];
	std::cout<<"matmult at core "<<core_id<<" terminating, dummy="<<dummy<<", rdp="<<rdp<<std::endl;


	return 0;
}

#include <iostream>
#include <fstream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include "matmul.hpp"
//#include "zsim_nic_defines.hpp"
#include "../src/libzsim/zsim_nic_defines.hpp"                                  
#include <sys/syscall.h>                                                        
#include <getopt.h>       

using namespace std;


//////////////////////////// CODE START ///////////////////
#define MIN(a, b) ( (a) < (b) ? (a) : (b) )
typedef double T;
typedef uint64_t ITYPE;


void tiled_mm(T* A, T* B, T* C, ITYPE M, ITYPE N, ITYPE K, bool * zsim_done)
{

	ITYPE Ti = 128;
	ITYPE Tj = 128;

	for (ITYPE ii = 0; ii < M; ii += Ti) {          // Row panel loop
		for (ITYPE jj = 0; jj < N; jj += Tj) {      // Tile loop

			// Per tile matrix multiplication loop
			for (ITYPE i = ii; i < (ITYPE)MIN((ii + Ti), M); i++) {

				for (ITYPE j = jj; j < MIN((jj + Tj), N); j++) {

					for (ITYPE k = 0; k < K; k++) {

						C[i * K + k] += A[i * N + j] * B[j * K + k];
						if (*zsim_done) {
							break;
						}

					}
					if (*zsim_done) {
						break;
					}
				}
				if (*zsim_done) {
					break;
				}
			}
			if (*zsim_done) {
				break;
			}
		}
		if (*zsim_done) {
			break;
		}
	}

}



void* matmul_thread(void* inarg) {

	thread_params* casted_inarg = (thread_params*) inarg;
	int core_id = 3;
	uint64_t mlen= 640;
	core_id = casted_inarg->core_id;
	mlen = casted_inarg->mlen;

	pid_t pid = getpid();

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);

	CPU_SET(core_id, &cpuset);

	int setresult = sched_setaffinity(pid, sizeof(cpuset), &cpuset);

	if(setresult!=0){
		std::cout<<"matmul - sched_setaffinity failed"<<std::endl;
	}


	//std::cout << "matmul - core_id = " << core_id<<", mlen = "<<mlen<<", ws size = "<<(mlen*mlen*8) << std::endl;
	printf("matmul: cid = %d, mlen = %d, ws_size = %d\n", core_id, mlen, mlen * mlen * 2 * 8);

	bool* zsim_done;

	register_buffer((void*)(&zsim_done), (void*)0x18);

	/////do matmul

	T* A;
	T* B;
	T* C;
	register_buffer((void*)(&A), (void*)0x30);
	register_buffer((void*)(&B), (void*)0x31);
	register_buffer((void*)(&C), (void*)0x32);

	//connect check
	if (A[10] == 0xc0ffee) {
		//std::cout << "matmul: COFFEe check good" << std::endl;
		printf("matmul: COFFEE check good\n");
	}

	uint64_t dummy=0;
	std::cout<<"matmul while loop begin"<<std::endl;

	while (!(*zsim_done)) {
		tiled_mm(A, B, C, mlen, mlen, mlen, zsim_done);
		dummy += rand() % mlen;
	}


	//print random element from mul - avoid possible unwatned optimization?
	dummy=dummy % (mlen*mlen);
	uint64_t di=dummy / mlen;
	uint64_t dj=dummy % mlen;
	T rdp = C[di][dj];
	//std::cout<<"matmult at core "<<core_id<<" terminating, dummy="<<dummy<<", rdp="<<rdp<<std::endl;
	printf("matmul at core %d terminating, rdp=%d\n", core_id, rdp);

	return 0;
}

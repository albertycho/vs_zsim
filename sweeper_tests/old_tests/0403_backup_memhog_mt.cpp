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

void* memhog_thread(void* inarg) {

	thread_params* casted_inarg = (thread_params*) inarg;
	int core_id = 2;
	uint64_t ws_size = 33554432;
	core_id = casted_inarg->core_id;
	ws_size = casted_inarg->ws_size;

	//if(argc>1){
	//	core_id=atoi(argv[1]);
	//}
	//else{
	//	std::cout<<"qp_test: no core_id specified"<<std::endl;
	//}

	pid_t pid = getpid();

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	
	CPU_SET(core_id, &cpuset);

	int setresult = sched_setaffinity(pid, sizeof(cpuset), &cpuset);

	if(setresult!=0){
		std::cout<<"qp_test - sched_setaffinity failed"<<std::endl;
	}

	std::cout << "memhog - core_id = " << core_id << std::endl;

	uint64_t array_size = ws_size / sizeof(uint64_t);

	uint64_t * hog_arr = (uint64_t*)malloc(array_size*sizeof(uint64_t));

	std::cout << "memhog - ws_size = " << ws_size<< std::endl;
	std::cout << "memhog - array_size = " << array_size<< std::endl;

	bool* zsim_done;

	register_buffer((void*)(&zsim_done), (void*)0x18);

	
	uint64_t dummy1=0;
	for(uint64_t i=0;i<array_size;i++){
		hog_arr[i]=i;
		dummy1++;
		if(*zsim_done){
			break;
		}
		//if (i % 10000 == 0) {
		//	std::cout<<"memhog at core "<<core_id<<" initializing, i="<<i<<std::endl;
		//}
	}
	
	uint64_t dummy=0;
	uint64_t sum=0;
	while (!(*zsim_done)) {
		sum+=hog_arr[(dummy%array_size)];
		dummy++;
		//if (dummy % 10000 == 0) {
		//	//std::cout<<"memhog at core "<<core_id<<" , dummy="<<dummy<<", sum="<<sum<<std::endl;

		//}
	}



	std::cout<<"memhog at core "<<core_id<<" terminating, dummy="<<dummy<<", sum="<<sum<<std::endl;
	ofstream f("memhog_"+std::to_string(core_id)+"_iter_count.txt");
	f<<(dummy+dummy1)<<std::endl;

	f.close();

    return 0;

}



#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#include "../src/libzsim/zsim_nic_defines.hpp"
#include <sys/syscall.h>
#include <getopt.h>

using namespace std;

int main(int argc, char* argv[]) {

	uint64_t core_id=15;
	uint64_t ws_size=33554432; //32MB, want larger than LLC cap

	int c;

	static const struct option opts[] = {                                        
		{.name = "core_id", .has_arg = 1, .flag = NULL, .val = 's'},         
		{.name = "ws_size", .has_arg = 1,.flag = NULL, .val = 'K'},            
	};          

    while (1) {                                                                 
        c = getopt_long(argc, argv, "s:K", opts, NULL);
        if (c == -1) {                                                          
            printf("GETOPT FAILED (1 FAIL expected)\n");                        
            break;                                                              
        }                                                                       
        switch (c) {                                                            
            case 's':                                                           
                core_id = atol(optarg);
				printf("memhog - core_id = %d", core_id);                              
                break;                                                          
            case 'K':                                                           
                ws_size = atol(optarg);                  
				//printf("memhog - ws_size = %d", ws_size);                    
                break;     
            default:                                                            
                printf("Invalid argument %d\n", c);                             
                //assert(false);                                                  
        }                                                                       
    }                                                                           
                

	const pid_t pid = getpid();

	// cpu_set_t: This data set is a bitset where each bit represents a CPU.

	cpu_set_t cpuset;

	// CPU_ZERO: This macro initializes the CPU set set to be the empty set.

	CPU_ZERO(&cpuset);

	// CPU_SET: This macro adds cpu to the CPU set set.

	CPU_SET(core_id, &cpuset);

	int setresult = sched_setaffinity(pid, sizeof(cpu_set_t), &cpuset);
	if(setresult!=0){
		std::cout<<"nic_proxy_app - sched_setaffinity failed"<<std::endl;
	}

	//int setresult = pthread_setaffinity_np(pid, sizeof(cpuset), &cpuset);

	uint64_t array_size = ws_size / sizeof(uint64_t);

	uint64_t * hog_arr = (uint64_t*)malloc(array_size*sizeof(uint64_t));


	bool* zsim_done;

	register_buffer((void*)(&zsim_done), (void*)0x18);

	//std::cout<<"pid: "<<pid<<", setresult: " <<setresult<<std::endl;

	for(uint64_t i=0;i<array_size;i++){
		hog_arr[i]=i;
	}
	
	uint64_t dummy=0;
	uint64_t sum=0;
	while (!(*zsim_done)) {
		sum+=hog_arr[(dummy%array_size)];
		dummy++;
		//if (dummy % 10000 == 0) {
		//}
	}



	//register_buffer((void*) 0, (void*) 0xdead);
	std::cout<<"memhog - terminating, dummy="<<dummy<<", sum="<<sum<<std::endl;
    return 0;
}

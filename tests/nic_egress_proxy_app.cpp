#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#include "zsim_nic_defines.hpp"
#include <sys/syscall.h>

using namespace std;

int main() {

	const int core_id = 1;

	const pid_t pid = getpid();
//	const pid_t pid = gettid();
	//const pid_t pid = syscall(SYS_gettid);

	// cpu_set_t: This data set is a bitset where each bit represents a CPU.

	cpu_set_t cpuset;

	// CPU_ZERO: This macro initializes the CPU set set to be the empty set.

	CPU_ZERO(&cpuset);

	// CPU_SET: This macro adds cpu to the CPU set set.

	CPU_SET(core_id, &cpuset);

	int setresult = sched_setaffinity(pid, sizeof(cpuset), &cpuset);

	if(setresult!=0){
		std::cout<<"nic_egress_proxy_app - sched_setaffinity failed"<<std::endl;
	}



	//int setresult = pthread_setaffinity_np(pid, sizeof(cpuset), &cpuset);


	bool* nic_proc_on;

	register_buffer((void*)(&nic_proc_on), (void*)0xC);

	//std::cout<<"pid: "<<pid<<", setresult: " <<setresult<<std::endl;

	int dummy=0;
	while ((*nic_proc_on)) {
		dummy++;
		//if (dummy % 10000 == 0) {
		//	std::cout << "nic proxy app running" << std::endl;
		//}
	}



	//register_buffer((void*) 0, (void*) 0xdead);
	std::cout<<"nic egress proxy app - terminating"<<std::endl;
    return 0;
}

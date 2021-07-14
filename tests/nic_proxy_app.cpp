#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include <sched.h>
#include "zsim_nic_defines.hpp"

using namespace std;

int main() {

	const int core_id = 0;

	//const pid_t pid = getpid();

	// cpu_set_t: This data set is a bitset where each bit represents a CPU.

	cpu_set_t cpuset;

	// CPU_ZERO: This macro initializes the CPU set set to be the empty set.

	CPU_ZERO(&cpuset);

	// CPU_SET: This macro adds cpu to the CPU set set.

	CPU_SET(core_id, &cpuset);


	bool* nic_proc_on;

	register_buffer((void*)(&nic_proc_on), (void*)0xB);


	int dummy=0;
	while ((*nic_proc_on)) {
		dummy++;
		//if (dummy % 10000 == 0) {
		//	std::cout << "nic proxy app running" << std::endl;
		//}
	}



	//register_buffer((void*) 0, (void*) 0xdead);
	std::cout<<"nic proxy app - terminating"<<std::endl;
    return 0;
}

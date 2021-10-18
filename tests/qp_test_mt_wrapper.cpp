#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include "qp_test_thread_ver.hpp"
//#include "zsim_nic_defines.hpp"


int main(int argc, char* argv[]) {

	int numthreads = 1;
	if(argc>1){
		numthreads=atoi(argv[1]);
	}

	pthread_t *thread_arr = (pthread_t*)malloc(numthreads * sizeof(pthread_t));

	int i;
	for (i = 0; i < numthreads; i++) {
		int core_id = i + 2;
		int err = pthread_create(&thread_arr[i], NULL, qp_test, &core_id);
		if (err != 0) std::cout << "pthread_create failed" << std::endl;
	}

	for (i = 0; i < numthreads; i++) {
		pthread_join(thread_arr[i], NULL);
	}


	free(thread_arr);
	

    std::cout << "qp_test_mt wrapper done" << std::endl;
    return 0;
}



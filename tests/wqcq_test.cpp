#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include "magic_insts.hpp"

using namespace std;

int main() {

	uint64_t *wq_head;
	uint64_t *wq_tail;
	uint64_t *cq_head;
	uint64_t *cq_tail;
	uint64_t * wq;
	uint64_t * cq;
//	uint64_t * wq=(uint64_t*)calloc(32,sizeof(uint64_t));
//	uint64_t * cq=(uint64_t*)calloc(32,sizeof(uint64_t));
	

	//register_buffer((void*) nic_buffer, (void*)0xaa);
	register_buffer((void*) (&wq_head), (void*) 2);
	register_buffer((void*) (&wq_tail), (void*) 3);
	register_buffer((void*) (&cq_head), (void*) 4);
	register_buffer((void*) (&cq_tail), (void*) 5);
	register_buffer((void*) (&wq), (void*) 0);
	register_buffer((void*) (&cq), (void*) 1);

	std::cout<<"APP: wq_head="<<std::hex<<wq_head<<std::endl;
	std::cout<<"APP: *wq_head="<<std::hex<<*wq_head<<std::endl;

	int count=0;
	while(1)
	{
		wq[*wq_head]=0xccd0+*wq_head;
		//*wq_head++;
		*wq_head = (*wq_head)+1;
		std::cout<<"APP: *wq_head="<<*wq_head<<std::endl;
		while(*cq_head==*cq_tail)
		{
		}
		std::cout<<"app: cq["<<*cq_tail<<"] returned: "<<std::hex<<cq[*cq_tail]<<std::endl;
		//*cq_tail++;
		*cq_tail=(*cq_tail)+1;
		count++;
		if(count>5)
		{
			std::cout<<"app: recvd last packet"<<std::endl;
			break;
		}
	}

	register_buffer((void*) 0, (void*) 0xdead);
    return 0;
}

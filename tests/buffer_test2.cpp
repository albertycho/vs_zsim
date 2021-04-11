#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include "magic_insts.hpp"

using namespace std;

int main() {
	//zsim fails to capture nic_buffer addr without this print statement..
	
	uint64_t* nic_buffer;
		
	
	register_buffer((void*) (&nic_buffer), (void*)0xaa);


	while(1){
		if(*nic_buffer=0xcc0000){
			*nic_buffer= (*nic_buffer)+1;
			break;
		}
	}

	while(1){
		if((*nic_buffer) % 2 == 0)
		{
			std::cout<<"APP2 - test_tag="<<std::hex<<*nic_buffer<<std::endl;
			*nic_buffer=(*nic_buffer)+1;
		}
		if((*nic_buffer) > 0xcc000a) break;
	}
	
	register_buffer((void*) 0, (void*) 0xdead);
    return 0;
}

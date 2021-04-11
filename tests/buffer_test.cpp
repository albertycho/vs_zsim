#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include "magic_insts.hpp"

using namespace std;

int main() {
	//zsim fails to capture nic_buffer addr without this print statement..
	
	uint64_t* nic_buffer;
		
	
	register_buffer((void*) (&nic_buffer), (void*)0xaa);

	std::cout << "App: nicInfo0_test_tag_addr: " << std::hex <<  (nic_buffer) << std::endl;
	std::cout << "App: nicInfo0_test_tag_val: " << std::hex <<  (*nic_buffer) << std::endl;


	*nic_buffer=0xcc0000;

	while(1){
		if((*nic_buffer) % 2 == 1)
		{
			std::cout<<"APP1 - test_tag="<<std::hex<<*nic_buffer<<std::endl;
			*nic_buffer=(*nic_buffer)+1;
		}
		if((*nic_buffer) > 0xcc000a) break;
	}
	
	register_buffer((void*) 0, (void*) 0xdead);
    return 0;
}

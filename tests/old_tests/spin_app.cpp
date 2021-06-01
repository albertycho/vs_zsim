#include <unistd.h>
#include <iostream>

int main(){

	while(1)
	{
		sleep(5);
		std::cout<<"spintest"<<std::endl;
	}

	return 0;
}

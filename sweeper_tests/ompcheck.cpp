#include <iostream>
#include <algorithm>
#include <omp.h>
#define ARRAY_SIZE 100000000
#define ARRAY_VALUE 1231
int main()
{
	omp_set_num_threads(4);
	int *arr = new int[ARRAY_SIZE];
	std::fill_n(arr, ARRAY_SIZE, ARRAY_VALUE);
	#pragma omp parallel for
	for(int i = 0; i < ARRAY_SIZE; i++)
	{
		arr[i] = arr[i] / arr[i] + arr[i] / 5 - 14;
	}

	std::cout<<arr[100]<<std::endl;
	return 0;
}

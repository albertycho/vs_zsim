/*
 * RPCGenerator.cpp
 */

#include "RPCGenerator.hpp"
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <iostream>
#include <cstring>
#include <galloc.h>

#include <math.h>



int* get_random_permutation(unsigned int n, unsigned int clt_gid, uint64_t* seed) {
	unsigned int i, j, temp;
	assert(n > 0);

	/* Each client uses a different range in the cycle space of fastrand */
	for (i = 0; i < clt_gid * n; i++) {
		std::rand();
	}




	int* log = gm_calloc<int>(n);
	assert(log != NULL);
	for (i = 0; i < n; i++) {
		log[i] = i;
	}

	//printf("\tclient %d: shuffling..\n", clt_gid);
	for (i = n - 1; i >= 1; i--) {
		j = std::rand() % (i + 1);
		if (j < 0) {
			std::cout << "random_permutation - j is negative! j = " << j << std::endl;
		}
		temp = log[i];
		log[i] = log[j];
		log[j] = temp;
	}


	return log;
}

long double* get_zipf_table(unsigned int n, long double zipf_s){


	long double* log = gm_calloc<long double>(n);
	long double hn=0;
	long double zs=zipf_s;

	for(int i=0; i<n;i++){
		long double j = i+1;
		long double tmp = pow(j,zs);

		hn+= (1 / ( pow(j, zs) ));
	}


	for(int i=0;i<n;i++){
		long double j=i+1;
		long double tmp = (1 / (pow(j,zs)));
		log[i]= tmp / hn;
	}
	//now make frequency cumulative
	for(int i=1;i<n;i++){
		log[i]= log[i-1] + log[i];
		//leave print for debug / zipf_s calibration
		//std::cout<<"zipf_log["<<i<<"] = "<<log[i]<<std::endl;
	}

	return log;

}

RPCGenerator::RPCGenerator(size_t aNumKeys, size_t anUpdateFrac) :
	srand_seed(0xdeadbeef),
	num_keys(aNumKeys),
	update_fraction(anUpdateFrac)
{
	//key_arr = get_random_permutation(num_keys, 1 /*clt id*/, &srand_seed);
	key_arr = NULL;
	std::srand(srand_seed);
	//std::cout << "sizeof mica_op: " << sizeof(mica_op) << std::endl;
}

void
RPCGenerator::set_num_keys(size_t i_num_keys) {
	num_keys = i_num_keys;
	key_arr = get_random_permutation(num_keys, 1 /*clt id*/, &srand_seed);
	zcf = get_zipf_table(num_keys,zipf_s);
}


static void
convert_ipv6_5tuple(struct ipv6_5tuple* key1,
		union ipv6_5tuple_host* key2)
{
	uint32_t i;

	for (i = 0; i < IPV6_ADDR_LEN; i++) {
		key2->ip_dst[i] = key1->ip_dst[i];
		key2->ip_src[i] = key1->ip_src[i];
	}
	key2->port_dst = (key1->port_dst);
	key2->port_src = (key1->port_src);
	key2->proto = key1->proto;
	key2->pad0 = 0;
	key2->pad1 = 0;
	key2->reserve = 0;
}



int
RPCGenerator::generatePackedRPC_batch(char* userBuffer, uint32_t numreqs) const {
	char* buf_ptr = userBuffer;
	int retsize = 0;
	for (int i = 0; i < numreqs; i++) {
		printf("generatePackedRPC_batch: buf_ptr:%u\n", buf_ptr); 
		int reqsize = generatePackedRPC(buf_ptr);
		retsize += reqsize;
		buf_ptr += reqsize;
	}
	return retsize;

}

int
RPCGenerator::generatePackedRPC(char* userBuffer) const {
	bool is_update = true; //(std::rand() % 100) < (int)update_fraction ? true : false;
	//bool is_update = (std::rand() % 100) < (int)update_fraction ? true : false;
	int key_i;

	if(load_dist_type==ZIPF_DIST){
		key_i=num_keys-1;

		/* Tried instantiating random double generator once at init,
		 *  but doesn't work well with const function
		 */
		long double lower_bound=0;                                                  
		long double upper_bound=1;                                                  
		std::uniform_real_distribution<long double> unif(lower_bound, upper_bound);
		std::random_device rd;                                                  
		std::mt19937 gen(rd());                                                 
		long double rand_double = unif(gen);


		for(int i=0; i<num_keys;i++){
			if (zcf[i] >= rand_double){
				key_i=i;
				break;
			}
		}
		//std::cout<<"generatePackedRPC rand: " <<rand_double<<std::endl;
		//std::cout<<"key_i= "<<key_i<<std::endl;

	}
	else if(load_dist_type==UNIFORM_DIST){
		key_i= std::rand() % num_keys;
	}
	else{ //default uniform
		key_i= std::rand() % num_keys;
	}


	if(lg_type==0){ //herd
		struct mica_op req;

		uint128 hval = CityHash128((char*)&key_arr[key_i], 4);

		req.opcode = is_update ? HERD_OP_PUT : HERD_OP_GET;
		req.val_len = is_update ? MICA_MAX_VALUE : 0;
		/*
		   if (is_update) {
		   for (size_t i = 0; i < MICA_MAX_VALUE; i++) {
		   req.value[i] = (char)(std::rand() & 0xff); // generate a random byte
		   }
		   }
		   */
		//sizeof mica_op is 64
		memcpy(&req, &hval, sizeof(hval));
		//int copy_size = is_update? sizeof(req) : (sizeof(req) - MICA_MAX_VALUE);
		int copy_size = sizeof(req); //packet size is a test variable now, use uniform size per req
		memcpy(userBuffer, &(req), copy_size);

		//printf("Generated packet with opcode %d, val_len %d, key %llx\n", req.opcode, req.val_len, hval);

		return copy_size;
	}

	if(lg_type==1){ //l3fwd

		//FIXME: update to actual header format
		uint8_t port = key_i % 16; //numports
		struct ipv6_l3fwd_em_route entry;
		union ipv6_5tuple_host newkey;

		/* Create the ipv6 exact match flow */
		memset(&entry, 0, sizeof(entry));
		entry = ipv6_l3fwd_em_route_array[port];
		//entry.key.ip_dst[15] = (key_i + 1) % 256;//BYTE_VALUE_MAX;
		entry.key.ip_dst[15] = (port+1) % 256;//BYTE_VALUE_MAX;
		convert_ipv6_5tuple(&entry.key, &newkey);
		memcpy(userBuffer, &newkey, sizeof(newkey));
		return sizeof(newkey);
	}

	//Other cases?
	return 0;
}

uint32_t
RPCGenerator::getRPCPayloadSize() const {
	return sizeof(struct mica_op);
}

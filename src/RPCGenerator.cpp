#include "RPCGenerator.hpp"
#include <cstdlib>
#include <cstdio>
#include <cassert>

RPCGenerator::RPCGenerator(size_t aNumKeys, size_t anUpdateFrac) :
    srand_seed(0xdeadbeef),
    num_keys(aNumKeys),
    update_fraction(anUpdateFrac)
{
    //key_arr = get_random_permutation(num_keys, 1 /*clt id*/, &srand_seed);
    key_arr = NULL;
    std::srand(srand_seed);
}



void
RPCGenerator::generatePackedRPC(char* userBuffer) const {
    return;
}

uint32_t
RPCGenerator::getRPCPayloadSize() const {
    return sizeof(struct mica_op);
}
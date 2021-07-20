#include "RPCGenerator.hpp"
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <iostream>

RPCGenerator::RPCGenerator(size_t aNumKeys, size_t anUpdateFrac) :
    srand_seed(0xdeadbeef),
    num_keys(aNumKeys),
    update_fraction(anUpdateFrac)
{
    //key_arr = get_random_permutation(num_keys, 1 /*clt id*/, &srand_seed);
    key_arr = NULL;
    std::srand(srand_seed);
    std::cout << "sizeof mica_op: " << sizeof(mica_op) << std::endl;
}



void
RPCGenerator::generatePackedRPC(char* userBuffer) const {
    bool is_update = (std::rand() % 100) < (int)update_fraction ? true : false;
    int key_i = std::rand() % num_keys;

    struct mica_op req;
    req.opcode = is_update ? HERD_OP_PUT : HERD_OP_GET;
    req.val_len = is_update ? HERD_VALUE_SIZE : -1;
    if (is_update) {
        for (size_t i = 0; i < MICA_MAX_VALUE; i++) {
            req.value[i] = (char)(std::rand() & 0xff); // generate a random byte
        }
    }

    //sizeof mica_op is 64
    memcpy(userBuffer, &(req), sizeof(req));

    return;
}

uint32_t
RPCGenerator::getRPCPayloadSize() const {
    return sizeof(struct mica_op);
}
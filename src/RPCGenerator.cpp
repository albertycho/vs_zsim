#include "RPCGenerator.hpp"
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <iostream>
#include <cstring>
#include <galloc.h>



int* get_random_permutation(unsigned int n, unsigned int clt_gid, uint64_t* seed) {
    unsigned int i, j, temp;
    assert(n > 0);

    /* Each client uses a different range in the cycle space of fastrand */
    for (i = 0; i < clt_gid * n; i++) {
        std::rand();
    }

    //printf("client %d: creating a permutation of 0--%d. This takes time..\n",
    //    clt_gid, n - 1);

    //int* log = (int*)malloc(n * sizeof(int));
    int* log = (int*)gm_malloc(n * sizeof(int));
    assert(log != NULL);
    for (i = 0; i < n; i++) {
        log[i] = i;
    }

    //printf("\tclient %d: shuffling..\n", clt_gid);
    for (i = n - 1; i >= 1; i--) {
        j = std::rand() % (i + 1);
        temp = log[i];
        log[i] = log[j];
        log[j] = temp;
    }
    //printf("\tclient %d: done creating random permutation\n", clt_gid);

    return log;
}

RPCGenerator::RPCGenerator(size_t aNumKeys, size_t anUpdateFrac) :
    srand_seed(0xdeadbeef),
    num_keys(aNumKeys),
    update_fraction(anUpdateFrac)
{
    key_arr = get_random_permutation(num_keys, 1 /*clt id*/, &srand_seed);
    //key_arr = NULL;
    std::srand(srand_seed);
    std::cout << "sizeof mica_op: " << sizeof(mica_op) << std::endl;
}



void
RPCGenerator::generatePackedRPC(char* userBuffer) const {
    bool is_update = (std::rand() % 100) < (int)update_fraction ? true : false;
    int key_i = std::rand() % num_keys;

    struct mica_op req;
    //TODO: original code uses hash to get hval
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
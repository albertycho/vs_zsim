#ifndef __RPC_GENERATOR
#define __RPC_GENERATOR
#include <cstddef>
#include <stdint.h>

class RPCGenerator {
    private:
        uint64_t srand_seed;
        size_t num_keys, update_fraction;
        int* key_arr;

    public:
        RPCGenerator(size_t num_keys, size_t update_fraction);
        virtual void generatePackedRPC(char* userBuffer) const ;
        virtual uint32_t getRPCPayloadSize() const ;
};

#define MICA_MAX_VALUE \
  (64 - (sizeof(struct mica_key) + sizeof(uint8_t) + sizeof(uint8_t)))

/* Fixed-size 16 byte keys */
struct mica_key {
    unsigned long long __unused : 64;
    unsigned int bkt : 32;
    unsigned int server : 16;
    unsigned int tag : 16;
};

struct mica_op {
    struct mica_key key; /* This must be the 1st field and 16B aligned */
    uint8_t opcode;
    uint8_t val_len;
    uint8_t value[MICA_MAX_VALUE];
};

#endif //#ifndef __RPC_GENERATOR

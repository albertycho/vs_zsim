#ifndef __RPC_GENERATOR
#define __RPC_GENERATOR
#include <cstddef>
#include <stdint.h>

#define MICA_OP_GET 111
#define MICA_OP_PUT 112
#define HERD_MICA_OFFSET 10
#define HERD_OP_PUT (MICA_OP_PUT + HERD_MICA_OFFSET)
#define HERD_OP_GET (MICA_OP_GET + HERD_MICA_OFFSET)
#define HERD_VALUE_SIZE 32
#define MICA_MAX_VALUE \
  (64 - (sizeof(struct mica_key) + sizeof(uint8_t) + sizeof(uint8_t)))


class RPCGenerator {
    private:
        uint64_t srand_seed;
        size_t num_keys, update_fraction;
        int* key_arr;

    public:
        RPCGenerator(size_t num_keys, size_t update_fraction);
        virtual void generatePackedRPC(char* userBuffer) const ;
        virtual uint32_t getRPCPayloadSize() const ;
        void set_num_keys(size_t i_num_keys) { num_keys = i_num_keys; }
        void set_update_fraction(size_t i_update_fraction) { update_fraction = i_update_fraction; }

        //debug functions
        size_t get_num_keys() { return num_keys; }
        int* get_key_arr() { return key_arr; }
};


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

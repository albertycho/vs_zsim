
struct thread_params {
	int core_id;
	uint64_t mlen;
};
void* matmul_thread(void* inarg);

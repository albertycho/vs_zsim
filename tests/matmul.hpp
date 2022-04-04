
struct thread_params {
	int core_id;
	uint64_t ws_size;
};
void* matmul_thread(void* inarg);

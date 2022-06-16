
struct thread_params {
	int core_id;
	uint64_t ws_size;
	uint64_t *marr;
};
void* memhog_thread(void* inarg);

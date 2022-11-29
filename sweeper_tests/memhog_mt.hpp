
struct thread_params {
	int core_id;
	uint64_t ws_size;
	uint64_t *marr;
	uint64_t *marr2;
};
void* memhog_thread(void* inarg);

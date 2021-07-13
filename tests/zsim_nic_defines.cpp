#include "zsim_nic_defines.hpp"

#include <iostream>
#include <cstdint>
#include <stdlib.h>
#include <stdint.h>


int register_buffer(void * val, void* field)
{
//variables: start addr of WQ/CQ
//			 size of WQ/CQ
//can distinguish the type of variable depending on value of rbx?
	int dummy;
	asm(
		"movq %1, %%rbx;"
		"movq %2, %%rcx;"
		"xchg %%rbx, %%rbx;"
		:"=r" (dummy)
		:"r"(val), "r"(field)
		:"%rbx","%rcx" //clobbered registers
	);
	return 0;
}


int reg_wq(rmc_wq_t ** wq){
	register_buffer((void*) (wq), (void*) 0);
	return 0;
}

int reg_cq(rmc_cq_t **cq){
	register_buffer((void*) (cq), (void*) 1);
	return 0;
}


successStruct rmc_check_cq(rmc_wq_t *wq, rmc_cq_t *cq){
	successStruct ret;

	uint32_t tid;
	uint32_t wq_head = wq->head;
	uint32_t cq_tail = cq->tail;
	//std::cout<<"cq_tail at start of rmc_check_cq: "<<cq_tail<<std::endl;
	ret.success=1;
	ret.op=0;

	wq_entry_t raw_wqe;
	do{
		raw_wqe=wq->q[wq->head];

		cq_entry_t raw_cqe_entry = cq->q[cq_tail];
		bool tail_SR=raw_cqe_entry.SR;
		while((tail_SR==cq->SR) && (ret.success!=0))
		{
			//FIXME: okay to unset cq valid here?
			cq->q[cq_tail].valid=0;
			cq->tail = cq->tail + 1;
			//std::cout << "app increments cq tail" << std::endl;
			if (cq->tail >= MAX_NUM_WQ) {
                cq->tail = 0;
                cq->SR ^= 1;
				//std::cout<<"APP - flips cq SR"<<std::endl;
            }

			//std::cout<<"rmc_check_cq - success:"<<std::hex<<raw_cqe_entry.success<<std::endl;
		
			if ( raw_cqe_entry.success == 0x7F) {
				ret.recv_buf_addr =  raw_cqe_entry.recv_buf_addr;
				ret.op = RMC_INCOMING_SEND;
				ret.tid = raw_cqe_entry.tid;
				return ret;		
			}
			if (raw_cqe_entry.success == 1) {
				ret.recv_buf_addr = raw_cqe_entry.recv_buf_addr;
				ret.op = RMC_INCOMING_RESP;
				ret.tid = raw_cqe_entry.tid;
				return ret;
			}

			//FIXME: tid supposed to be used like this?
			tid = raw_cqe_entry.tid;
			std::cout<<"APP - invalidating wq entry "<<tid<<std::endl;
	        //wq->q[tid].valid = 0;
	
		  	ret.op = wq->q[tid].op;
			if (ret.op == RMC_SABRE) {
				ret.success = cq->q[cq_tail].success;
				if (ret.success == 0) {
	                    ret.tid = tid;
				}
			}
			else
			{
				ret.op=0;
			}
			cq_tail = cq->tail;
	
			//original code calls handler here
	
			raw_cqe_entry = cq->q[cq_tail]; 
			tail_SR = raw_cqe_entry.SR;
	
	
			}


	}while (raw_wqe.valid && (ret.success != 0));

	//std::cout<<"rmc_check_cq - ret.op:"<<std::hex<<ret.op<<std::endl;


	return ret;
}

//FIXME check data address datatype
int rmc_hw_send(rmc_wq_t *wq, uint32_t ctx_id, void *data_address, uint64_t length, int nid)
{
	uint32_t wq_head = wq->head;

	if(wq->q[wq_head].valid!=0){
		return -1;
	}
	//original code manipulates length with cacheblock size (?)

	 create_wq_entry(RMC_SEND, wq->SR, (uint32_t)ctx_id, (uint32_t)nid, (uint64_t)data_address, 0, length, (uint64_t)&(wq->q[wq_head]));

	wq->head =  wq->head + 1;
  	// check if WQ reached its end
  	if (wq->head >= MAX_NUM_WQ) {
    	wq->head = 0;
		wq->SR ^= 1;
		//std::cout<<"APP - flips wq SR"<<std::endl;
	}
	return 0;
}

void create_wq_entry(uint32_t op, bool SR, uint32_t cid, uint32_t nid,
            uint64_t buf_addr, uint64_t offset, uint64_t length,
            uint64_t wq_entry_addr) {
	
	wq_entry_t anEntry;
    anEntry.op = op;
    anEntry.SR = SR;
    anEntry.valid = 1;
    anEntry.buf_addr = buf_addr;
    anEntry.cid = cid;
    anEntry.nid = nid;
    anEntry.offset = offset;
    anEntry.length = length;
    *((wq_entry_t *)wq_entry_addr) = anEntry;
	//NOTIFY zsim of wq write
	register_buffer((void*)(NULL), (void*)NOTIFY_WQ_WRITE);

}

int rmc_hw_recv(rmc_wq_t *wq, uint32_t ctx_id, void *recv_buf, uint64_t length){

	uint32_t wq_head = wq->head;
	if(wq->q[wq_head].valid!=0){
		return -1;
	}
	create_wq_entry(RMC_RECV, wq->SR, ctx_id, 0, (uint64_t)recv_buf, 0, length, (uint64_t)&(wq->q[wq_head])); // node id and ctx offset don't care

	wq->head =  wq->head + 1;
  // check if WQ reached its end
  
	if (wq->head >= MAX_NUM_WQ) {
		wq->head = 0;
		wq->SR ^= 1;	
		//std::cout<<"APP - flips wq SR"<<std::endl;
	}

	return 0;
}

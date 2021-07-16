#include "nic_defines.h"
#include "log.h"
#include "ooo_core.h"
#include "ooo_core_recorder.h"
#include "memory_hierarchy.h"

#ifndef _CORE_NIC_API_H_
#define _CORE_NIC_API_H_

/// RRPP functions
/////////////////////


bool cq_wr_event_ready(uint64_t cur_cycle, glob_nic_elements* nicInfo, uint64_t core_id)
{
/*
* cq_wr_event_ready - returns true if there is a event due in CEQ
*/
	if (CQ_WR_EV_Q == NULL)
	{
		return false;
	}
	uint64_t q_cycle = CQ_WR_EV_Q->q_cycle;
	return q_cycle <= cur_cycle;
}

cq_wr_event* deq_cq_wr_event(glob_nic_elements* nicInfo, uint64_t core_id)
{
/*
* deq_cq_wr_event removes the head in CEQ and returns the entry to caller
*/
	futex_lock(&nicInfo->nic_elem[core_id].ceq_lock);
	assert(CQ_WR_EV_Q != NULL);

	cq_wr_event* ret = CQ_WR_EV_Q;

	CQ_WR_EV_Q = CQ_WR_EV_Q->next;
	futex_unlock(&nicInfo->nic_elem[core_id].ceq_lock);

	return ret;
}

int put_cq_entry(cq_entry_t ncq_entry, glob_nic_elements* nicInfo, uint64_t core_id)
{
/*
* put_cq_entry - takes in a new CQ_entry as input and inserts it in CQ
*/
	//separate out function that deals with the head/tail and SR
	rmc_cq_t* cq = nicInfo->nic_elem[core_id].cq;
	uint64_t cq_head = nicInfo->nic_elem[core_id].cq_head;
	
	
	if (cq->SR == cq->q[cq_head].SR) {
		info("FAILED cq->SR == cq->q[cq_head].SR check");
		info("cq_head=%d",cq_head);
		return -1;
	}

	ncq_entry.SR = nicInfo->nic_elem[core_id].ncq_SR;
	cq->q[cq_head] = ncq_entry;

	cq_head = cq_head + 1;
	if (cq_head >= MAX_NUM_WQ) {
		cq_head = 0;
		//flip SR!
		nicInfo->nic_elem[core_id].ncq_SR = !(nicInfo->nic_elem[core_id].ncq_SR);
		//std::cout<<"NIC - flip cq SR"<<std::endl;
	}
	nicInfo->nic_elem[core_id].cq_head = cq_head;
	return 0;

}

int process_cq_wr_event(cq_wr_event* cq_wr, glob_nic_elements* nicInfo, uint64_t core_id)
{
/*
* process_cq_wr_event - takes popped CEQ entry as input. 
*		processes it by writing to CQ, and frees CEQ entry
*/
	assert(cq_wr != NULL);

	cq_entry_t ncq_entry = cq_wr->cqe;
	int put_cq_entry_success = put_cq_entry(ncq_entry, nicInfo, core_id);
	if (put_cq_entry_success == -1)
	{
		return -1;
	}

	gm_free(cq_wr);
	return 0;

}


int core_ceq_routine(uint64_t cur_cycle, glob_nic_elements * nicInfo, uint64_t core_id) {
/*
* core_ceq_routine - put all required action in one function to keep core.cpp files simpler
*		checks CEQ for event due and processes entry if necessary
*/
	
	//TODO check if cq entry is available
	rmc_cq_t* cq = nicInfo->nic_elem[core_id].cq;
	uint64_t cq_head = nicInfo->nic_elem[core_id].cq_head;
	if (cq->SR == cq->q[cq_head].SR) {
		return -1;
	}

	if (cq_wr_event_ready(cur_cycle, nicInfo, core_id))
	{
		//std::cout << std::dec << "wr_event ready @ cycle      :" << cur_cycle << std::endl;

		cq_wr_event* cqwrev = deq_cq_wr_event(nicInfo, core_id);


		if (process_cq_wr_event(cqwrev, nicInfo, core_id) != 0)
		{
			panic("cq_entry write failed");
			return -1;
		}
	}
	return 0;
}



//functions for interfacing load_generator

int get_next_message(void* lg_p) {
/*
* get_next_message - grabs the next message from load generator and updates the cycle next packet is due
*		(in this prototype its all done manually in this function. eventually want to make it more elegant)
*/
	int next_message = ((load_generator*)lg_p)->message;
	((load_generator*)lg_p)->message = ((load_generator*)lg_p)->message + 1;
	//((load_generator*)lg_p)->next_cycle = ((load_generator*)lg_p)->next_cycle + 1000000; 
	((load_generator*)lg_p)->next_cycle = ((load_generator*)lg_p)->next_cycle + 10; 
	//TODO: will do something more sophisticated for setting next_cycle offset 

	return next_message;
}

uint32_t allocate_recv_buf(uint32_t blen, glob_nic_elements* nicInfo, uint32_t core_id) { // reusing(modifying) sim_nic.h function
/*
* allocate_recv_buf - finds free recv buffer from buffer pool and returns head index
*				returns the index of allocated recv buffer, not the address!
*/
	
	uint32_t head = 0;
	while (head < RECV_BUF_POOL_SIZE)
	{
		if (NICELEM.rb_dir[head].in_use == false)
		{
			bool fit = true;
			for (uint32_t i = head; i < head + blen; i++)
			{
				if (i >= RECV_BUF_POOL_SIZE) {
					return RECV_BUF_POOL_SIZE + 1;
				}
				if (NICELEM.rb_dir[i].in_use == true)
				{
					fit = false;
					if (NICELEM.rb_dir[i].is_head)
					{
						head = i + NICELEM.rb_dir[i].len;
					}
					else//should not happen
					{
						std::cout << "ALLOCATE_RECV_BUF BROKEN - IS_HEAD ERROR - 151 head: " <<head <<", i: "<< i << std::endl;
					}
					break;
				}

			}
			if (fit)
			{
				NICELEM.rb_dir[head].is_head = true;
				NICELEM.rb_dir[head].len = blen;
				for (uint32_t i = head; i < head + blen; i++)
				{
					NICELEM.rb_dir[i].in_use = true;
				}
				return head;
			}
		}
		else
		{
			if (NICELEM.rb_dir[head].is_head)
			{
				head = head + NICELEM.rb_dir[head].len;
			}
			else//should not happen
			{
				std::cout << "ALLOCATE_RECV_BUF BROKEN - IS_HEAD ERROR - 176, head: "<<head << std::endl;
			}

		}

	}

	return RECV_BUF_POOL_SIZE + 1; // indicate that we didn't find a fit

}

cq_entry_t generate_cqe(uint32_t success, uint32_t tid, uint64_t recv_buf_addr)
{
/*
* generate_cqe - builds a cq entry based on inputs
*/
	cq_entry_t cqe;
	cqe.recv_buf_addr = recv_buf_addr;
	cqe.success = success;
	cqe.tid = tid;
	cqe.valid = 1;
	return cqe;

}

void cq_event_enqueue(uint64_t q_cycle, cq_entry_t cqe, glob_nic_elements* nicInfo, uint64_t core_id)
{
/*
* cq_event_enqueue - takes in a cq entry and its scheduled cycle
*			creqtes a CEQ entry and enqueues it
*/
	cq_wr_event* cq_wr_e = gm_calloc<cq_wr_event>();
	cq_wr_e->cqe = cqe;
	cq_wr_e->q_cycle = q_cycle;
	cq_wr_e->next = NULL;
	
	futex_lock(&nicInfo->nic_elem[core_id].ceq_lock);
	if (nicInfo->nic_elem[core_id].cq_wr_event_q == NULL)
	{
		nicInfo->nic_elem[core_id].cq_wr_event_q = cq_wr_e;
	}
	else
	{
		cq_wr_event* cq_wr_event_q_tail = CQ_WR_EV_Q;
		while (cq_wr_event_q_tail->next != NULL)
		{
			cq_wr_event_q_tail = cq_wr_event_q_tail->next;
		}
		cq_wr_event_q_tail->next = cq_wr_e;
	}
	futex_unlock(&nicInfo->nic_elem[core_id].ceq_lock);
}

int create_CEQ_entry(uint64_t recv_buf_addr, uint32_t success, uint64_t cur_cycle, glob_nic_elements* nicInfo, uint32_t core_id) {
/*
* create_CEQ_entry - wrapper to generate cq entry, and enq corresponding CEQ entry
*/
	
	if (core_id > ((zinfo->numCores) - 1)) {
		info("create_ceq_entry - core_id out of bound: %d", core_id);
	}


	uint64_t ceq_delay = 100; //TODO: make this programmable
	uint32_t tid = 0;//TODO: handle tid better
	cq_entry_t cqe = generate_cqe(success, tid, recv_buf_addr);
	cq_event_enqueue(cur_cycle + ceq_delay, cqe, nicInfo, core_id);

	return 0;
}

int RRPP_routine(uint64_t cur_cycle, glob_nic_elements* nicInfo, void* lg_p, uint32_t core_id) {
	/*Wrapper for the whole RRPP routine*/

	//TODO: rewrite this function around inject_incoming_packet

	/*
	if (!gm_isready()) return 0;
	if (nicInfo->nic_elem[0].cq_valid == false) return 0;


	*/
	return 0;
}


int inject_incoming_packet(uint64_t cur_cycle, glob_nic_elements* nicInfo, void* lg_p, uint32_t core_id, int srcId, OOOCore* core, OOOCoreRecorder* cRec, FilterCache* l1d/*MemObject* dest*/) {
/*
* inject_incoming_packet - takes necessary architectural AND microarchitectural actions to inject packet
*				fetches next msg from load generator
*				allocates recv buffer and writes msg to it
*				record the memory access from injection (microarchitecutral)
*				creates ceq entry (architectural)
*/
	//TODO: passing on l1d for now, to use getParent method. Will have to be updated with the correct Memory Direct access method
	if (core_id > ((zinfo->numCores) - 1)) {
		info("inject_incoming_packet - core_id out of bound: %d", core_id);
	}
	int message = get_next_message(lg_p);
	futex_lock(&nicInfo->nic_elem[core_id].rb_lock);
	uint32_t rb_head = allocate_recv_buf(8, nicInfo, core_id);
	futex_unlock(&nicInfo->nic_elem[core_id].rb_lock);
	if (rb_head > RECV_BUF_POOL_SIZE) {
		info("core %d out of recv buffer, cycle %d", core_id, cur_cycle);
		//info("((zinfo->numCores) - 1)=%d", ((zinfo->numCores) - 1));
		return -1;
	}
	//std::cout << "allocate_recv_buf returned :" << std::dec << rb_head << ", core_id: " << core_id << std::endl;

	uint64_t recv_buf_addr = (uint64_t)(&(nicInfo->nic_elem[core_id].recv_buf[rb_head]));
	// write message to recv buffer
	if (core_id > ((zinfo->numCores) - 1)) {
		info("inject_incoming_packet - core_id out of bound: %d", core_id);
	}
	nicInfo->nic_elem[core_id].recv_buf[rb_head] = message;

	MemReq req;
	Address rbuf_lineAddr = recv_buf_addr >> lineBits;
	MESIState dummyState = MESIState::I;
	assert((!cRec->getEventRecorder()->hasRecord()));

	if (nicInfo->record_nic_access) {
		req = { rbuf_lineAddr, GETX, 0xDA0000, &dummyState, cur_cycle, NULL, dummyState, srcId, 0 };
	}
	else {
		req = { rbuf_lineAddr, GETX, 0xDA0000, &dummyState, cur_cycle, NULL, dummyState, srcId, MemReq::NORECORD };
	}

	//uint64_t reqSatisfiedCycle = core->l1d->getParent(recv_buf_addr >> lineBits)->access(req);
	uint64_t reqSatisfiedCycle = l1d->getParent(rbuf_lineAddr)->access(req);
	cRec->record(cur_cycle, cur_cycle, reqSatisfiedCycle);
	uint64_t ceq_cycle = (uint64_t)(((load_generator*)lg_p)->next_cycle);
	create_CEQ_entry(recv_buf_addr, 0x7f, ceq_cycle, nicInfo, core_id);

	//TODO may want to pass the reqSatisfiedcycle value back to the caller via updating an argument

	return 0;

}


/// RCP functions
/////////////////////

bool check_rcp_eq(uint64_t cur_cycle, glob_nic_elements* nicInfo, uint32_t core_id) {
/* 
* check rcp_eq head. similar to checking CEQ head
*/
	
	if (RCP_EQ == NULL) {
		return false;
	}

	//if rcp_eq head == NULL return false;
	uint64_t q_cycle = RCP_EQ->q_cycle;
	return q_cycle <= cur_cycle;
	//if rcp_eq head -> q_cycle <= cur_cycle return true;
}


rcp_event* deq_rcp_eq(glob_nic_elements* nicInfo, uint32_t core_id) {
/* 
* similar to deq_cq_wr_event
*/
	assert(RCP_EQ != NULL);
	rcp_event* ret = RCP_EQ;
	RCP_EQ = RCP_EQ->next;
	return ret;
}

void process_rcp_event(rcp_event* nrcp_event, glob_nic_elements* nicInfo, uint32_t core_id, uint64_t cur_cycle) {
/*
* process_rcp_event - takes in dequeued rcp_event. 
*			writes response to local buffer and create cq write event
*/
	//write response to local buffer
	//TODO: response may have to be programmable
	uint64_t response = nrcp_event->lbuf_data + 0xca000000;
	*((uint64_t*)(nrcp_event->lbuf_addr)) = response;
	//access lbuf microarchitecturally
	uint64_t lbuf_addr = nrcp_event->lbuf_addr;
	
	gm_free(nrcp_event);

	//create cq entry (CEQ entry)
	create_CEQ_entry(lbuf_addr, 1, cur_cycle, nicInfo, core_id);
}

void RCP_routine(uint64_t cur_cycle, glob_nic_elements* nicInfo, uint32_t core_id) {
/*
* RCP_routine - wrapper for checking avaliable rcp action and processing it
*/
	if (check_rcp_eq(cur_cycle, nicInfo, core_id)) {
		rcp_event* nrcp_event = deq_rcp_eq(nicInfo, core_id);
		process_rcp_event(nrcp_event, nicInfo, core_id, cur_cycle);
	}
}




/// RGP functions
// acho: Moved to zsim.cpp due to linking issues. May want to figure out a more elegant way later

#endif
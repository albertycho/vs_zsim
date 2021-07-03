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
	if (CQ_WR_EV_Q == NULL)
	{
		return false;
	}
	uint64_t q_cycle = CQ_WR_EV_Q->q_cycle;
	return q_cycle <= cur_cycle;
}

cq_wr_event* deq_cq_wr_event(glob_nic_elements* nicInfo, uint64_t core_id)
{
	assert(CQ_WR_EV_Q != NULL);

	cq_wr_event* ret = CQ_WR_EV_Q;

	CQ_WR_EV_Q = CQ_WR_EV_Q->next;

	return ret;
}

int put_cq_entry(cq_entry_t ncq_entry, glob_nic_elements* nicInfo, uint64_t core_id)
{
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
//int core_cq_wr_event_action(uint64_t cur_cycle, glob_nic_elements* nicInfo, uint64_t core_id){
int core_ceq_routine(uint64_t cur_cycle, glob_nic_elements * nicInfo, uint64_t core_id) {
//put all required action in one function to keep core.cpp files simpler
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
bool check_load_gen(void* lg_p, int cur_cycle) {
	
	int lg_next_cycle = ((load_generator*)lg_p)->next_cycle;
	if (lg_next_cycle <= cur_cycle) {
		return true;
	}
	return false;
}

int get_next_message(void* lg_p) {
	int next_message = ((load_generator*)lg_p)->message;
	((load_generator*)lg_p)->message = ((load_generator*)lg_p)->message + 1;
	//((load_generator*)lg_p)->next_cycle = ((load_generator*)lg_p)->next_cycle + 1000000; 
	((load_generator*)lg_p)->next_cycle = ((load_generator*)lg_p)->next_cycle + 10; 
	//TODO: will do something more sophisticated for setting next_cycle offset 

	return next_message;
}

uint32_t allocate_recv_buf(uint32_t blen, glob_nic_elements* nicInfo, uint32_t core_id) { // reusing(modifying) sim_nic.h function
//TODO: go over this, find problems.. might be better to rewrite
	//returns the index of allocated recv buffer, not the address!
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
						std::cout << "ALLOCATE_RECV_BUF BROKEN - IS_HEAD ERROR" << std::endl;
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
				std::cout << "ALLOCATE_RECV_BUF BROKEN - IS_HEAD ERROR" << std::endl;
			}

		}

	}

	return RECV_BUF_POOL_SIZE + 1; // indicate that we didn't find a fit

}

cq_entry_t generate_cqe(uint32_t success, uint32_t tid, uint64_t recv_buf_addr)
{
	cq_entry_t cqe;
	cqe.recv_buf_addr = recv_buf_addr;
	cqe.success = success;
	cqe.tid = tid;
	cqe.valid = 1;
	return cqe;

}

void cq_event_enqueue(uint64_t q_cycle, cq_entry_t cqe, glob_nic_elements* nicInfo, uint64_t core_id)
{
	cq_wr_event* cq_wr_e = gm_calloc<cq_wr_event>();
	cq_wr_e->cqe = cqe;
	cq_wr_e->q_cycle = q_cycle;
	cq_wr_e->next = NULL;

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
}

int create_CEQ_entry(uint64_t recv_buf_addr, uint32_t success, uint64_t cur_cycle, glob_nic_elements* nicInfo, uint32_t core_id) {
	//TODO: create and enq CEQ entry - reuse functions from sim_nic.h
	
	if (core_id > ((zinfo->numCores) - 1)) {
		info("inject_incoming_packet - core_id out of bound: %d", core_id);
	}

	uint64_t ceq_delay = 100;
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

	if (check_load_gen(lg_p, cur_cycle)) {
		int message = get_next_message(lg_p);
		uint32_t rb_head = allocate_recv_buf(1, nicInfo, core_id);
		uint64_t recv_buf_addr = (uint64_t)(&(nicInfo->nic_elem[core_id].recv_buf[rb_head]));
		//inject_inbound_packet(message, recv_buf_addr);
		create_CEQ_entry(recv_buf_addr, 0x7f, cur_cycle, nicInfo, core_id);

	}
	*/
	return 0;
}


int inject_incoming_packet(uint64_t cur_cycle, glob_nic_elements* nicInfo, void* lg_p, uint32_t core_id, int srcId, OOOCore* core, OOOCoreRecorder* cRec, FilterCache* l1d/*MemObject* dest*/) {
	//TODO: passing on l1d for now, to use getParent method. Will have to be updated with the correct Memory Direct access method
	if (core_id > 2) {
		info("inject_incoming_packet - core_id out of bound: %d", core_id);
	}
	int message = get_next_message(lg_p);
	uint32_t rb_head = allocate_recv_buf(8, nicInfo, core_id);
	if (rb_head > RECV_BUF_POOL_SIZE) {
		info("core %d out of recv buffer", core_id);
		info("((zinfo->numCores) - 1)=%d", ((zinfo->numCores) - 1));
		return -1;
	}
	uint64_t recv_buf_addr = (uint64_t)(&(nicInfo->nic_elem[core_id].recv_buf[rb_head]));
	// write message to recv buffer
	if (core_id > 2) {
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
	//check rcp_eq head. similar to checking CEQ head
	
	if (RCP_EQ == NULL) {
		return false;
	}

	//if rcp_eq head == NULL return false;
	uint64_t q_cycle = RCP_EQ->q_cycle;
	return q_cycle <= cur_cycle;
	//if rcp_eq head -> q_cycle <= cur_cycle return true;
}


rcp_event* deq_rcp_eq(glob_nic_elements* nicInfo, uint32_t core_id) {
	//similar to deq_cq_wr_event
	assert(RCP_EQ != NULL);
	rcp_event* ret = RCP_EQ;
	RCP_EQ = RCP_EQ->next;
	return ret;
}

void process_rcp_event(rcp_event* nrcp_event, glob_nic_elements* nicInfo, uint32_t core_id, uint64_t cur_cycle) {
	//write response to local buffer
	uint64_t response = nrcp_event->lbuf_data + 0xca000000;
	*((uint64_t*)(nrcp_event->lbuf_addr)) = response;
	//access lbuf microarchitecturally
	uint64_t lbuf_addr = nrcp_event->lbuf_addr;
	
	gm_free(nrcp_event);

	//create cq entry (CEQ entry)
	create_CEQ_entry(lbuf_addr, 1, cur_cycle, nicInfo, core_id);
}

void RCP_routine(uint64_t cur_cycle, glob_nic_elements* nicInfo, uint32_t core_id) {
	if (check_rcp_eq(cur_cycle, nicInfo, core_id)) {
		rcp_event* nrcp_event = deq_rcp_eq(nicInfo, core_id);
		process_rcp_event(nrcp_event, nicInfo, core_id, cur_cycle);
	}
}




/// RGP functions
// acho: Moved to zsim.cpp due to linking issues. May want to figure out a more elegant way later
/*

bool check_wq(uint64_t core_id, glob_nic_elements* nicInfo) {
	wq_entry_t raw_wq_entry = NICELEM.wq->q[NICELEM.wq_tail];
	//wq_entry_t raw_wq_entry = wq->q[SIM_NICELEM.wq_tail];
	if ((raw_wq_entry.valid == 0) || (NICELEM.nwq_SR != (raw_wq_entry.SR))) {
		return false;
	}
	return true;
}

wq_entry_t deq_wq_entry(uint64_t core_id, glob_nic_elements* nicInfo) {
	wq_entry_t raw_wq_entry = NICELEM.wq->q[NICELEM.wq_tail];
	
	rmc_wq_t* wq = NICELEM.wq;

	//FIXME: invalidating WQ at NIC for now. 
	//must be updated s.t. app does this when getting cq entry
	wq->q[NICELEM.wq_tail].valid = 0;
	NICELEM.wq_tail = NICELEM.wq_tail + 1;
	if (NICELEM.wq_tail >= MAX_NUM_WQ) {
		NICELEM.wq_tail = 0;
		NICELEM.nwq_SR = !(NICELEM.nwq_SR);
		//std::cout<<"NIC - flip wq SR "<<std::endl;
	}

	return raw_wq_entry;
}

void process_wq_entry(wq_entry_t cur_wq_entry, uint64_t core_id, glob_nic_elements* nicInfo)
{
	if (cur_wq_entry.op == RMC_RECV) {
		//TODO:rewrite free_recv_buf_addr for core_nic_api
		//free_recv_buf_addr(cur_wq_entry.buf_addr, core_id);
		return;
	}

	if (cur_wq_entry.op == RMC_SEND)
	{
		// create a RRPP EQ entry? if model expects a remote response for this send
		return;
	}
}

int nic_rgp_action(uint64_t core_id, glob_nic_elements* nicInfo)
{
	if (!check_wq(core_id, nicInfo))
	{
		//nothing in wq, return
		return 0;
	}
	wq_entry_t cur_wq_entry = deq_wq_entry(core_id, nicInfo);
	process_wq_entry(cur_wq_entry, core_id, nicInfo);

	return 0;
}

*/

#endif
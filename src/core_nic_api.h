#include "nic_defines.h"
#include "log.h"
#include "ooo_core.h"
#include "ooo_core_recorder.h"
#include "memory_hierarchy.h"

#include <iostream>
#include <fstream>


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

//int add_time_card(p_time_card* ptc, load_generator* lg_p) {
int tc_map_insert(uint64_t ptag, uint64_t issue_cycle) {


	load_generator* lg_p = (load_generator*)gm_get_lg_ptr();
	futex_lock(&lg_p->ptc_lock);
	info("ptc insert to map");
	//(lg_p->tc_map)[ptag] = issue_time;
	lg_p->tc_map->insert(std::make_pair(ptag, issue_cycle));
	info("ptc insert to map successful, map size : %d", lg_p->tc_map->size());
	futex_unlock(&lg_p->ptc_lock);

	return 0;
}

int tc_linked_list_insert(uint64_t ptag, uint64_t issue_cycle) {
	///////////////////////
	//TODO - log incoming packet ptag & issue time
	p_time_card* ptc = gm_calloc<p_time_card>();
	ptc->issue_cycle = issue_cycle;
	ptc->ptag = ptag;

	if (lg_p->ptc_head == NULL)
	{
		lg_p->ptc_head = ptc;
		return 0;
	}
	
	uint64_t tcount = 0;
	
	futex_lock(&lg_p->ptc_lock);
	p_time_card* head = lg_p->ptc_head;
	while (head->next != NULL) {
		head = head->next;
		tcount++;
	}
	head->next = ptc;
	info("insert ptc pcount = %d", tcount);
	futex_unlock(&lg_p->ptc_lock);
	return 0;

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

	if (ncq_entry.success==0x7f) {
		uint64_t ptag = ncq_entry.tid;
		uint64_t issue_cycle = cq_wr->q_cycle;
		//add_time_card(ptag, issue_cycle);
		tc_linked_list_insert(ptag, issue_cycle);
	}

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
	
	((load_generator*)lg_p)->ptag = ((load_generator*)lg_p)->ptag + 1;

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
	load_generator* lg_p = (load_generator * )gm_get_lg_ptr();
	
	if (core_id > ((zinfo->numCores) - 1)) {
		info("create_ceq_entry - core_id out of bound: %d", core_id);
	}


	uint64_t ceq_delay = 100; //TODO: make this programmable
	
	uint32_t tid = lg_p->ptag;//TODO: put tid=lg_p->ptag

	//TODO - log incoming packet ptag & issue time
	//p_time_card* ptc = gm_calloc<p_time_card>();
	//ptc->issue_cycle = cur_cycle;
	//ptc->ptag = lg_p->ptag;

	//insert_time_card(lg_p->ptag, cur_cycle, lg_p);
	if (success == 0x7f) {
		uint64_t start_cycle = cur_cycle;
		tc_map_insert(tid, start_cycle);
	}

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
	uint32_t rb_head = allocate_recv_buf(1, nicInfo, core_id);
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
	//nicInfo->nic_elem[core_id].recv_buf[rb_head].line_seg[0] = message;

	((load_generator*) lg_p)->RPCGen->generatePackedRPC((char*)(&(nicInfo->nic_elem[core_id].recv_buf[rb_head].line_seg[0])));

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
// NIC RGP functions 

bool check_wq(uint64_t core_id, glob_nic_elements* nicInfo) {

	if (nicInfo->nic_elem[core_id].wq_valid == false) {
		return false;
	}

	wq_entry_t raw_wq_entry = NICELEM.wq->q[NICELEM.wq_tail];
	//wq_entry_t raw_wq_entry = wq->q[SIM_NICELEM.wq_tail];
	if ((raw_wq_entry.valid == 0) || (NICELEM.nwq_SR != (raw_wq_entry.SR))) {
		return false;
	}
	return true;
}

wq_entry_t deq_wq_entry(uint64_t core_id, glob_nic_elements* nicInfo) {
	/*
	* deq_wq_entry: removes the first entry from wq and returns it
	*   returned wq_entry can be then processed
	*/
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

void enq_rcp_event(uint64_t q_cycle, uint64_t lbuf_addr, uint64_t lbuf_data, glob_nic_elements* nicInfo, uint64_t core_id) {
	/*
	* eqn_rcp_event - called by process_wq_entry
	*       creates an rcp_event that corresponds to a RGP call from wq_entry(RMC_SEND)
	*       and enqueues it to RCP_EQ, which is polled by core
	*/

	rcp_event* rcp_e = gm_calloc<rcp_event>();
	rcp_e->lbuf_addr = lbuf_addr;
	rcp_e->lbuf_data = lbuf_data;
	rcp_e->q_cycle = q_cycle;
	rcp_e->next = NULL;

	if (RCP_EQ == NULL) {
		RCP_EQ = rcp_e;
	}
	else {
		rcp_event* rcp_eq_tail = RCP_EQ;
		while (rcp_eq_tail->next != NULL) {
			rcp_eq_tail = rcp_eq_tail->next;
		}
		rcp_eq_tail->next = rcp_e;
	}
}

int free_recv_buf(uint32_t head, uint32_t core_id) {
	/*
	* free_recv_buf - called by free_recv_buf_addr.
			Takes the index of the recv_buf to be freed
	*/
	assert(NICELEM.rb_dir[head].is_head);
	assert(NICELEM.rb_dir[head].in_use);
	//dbg print
	//info("free_recv_buf - core_id = %d, head = %d", core_id, head);

	uint32_t blen = NICELEM.rb_dir[head].len;
	futex_lock(&nicInfo->nic_elem[core_id].rb_lock);
	for (uint32_t i = head; i < head + blen; i++) {
		NICELEM.rb_dir[i].in_use = false;
		NICELEM.rb_dir[i].is_head = false;
		NICELEM.rb_dir[i].len = 0;
	}
	futex_unlock(&nicInfo->nic_elem[core_id].rb_lock);
	//dbg print
	//info("free_recv_buf - finished freeing");
	return 0;
}

int free_recv_buf_addr(uint64_t buf_addr, uint32_t core_id) {
	/*
	* free_recv_buf_addr - called if wq_entry is RMC_RECV.
	*       calculate the index of recv_buf from the addr and calls free_recv_buf
	*       (added layer of function for easier edit/debug)
	*/
	uint64_t buf_base = (uint64_t)(&(NICELEM.recv_buf[0]));
	uint64_t offset = buf_addr - buf_base;
	uint32_t head = (uint32_t)(offset / 64); //divide by size of buffer in bytes
	//TODO may need debug prints to check offset and head calculation

	return free_recv_buf(head, core_id);
}


int log_packet_latency_list(uint64_t ptag, uint64_t fin_time) {
	
	load_generator* lg_p = (load_generator*)gm_get_lg_ptr();
	
	ofstream list_latency_file("list_latency.txt", ios::app);
	//list_latency_file.open("list_latency.txt");

	/*
	futex_lock(&lg_p->ptc_lock);
	info("reading ptc from map and removing");
	uint64_t start_time = (*(lg_p->tc_map))[ptag];
	lg_p->tc_map->erase(ptag);
	futex_unlock(&lg_p->ptc_lock);
	info("ptc erase successful");
	*/
	/*
	uint64_t latency = fin_time - start_time;


	return 0;
	*/

	////////////////code with linked list

	//info("log packet latency called");
	uint64_t tcount = 0;

	assert(lg_p->ptc_head != NULL);

	futex_lock(&lg_p->ptc_lock);
	p_time_card* tmp = lg_p->ptc_head;

	if (tmp->ptag == ptag) {
		lg_p->ptc_head = tmp->next;
	}
	else {
		p_time_card* prev = tmp;
		tmp = tmp->next;
		while (tmp->ptag != ptag) {
			tcount++;

			prev = tmp;
			tmp = tmp->next;
			//DBG
			if (tmp == NULL) {
				info("ptag=%d", ptag);
			}
			assert(tmp != NULL);
		}

		prev->next = tmp->next;

	}
	uint64_t latency = fin_time - tmp->issue_cycle;
	info("log packet latency pcount = %d ,ptag = %d, latency = %d", tcount, ptag, latency);
	futex_unlock(&lg_p->ptc_lock);

	//uint64_t latency = fin_time - tmp->issue_cycle;

	//LOG latency
	//out >> "tag= " >> tmp->ptag >> ", lat= " >> latency >> std::endl;
	list_latency_file << ptag << ", " << (fin_time - (tmp->issue_cycle)) << std::endl;

	list_latency_file.close();
	//FREE ptc pointer
	gm_free(tmp);
	return 0;
}

int enq_dpq(uint64_t lbuf_addr, uint64_t end_time, uint64_t ptag) {
	done_packet_info* dpq_entry = gm_calloc<done_packet_info>();
	dpq_entry->end_cycle = end_time;
	dpq_entry->lbuf_addr = lbuf_addr;
	dpq_entry->tag = ptag;
	dpq_entry->next = NULL;
	dpq_entry->prev = NULL;

	glob_nic_elements* nicInfo = (glob_nic_elements * ) gm_get_nic_ptr();
	futex_lock(&(nicInfo->dpq_lock));
	if (nicInfo->done_packet_q_tail == NULL) {
		nicInfo->done_packet_q_tail = dpq_entry;
		nicInfo->done_packet_q_head = dpq_entry;
	}
	else {
		nicInfo->done_packet_q_tail->next = dpq_entry;
		dpq_entry->prev = nicInfo->done_packet_q_tail;
		nicInfo->done_packet_q_tail = dpq_entry;
	}

	nicInfo->dpq_size = nicInfo->dpq_size + 1;
	info("dpq_size = %d", nicInfo->dpq_size);
	futex_unlock(&(nicInfo->dpq_lock));

	return 0;
}

int deq_dpq(int srcId, OOOCore* core, OOOCoreRecorder* cRec, FilterCache* l1d/*MemObject* dest*/) {
	/*
	* deq_dpq - run by nic_core in bbl(). gets the packet latency info from tc_map
	*			uarch access to memobject and record
	*/
	glob_nic_elements* nicInfo = (glob_nic_elements*)gm_get_nic_ptr();
	load_generator* lg_p = (load_generator*)gm_get_lg_ptr();

	info("deq_dpq - dpq size = %d", nicInfo->dpq_size);

	ofstream map_latency_file("map_latency.txt", ios::app);
	//map_latency_file.open("map_latency.txt");


	while (nicInfo->done_packet_q_head != NULL) {
		futex_lock(&(nicInfo->dpq_lock));
		done_packet_info* dp = nicInfo->done_packet_q_head;
		nicInfo->done_packet_q_head = dp->next;
		if (nicInfo->done_packet_q_head == NULL) {
			nicInfo->done_packet_q_tail = NULL;
		}
		futex_unlock(&(nicInfo->dpq_lock));
		
		uint64_t end_cycle = dp->end_cycle;

		/// handle done packet - uarch mem access, lookup map to match ptag and log latency
		///////////////// UARCH MEM ACCESS /////////////////////////
		/*
		MemReq req;
		Address lbuf_lineAddr = dp->lbuf_addr >> lineBits;
		MESIState dummyState = MESIState::I;
		assert((!cRec->getEventRecorder()->hasRecord()));

		if (nicInfo->record_nic_access) {
			req = { lbuf_lineAddr, GETS, 0xDA0000, &dummyState, end_cycle, NULL, dummyState, srcId, 0 };
		}
		else {
			req = { lbuf_lineAddr, GETS, 0xDA0000, &dummyState, end_cycle, NULL, dummyState, srcId, MemReq::NORECORD };
		}

		uint64_t reqSatisfiedCycle = l1d->getParent(lbuf_lineAddr)->access(req);
		cRec->record(end_cycle, end_cycle, reqSatisfiedCycle);
		*/

		//////// get packet latency info from tag-starttime map //////
		uint64_t ptag = dp->tag;
		
		futex_lock(&lg_p->ptc_lock);
		info("reading ptc from map and removing");
		uint64_t start_cycle = (*(lg_p->tc_map))[ptag];
		lg_p->tc_map->erase(ptag);

		nicInfo->dpq_size--;

		futex_unlock(&lg_p->ptc_lock);

		map_latency_file << ptag << ", " << (end_cycle - start_cycle) << std::endl;


	}

	map_latency_file.close();

	info("deq_dpq done - dpq size = %d", nicInfo->dpq_size);

	return 0;
}

void process_wq_entry(wq_entry_t cur_wq_entry, uint64_t core_id, glob_nic_elements* nicInfo)
{
	/*
	* process_wq_entry - handles the wq_entry by calling appropirate action based on OP
	*/
	if (cur_wq_entry.op == RMC_RECV) {
		//TODO:rewrite free_recv_buf_addr for core_nic_api
		//free_recv_buf_addr(cur_wq_entry.buf_addr, core_id);

		//info("RMC_RECV from APP"); // for debug
		free_recv_buf_addr(cur_wq_entry.buf_addr, core_id);
		return;
	}

	if (cur_wq_entry.op == RMC_SEND)
	{
		//TODO - define this somewhere else? decide how to handle nw_roundtrip_delay
		uint64_t nw_roundtrip_delay = 100;

		// TODO - getcycles may not retrun precise cycle (could be in phase granularity). May want something more precise
		uint64_t q_cycle = zinfo->cores[core_id]->getCycles();
		uint64_t rcp_q_cycle = q_cycle + nw_roundtrip_delay;
		uint64_t lbuf_addr = cur_wq_entry.buf_addr;
		uint64_t lbuf_data = *((uint64_t*)lbuf_addr);

		uint64_t ptag = cur_wq_entry.nid;

		//TODO - check what we want to use for timestamp
		log_packet_latency_list(ptag, q_cycle);
		enq_dpq(lbuf_addr, q_cycle, ptag);

		enq_rcp_event(rcp_q_cycle, lbuf_addr, lbuf_data, nicInfo, core_id);
		return;
	}
}

int nic_rgp_action(uint64_t core_id, glob_nic_elements* nicInfo)
{
	/*
	* nic_rgp_action - called when app(core) notifies of new wq_entry
	*       dequeues the wq_entry and processes it (take appropriate action)
	*/
	if (!check_wq(core_id, nicInfo))
	{
		info("nic_rgp_action called but nothing in wq");
		//nothing in wq, return
		return 0;
	}
	wq_entry_t cur_wq_entry = deq_wq_entry(core_id, nicInfo);
	process_wq_entry(cur_wq_entry, core_id, nicInfo);

	return 0;
}


////////////////////////////////////////////////////////////////////

#endif
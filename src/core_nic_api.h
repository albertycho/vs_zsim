//#include "nic_defines.h"
#include "log.h"
#include "ooo_core.h"
#include "ooo_core_recorder.h"
#include "memory_hierarchy.h"

#include "trace_driver.h"
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
	if (q_cycle <= cur_cycle) {
		if(CQ_WR_EV_Q->cqe.success == 0x7f) {
			//nicInfo->nic_elem[core_id].ts_queue[nicInfo->nic_elem[core_id].ts_idx++] = cur_cycle;
			nicInfo->nic_elem[core_id].phase_queue[nicInfo->nic_elem[core_id].phase_idx++] = zinfo->numPhases;
		}
		return true;
	}
	return false;
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
	assert(nicInfo->nic_elem[core_id].ceq_size > 0);
	nicInfo->nic_elem[core_id].ceq_size--;
	futex_unlock(&nicInfo->nic_elem[core_id].ceq_lock);

	return ret;
}

//int add_time_card(p_time_card* ptc, load_generator* lg_p) {
int tc_map_insert(uint64_t in_ptag, uint64_t issue_cycle, uint64_t core_id) {

	uint16_t ptag = (uint16_t) in_ptag;
	load_generator* lg_p = (load_generator*)gm_get_lg_ptr();
	futex_lock(&lg_p->ptc_lock);
	
	if((lg_p->tc_map[ptag].core_id) != 0){
		info("duplicate ptag found");
		glob_nic_elements* nicInfo = (glob_nic_elements*)gm_get_nic_ptr();
		uint64_t sent_p = lg_p->sent_packets;
		uint64_t done_p = nicInfo->latencies_size;
		info("sent: %d, completed: %d",sent_p, done_p)
		info("already have %lld", ptag);
		info("remining rb on current core: %d", nicInfo->remaining_rb[nicInfo->sampling_phase_index-1]);
		info("process_wq_entry called %d",nicInfo->process_wq_entry_count);
		info("free rb called		  %d",nicInfo->free_rb_call_count);
		info("rmc_send_withptag count %d",nicInfo->rmc_send_count);
		info("valid deq_dpqCall count %d",nicInfo->deq_dpq_count);
		info("enq_dpq count      	  %d",nicInfo->enq_dpq_count);
		info("dpq size				  %d", nicInfo->dpq_size);
		info("conseq_validDeqDpqCall  %d",nicInfo->conseq_valid_deq_dpq_count);
		info("delat dpq size          %d",nicInfo->delta_dpq_size);
		for(int iii=0; iii<1000;iii++){
			info("delta_dpq_sizes[%d]: %d",iii, nicInfo->delta_dpq_sizes[iii]);
		}
		panic("already have %lld", ptag);
	}

	timestamp ts;
	ts.core_id = core_id;
	ts.phase = zinfo->numPhases;
	ts.nic_enq_cycle = issue_cycle;
	//ts.bbl = bbl;
	lg_p->tc_map[(uint16_t)ptag] = ts;
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
	if ((cq->tail == cq_head) && (cq->SR != nicInfo->nic_elem[core_id].ncq_SR)) {
		info("FAILED cq->SR == cq->q[cq_head].SR check");
		info("cq_head=%lu",cq_head);
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

	/*
	if (ncq_entry.success==0x7f) {
		uint64_t ptag = ncq_entry.tid;
		uint64_t issue_cycle = cq_wr->q_cycle;
		//add_time_card(ptag, issue_cycle);
		tc_linked_list_insert(ptag, issue_cycle);
	}
	*/

	//info("in process_cq_wr_event - calling put cq entry");

	int put_cq_entry_success = put_cq_entry(ncq_entry, nicInfo, core_id);
	if (put_cq_entry_success == -1)
	{
		return -1;
	}

	//info("in process_cq_wr_event - put cq entry was successful");


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

	if( (cq->tail==cq_head) && (cq->SR!=nicInfo->nic_elem[core_id].ncq_SR) ){
		//info("cq for core %lu is full, curcycle: %lu", core_id, cur_cycle);
		return -1;
	}

	if (cq_wr_event_ready(cur_cycle, nicInfo, core_id))
	{
		//std::cout << std::dec << "wr_event ready @ cycle      :" << cur_cycle << std::endl;
		
		cq_wr_event* cqwrev = deq_cq_wr_event(nicInfo, core_id);
		//dbgprint
		//info("CEQ_size:%d", nicInfo->nic_elem[core_id].ceq_size);

		if (process_cq_wr_event(cqwrev, nicInfo, core_id) != 0)
		{
			panic("cq_entry write failed");
			return -1;
		}
	}
	return 0;
}


uint32_t get_cq_size(uint32_t core_i){
	glob_nic_elements* nicInfo = (glob_nic_elements*)gm_get_nic_ptr();
	uint32_t cq_head = nicInfo->nic_elem[core_i].cq_head;
    uint32_t cq_tail = nicInfo->nic_elem[core_i].cq->tail;
    if (cq_head < cq_tail) {
        cq_head += MAX_NUM_WQ;
	}
    return (cq_head - cq_tail);
}


//functions for interfacing load_generator

int update_loadgen(void* in_lg_p, uint64_t cur_cycle, uint32_t lg_i=0) {
/*
* update_loadgen- updates cycle and tag for load gen
*					packet creation is done in RPCGEN::generatePackedRPC
*/

	load_generator * lg_p = ((load_generator*)in_lg_p);
	load_gen_mod* lgm_p = lg_p->lgs;

	// calculate based on injection rate. interval = phaseLen / injection rate
	//uint64_t interval = ((load_generator*)lg_p)->interval;
	uint64_t interval;
	uint32_t lambda = lg_p->lgs[lg_i].interval;
	double U = drand48();

	switch(lg_p->lgs[lg_i].arrival_dist){
		case 0: //uniform arrival rate
			interval = lg_p->lgs[lg_i].interval;
			break;
		case 1:	//poissson arrival rate
		{	
			errno = 0;
			double temp = log(U);
			while(errno == ERANGE) {
				U = drand48();
				errno = 0;
				temp = log(U);
			}
			interval = (uint64_t) floor((-1) * temp * lambda) + 1;
		}
			break;
		case 2: // burst and rest
			{
				if(lg_p->lgs[lg_i].burst_count < lg_p->lgs[lg_i].burst_len){
					//don't change "next cycle", so we send next packet immediately
					lg_p->lgs[lg_i].burst_count = lg_p->lgs[lg_i].burst_count+1;
					interval = 0;
				}
				else{
					//else reset counter set next cycle s.t. average interval is maintained
					lg_p->lgs[lg_i].burst_count=0;
					interval = (lg_p->lgs[lg_i].burst_len) * (lg_p->lgs[lg_i].interval);
				}

			}
			break;
		case 3: // keep queue_depth
			interval=1000; //set far away, check_cq_depth function in ooo_core.cpp will reset next_cycle


			break;
		default:
			interval = lg_p->lgs[lg_i].interval;
		break;
	}
	//info("interval: %lu", interval);

//	if(lg_p->arrival_dist==1){ //poisson
//		uint32_t lambda = lg_p->interval;
//		double U = drand48();
//		interval = (uint64_t) floor(-log(U) * lambda) + 1;
//	}

	
	glob_nic_elements* nicInfo = (glob_nic_elements*)gm_get_nic_ptr();


	//////// send in loop was a debug feature////////
	if(nicInfo->send_in_loop){
		//info("send_in_loop");
		if(!(lg_p->prev_cycle==0)){
			lg_p->sum_interval = lg_p->sum_interval + (cur_cycle-(lg_p->prev_cycle));
		}
	}
	else{
		//((load_generator*)lg_p)->sum_interval = ((load_generator*)lg_p)->sum_interval + interval;
		//can do same thing for normal sends
		if(!(lg_p->prev_cycle==0)){
		lg_p->sum_interval = lg_p->sum_interval + (cur_cycle-(lg_p->prev_cycle));
		lg_p->lgs[lg_i].sum_interval = lg_p->lgs[lg_i].sum_interval + (cur_cycle-(lg_p->lgs[lg_i].prev_cycle));
		}
	}



	((load_generator*)lg_p)->lgs[lg_i].next_cycle = ((load_generator*)lg_p)->lgs[lg_i].next_cycle + interval;

	((load_generator*)lg_p)->ptag++;

	((load_generator*)lg_p)->sent_packets++;
	lg_p->lgs[lg_i].sent_packets++;
	//for debugging
	uint64_t packet_size=512;//default
	if(nicInfo->forced_packet_size!=0){
		packet_size = nicInfo->forced_packet_size;
	}
	uint64_t total_rbufs = (nicInfo->recv_buf_pool_size)*(lg_p->lgs[lg_i].num_cores) / packet_size;
	if(((lg_p->sent_packets) % total_rbufs)==0){ // iterated through all recv buf - 512(rb count) * 18 (core count)
		info("RB space iterated %d-th time: sampling phase %d", ((lg_p->sent_packets / total_rbufs)),nicInfo->sampling_phase_index);
	}

	if (((load_generator*)lg_p)->sent_packets == ((load_generator*)lg_p)->target_packet_count) {
		((load_generator*)lg_p)->all_packets_sent = true;
		info("all packets sent at sampling phase %d, mem_bw_len: %d", nicInfo->sampling_phase_index, zinfo->mem_bw_len);
	}
	

	((load_generator*)lg_p)->prev_cycle = cur_cycle;
	((load_generator*)lg_p)->lgs[lg_i].prev_cycle = cur_cycle;
	return 0;
}

uint32_t allocate_recv_buf(uint32_t blen, glob_nic_elements* nicInfo, uint32_t core_id, bool wrap_around=false) { 
/*
* allocate_recv_buf - finds free recv buffer from buffer pool and returns head index
*				returns the index of allocated recv buffer, not the address!
*/
	
	//uint32_t head = 0;
	uint32_t head = nicInfo->nic_elem[core_id].rb_iterator;
	while (head < RECV_BUF_POOL_SIZE)
	{
		if (NICELEM.rb_dir[head].in_use == false)
		{
			bool fit = true;
			for (uint32_t i = head; i < head + blen; i++)
			{
				if (i >= RECV_BUF_POOL_SIZE) {
					NICELEM.rb_iterator = 0; //reset iteartor
					if (!wrap_around) {
						return allocate_recv_buf(blen, nicInfo, core_id, true);
					}
					else {
						return RECV_BUF_POOL_SIZE + 1;
					}
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
					NICELEM.rb_dir[i].use_count++;
				}
				NICELEM.rb_iterator = head+blen;

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

	NICELEM.rb_iterator = 0; //reset iteartor
	if (!wrap_around) {
		return allocate_recv_buf(blen, nicInfo, core_id, true);
	}

	return RECV_BUF_POOL_SIZE + 1; // indicate that we didn't find a fit

}

cq_entry_t generate_cqe(uint32_t success, uint32_t tid, uint64_t recv_buf_addr)
{
/*
* generate_cqe - builds a cq entry based on inputs
*/

	//std::cout << "Generating cq entry with success code " << std::hex<<success << " and recv buff addr = " << recv_buf_addr << std::endl;
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
	nicInfo->nic_elem[core_id].ceq_size++;
	futex_unlock(&nicInfo->nic_elem[core_id].ceq_lock);
}



int create_CEQ_entry(uint64_t recv_buf_addr, uint32_t success, uint64_t cur_cycle, glob_nic_elements* nicInfo, uint32_t core_id) {
/*
* create_CEQ_entry - wrapper to generate cq entry, and enq corresponding CEQ entry
*/
	load_generator* lg_p = (load_generator * )gm_get_lg_ptr();
	
	if (core_id > ((zinfo->numCores) - 1)) {
		//info("create_ceq_entry - core_id out of bound: %d", core_id);
	}

	uint64_t ceq_delay = nicInfo->ceq_delay; //TODO: make this programmable
	
	uint32_t tid = lg_p->ptag; //put tid=lg_p->ptag for packet latency tracking. For now, no issues using tid for ptag


	//insert_time_card(lg_p->ptag, cur_cycle, lg_p);
	if (success == 0x7f) {
		uint64_t start_cycle = cur_cycle;
		tc_map_insert(tid, start_cycle, core_id);
	}

	cq_entry_t cqe = generate_cqe(success, tid, recv_buf_addr);
	cq_event_enqueue(cur_cycle + ceq_delay, cqe, nicInfo, core_id);

	return 0;
}

//This function is pretty much replaced by inject incoming packet
int RRPP_routine(uint64_t cur_cycle, glob_nic_elements* nicInfo, void* lg_p, uint32_t core_id) {
	/*Wrapper for the whole RRPP routine*/

	/*
	if (!gm_isready()) return 0;
	if (nicInfo->nic_elem[0].cq_valid == false) return 0;


	*/
	return 0;
}

/* 	core_id: destination core (that will receive the packet)
	srcId: should always be 0 (nic ingress coreId)
	core: ingress nic core handle
	cRec: ingress nic core recorder handle
	l1d: l1d of destination core
*/

void get_IRSR_stat(uint64_t cur_cycle, load_generator* lg_p, glob_nic_elements* nicInfo, uint32_t core_i, uint32_t cq_size ){
		//log IR, SR, cq/ceq size for plotting to profile initila queue buildup
	//if(nicInfo->next_phase_sampling_cycle==0){
	if((nicInfo->next_phase_sampling_cycle==0) && (nicInfo->ready_for_inj==0xabcd)){
		nicInfo->next_phase_sampling_cycle=cur_cycle+1000;
		info("first sampling cycle: %d", nicInfo->next_phase_sampling_cycle);
		nicInfo->last_phase_sent_packets=lg_p->sent_packets;
		nicInfo->last_phase_done_packets=nicInfo->latencies_size;
		nicInfo->last_zsim_pahse = zinfo->numPhases;

		//put 0 data for index 0. To sync phases with mem bw
		nicInfo->sampling_phase_index++; 
		nicInfo->IR_per_phase[0]=0;
		nicInfo->SR_per_phase[0]=0;

	}


	if((cur_cycle > nicInfo->next_phase_sampling_cycle) && (nicInfo->ready_for_inj==0xabcd)){
		if((cur_cycle - (nicInfo->next_phase_sampling_cycle)) > 200){
			//info("cur_cycle is too far ahead of phase sampling cycle by %d", (cur_cycle - (nicInfo->next_phase_sampling_cycle)));
			//info("zsim_phases since last sampling phase: %d", ((zinfo->numPhases) - (nicInfo->last_zsim_pahse)));
		}
		uint32_t ii=nicInfo->sampling_phase_index;
		nicInfo->sampling_phase_index++;
		nicInfo->last_zsim_pahse = zinfo->numPhases;
		nicInfo->IR_per_phase[ii]=lg_p->sent_packets - nicInfo->last_phase_sent_packets;
		nicInfo->SR_per_phase[ii]=nicInfo->latencies_size - nicInfo->last_phase_done_packets;
		nicInfo->last_phase_sent_packets=lg_p->sent_packets;
		nicInfo->last_phase_done_packets=nicInfo->latencies_size;
		nicInfo->cq_size_per_phase[ii]=cq_size;
		nicInfo->ceq_size_per_phase[ii]=nicInfo->nic_elem[core_i].ceq_size;
		nicInfo->lg_clk_slack[ii] = 0;
		nicInfo->remaining_rb[ii] = nicInfo->nic_elem[core_i].rb_left;
		if(zinfo->mem_bw_len>0){
			nicInfo->mem_bw_sampled[ii]=zinfo->mem_bwdth[0][zinfo->mem_bw_len-1];
		}
		
		//dbg
		if (cur_cycle > lg_p->lgs[0].next_cycle) {
			nicInfo->lg_clk_slack[ii] = (cur_cycle) - (lg_p->lgs[0].next_cycle);
		}

		//dbg - remove. only works for sepcific setup (16 cores)
		//assert(core_id > 2);
		//assert(core_id < 19);
		//for (int iii = 3; iii < 19; iii++) {
		//	nicInfo->cq_size_cores_per_phase[iii-3][ii] = get_cq_size(iii);
		//}

		nicInfo->next_phase_sampling_cycle+=1000;
		if(nicInfo->sampling_phase_index < 30){
			//info("sampling phase count: %d", nicInfo->sampling_phase_index);
		}
	}

}

int inject_incoming_packet(uint64_t& cur_cycle, glob_nic_elements* nicInfo, void* lg_p_in, uint32_t core_id, uint32_t srcId, OOOCore* core, OOOCoreRecorder* cRec, FilterCache* l1d, uint16_t level, uint32_t lg_i) {
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

	//info("inject incoming packet called, cur_cycle= %d", cur_cycle);
	load_generator* lg_p = ((load_generator*) lg_p_in);

	uint32_t core_i = lg_p->lgs[lg_i].last_core;
	uint32_t cq_size = get_cq_size(core_i);
	if(nicInfo->sampling_phase_index < 100000 ){ 
		get_IRSR_stat(cur_cycle, lg_p, nicInfo, core_i, cq_size);
	}

	uint32_t herd_msg_size = 512;
	uint32_t packet_size = herd_msg_size;
	
	if(nicInfo->forced_packet_size!=0){
		packet_size = nicInfo->forced_packet_size;
	}

	//if (packet_size % herd_msg_size != 0) {
	//	info("WARNING: packet size is not a multiple of msg size!");
	//}

	futex_lock(&nicInfo->nic_elem[core_id].rb_lock);
	uint32_t rb_head = allocate_recv_buf(packet_size, nicInfo, core_id);		// for mica, allocate 512B 
	nicInfo->nic_elem[core_id].rb_left--;
	//dbgprint
	//info("allocate_recv_buf - rb_head = %d", rb_head);

	futex_unlock(&nicInfo->nic_elem[core_id].rb_lock);

	uint64_t rbuf_count = (nicInfo->recv_buf_pool_size) / (nicInfo->forced_packet_size);
	uint64_t outstanding_rb = rbuf_count - nicInfo->nic_elem[core_id].rb_left;

	if (rb_head > RECV_BUF_POOL_SIZE) {
	//// temporary change to confirm larger rb count runs can sustain higher tp due to resilience to queue buildup
	//if (outstanding_rb > 512 && ((lg_p->lgs[lg_i].arrival_dist) != 3) ) {
	//if  ( (rb_head > RECV_BUF_POOL_SIZE) ||  ((outstanding_rb > 512 && ((lg_p->lgs[lg_i].arrival_dist) != 3))) ) {
		//panic("core %d out of recv buffer, cycle %lu", core_id, cur_cycle);
		/* Try graceful exit */
		if(nicInfo->out_of_rbuf==false){
			//info("core %d out of recv buffer, cycle %lu", core_id, cur_cycle);
			info("core %d getting queue builtup, cycle %lu", core_id, cur_cycle);
		}

		lg_p->all_packets_completed=true;
		nicInfo->out_of_rbuf=true;

		return -1;
	}

	uint64_t recv_buf_addr = (uint64_t)(&(nicInfo->nic_elem[core_id].recv_buf[rb_head]));
	// write message to recv buffer via load generator/RPCGen
	int size = ((load_generator*) lg_p)->lgs[lg_i].RPCGen->generatePackedRPC((char*)(&(nicInfo->nic_elem[core_id].recv_buf[rb_head].line_seg[0])), packet_size);
	update_loadgen(lg_p, cur_cycle, lg_i);

	uint64_t reqSatisfiedCycle = cur_cycle;
	uint64_t temp;
	if (level == 42) {	// ideal ingress
		reqSatisfiedCycle = cur_cycle+1;
	}
	else {
		uint64_t addr = recv_buf_addr;
		uint64_t lsize=0;
		if(size % 64)
			lsize = 1;
		lsize += size/64;

		int i=0;
		while(lsize){
			temp = l1d->store(addr, cur_cycle+i, level, srcId, MemReq::PKTIN) + (level == 3 ? 1 : 0) * L1D_LAT;
			//TODO check what cycles need to be passed to recrod
			cRec->record(cur_cycle+i, cur_cycle+i, temp);
			lsize--;
			addr += 64;
			reqSatisfiedCycle = max(temp, reqSatisfiedCycle);
			//i++;
		}

	}

	//uint64_t ceq_cycle = (uint64_t)(((load_generator*)lg_p)->next_cycle);
	create_CEQ_entry(recv_buf_addr, 0x7f, reqSatisfiedCycle/*cur_cycle*//*ceq_cycle*/, nicInfo, core_id);
	//create_CEQ_entry(recv_buf_addr, 0x7f, 10/*ceq_cycle*/, nicInfo, core_id);

	//TODO may want to pass the reqSatisfiedcycle value back to the caller via updating an argument
	//std::cout << "packet injection completed at " << reqSatisfiedCycle << std::endl;
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
	futex_lock(&nicInfo->nic_elem[core_id].rcp_lock);
	assert(RCP_EQ != NULL);
	rcp_event* ret = RCP_EQ;
	RCP_EQ = RCP_EQ->next;
	futex_unlock(&nicInfo->nic_elem[core_id].rcp_lock);
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

	futex_lock(&nicInfo->nic_elem[core_id].rcp_lock);

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
	futex_unlock(&nicInfo->nic_elem[core_id].rcp_lock);
}

int free_recv_buf(uint32_t head, uint32_t core_id) {
	/*
	* free_recv_buf - called by free_recv_buf_addr.
			Takes the index of the recv_buf to be freed
	*/

	assert(NICELEM.rb_dir[head].is_head);
	assert(NICELEM.rb_dir[head].in_use);
	//dbg print

	uint32_t blen = NICELEM.rb_dir[head].len;

	//info("free_recv_buf - core_id = %d, head = %d, block_len = %d", core_id, head, blen);

	for (uint32_t i = head; i < head + blen; i++) {
		NICELEM.rb_dir[i].in_use = false;
		NICELEM.rb_dir[i].is_head = false;
		NICELEM.rb_dir[i].len = 0;
	}

	nicInfo->nic_elem[core_id].rb_left++;
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

	//dbg count
	futex_lock(&(nicInfo->ptag_dbug_lock));
	nicInfo->free_rb_call_count++;
	futex_unlock(&(nicInfo->ptag_dbug_lock));

	uint64_t buf_base = (uint64_t)(&(NICELEM.recv_buf[0]));
	uint64_t offset = buf_addr - buf_base;
	uint32_t head = (uint32_t)(offset); //divide by size of buffer entry in bytes



	futex_lock(&nicInfo->nic_elem[core_id].rb_lock);
	//info("Free_recv_buf_addr: core_id= %d, head= %d", core_id, head);
	int retval = free_recv_buf(head, core_id);
	futex_unlock(&nicInfo->nic_elem[core_id].rb_lock);
	return retval;
}

int resize_latencies_arr() {
	glob_nic_elements* nicInfo = (glob_nic_elements*)gm_get_nic_ptr();

	uint64_t new_capa = (nicInfo->latencies_capa) * 2;

	uint64_t * new_latencies = gm_calloc<uint64_t>(new_capa);

	memcpy(new_latencies, nicInfo->latencies, (sizeof(uint64_t))*(nicInfo->latencies_size));

	gm_free(nicInfo->latencies);

	nicInfo->latencies = new_latencies;
	nicInfo->latencies_capa = new_capa;
	
	return 0;
}

int insert_latency_stat(uint64_t p_latency) {
	glob_nic_elements* nicInfo = (glob_nic_elements*)gm_get_nic_ptr();
	load_generator* lg_p = (load_generator*)gm_get_lg_ptr();

	if (nicInfo->latencies_size >= nicInfo->latencies_capa) {
		resize_latencies_arr();
	}

	nicInfo->latencies[nicInfo->latencies_size] = p_latency;
	nicInfo->latencies_size = nicInfo->latencies_size + 1;

	if (p_latency > nicInfo->max_latency) {
		nicInfo->max_latency = p_latency;
	}



	//dbgprint
	//info("latencies gathered: %d", nicInfo->latencies_size);

	return 0;
}



int enq_dpq(uint64_t lbuf_addr, uint64_t end_time, uint64_t ptag, uint64_t length) {
	done_packet_info* dpq_entry = gm_calloc<done_packet_info>();
	dpq_entry->end_cycle = end_time;
	dpq_entry->lbuf_addr = lbuf_addr;
	dpq_entry->len = length;
	dpq_entry->ending_phase = zinfo->numPhases;
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

	nicInfo->dpq_size++;
	//info("dpq_size = %d", nicInfo->dpq_size);
	futex_unlock(&(nicInfo->dpq_lock));
	
	///debug count
	futex_lock(&(nicInfo->ptag_dbug_lock));
	nicInfo->enq_dpq_count++;
	futex_unlock(&(nicInfo->ptag_dbug_lock));

	return 0;
}

int deq_dpq(uint32_t srcId, OOOCore* core, OOOCoreRecorder* cRec, FilterCache* l1d/*MemObject* dest*/, uint64_t core_cycle, uint16_t level, uint16_t inval = 0) {
	
	/*
	* deq_dpq - run by nic_core in bbl(). gets the packet latency info from tc_map
	*			uarch access to memobject and record
	*/
	glob_nic_elements* nicInfo = (glob_nic_elements*)gm_get_nic_ptr();
	load_generator* lg_p = (load_generator*)gm_get_lg_ptr();

	//debug 
	nicInfo->delta_dpq_size = nicInfo->dpq_size - nicInfo->last_dpq_size;
	nicInfo->last_dpq_size = nicInfo->dpq_size;
	nicInfo->delta_dpq_sizes[((nicInfo->delta_dpq_index++) % 1000)]=nicInfo->delta_dpq_size;



	futex_lock(&(nicInfo->dpq_lock));
	//while () {
		if(nicInfo->done_packet_q_head != NULL) {
			done_packet_info* dp = nicInfo->done_packet_q_head;
			nicInfo->done_packet_q_head = dp->next;
			if (nicInfo->done_packet_q_head == NULL) {
				nicInfo->done_packet_q_tail = NULL;
			}
			//info("dpq_size = %lld",nicInfo->dpq_size);
			nicInfo->dpq_size--;
			futex_unlock(&(nicInfo->dpq_lock));


			////debug counters
			nicInfo->last_deq_dpq_call_valid=true;
			nicInfo->conseq_valid_deq_dpq_count++;
				
			uint64_t end_cycle = dp->end_cycle;

			/// handle done packet - uarch mem access, lookup map to match ptag and log latency
			///////////////// UARCH MEM ACCESS /////////////////////////

			// GETS to LLC

			//info("starting deq_dpq at cycle %lld", core_cycle);
			uint64_t reqSatisfiedCycle;
			if (level == 42) {
				reqSatisfiedCycle = core_cycle+1;
			} else if (level == 1 && inval == 1) { 	// non-ddio config of modern intel cpus: they snoop the cache for a packet, but also invalidate, so use a GETX and inval the LLC
				uint64_t addr = dp->lbuf_addr;
				uint64_t lsize = dp->len;
				//lsize is in bytes, convert to number of cachelines
				//lsize = lsize / 64;
				//info("lsize: %d", lsize);
				while(lsize){
					reqSatisfiedCycle = l1d->store(addr, core_cycle, level, srcId, MemReq::PKTOUT);				//TODO check what cycles need to be passed to recrod
					cRec->record(core_cycle, core_cycle, reqSatisfiedCycle);
					lsize--;
					addr += 64;
				}
			} else {// ddio: we snoop the cache for the data, but don't invalidate (GETS) + all other cases
				uint64_t addr = dp->lbuf_addr;
				uint64_t lsize = dp->len;
				//info("lsize: %d", lsize);
				//lsize is in bytes, convert to number of cachelines
				//lsize = lsize / 64;
				while(lsize){
					reqSatisfiedCycle = l1d->load(addr, core_cycle, level, srcId, MemReq::PKTOUT) + (level == 3 ? 1 : 0) * L1D_LAT;		//TODO check what cycles need to be passed to recrod
					cRec->record(core_cycle, core_cycle, reqSatisfiedCycle);
					lsize--;
					addr += 64;
				}
			}
			uint64_t lb_addr = dp->lbuf_addr;

			//////// get packet latency info from tag-starttime map //////
			uint64_t ptag = dp->tag;
			uint32_t ending_phase = dp->ending_phase;
			gm_free(dp);
			
			futex_lock(&lg_p->ptc_lock);

			//assert(lg_p->tc_map->count(ptag) > 0);

			timestamp tmstmp = lg_p->tc_map[ptag];
			uint64_t start_cycle = tmstmp.nic_enq_cycle;
			
			uint64_t core_id = tmstmp.core_id;
			uint32_t start_phase = tmstmp.phase;
			//uint32_t start_bbl = (*(lg_p->tc_map))[ptag].bbl;

			//debug
			if(ptag>65536) info("ptag > 65536 in deq_dpq");
			if(lg_p->tc_map[ptag].core_id==0){
				info("deq_dpq: tc_mam[%d] is already empty",ptag);
			}
			///////////////

			//debug count
			futex_lock(&(nicInfo->ptag_dbug_lock));
			nicInfo->deq_dpq_count++;
			futex_unlock(&(nicInfo->ptag_dbug_lock));


			lg_p->tc_map[ptag].core_id=0;
			//lg_p->tc_map_core->erase(ptag);
			//lg_p->tc_map_phase->erase(ptag);


			futex_unlock(&lg_p->ptc_lock);
			
			if (nicInfo->zeroCopy) { // free recv buf and send clean here

				if (nicInfo->clean_recv != 0) {
					//TODO: storeQ and related vars - how to wire into this part
					//ReorderBuffer* storeQueue = core->get_sq_ptr();

					uint64_t size = nicInfo->forced_packet_size;
					size += CACHE_BLOCK_SIZE - 1;
					size >>= CACHE_BLOCK_BITS;  //number of cache lines
					//uint64_t dispatchCycle = core_cycle;
					uint64_t dispatchCycle = reqSatisfiedCycle;

					//uint64_t sqCycle = storeQueue.minAllocCycle();
					uint64_t sqCycle = core->get_sq_minAllocCycle();
					if (sqCycle > dispatchCycle) {
#ifdef LSU_IW_BACKPRESSURE
						core->iw_poisonRange(curCycle, sqCycle, 0x10 /*PORT_4, stores*/, core_id);
#endif
						dispatchCycle = sqCycle;
					}

					// Wait for all previous store addresses to be resolved (not just ours :))
					uint64_t lastStoreAddrCommitCycle = core->get_lastStoreAddrCommitCycle();
					dispatchCycle = MAX(lastStoreAddrCommitCycle + 1, dispatchCycle);

					Address addr = lb_addr;

					//uint64_t reqSatisfiedCycle = dispatchCycle;
					reqSatisfiedCycle = dispatchCycle;
					while (size) {
						reqSatisfiedCycle = max(l1d->clean(addr, dispatchCycle, nicInfo->clean_recv) + L1D_LAT, reqSatisfiedCycle);
						cRec->record(core_cycle, dispatchCycle, reqSatisfiedCycle);
						addr += 64;
						size--;
					}

					uint64_t commitCycle = reqSatisfiedCycle;

					uint64_t lastStoreCommitCycle = core->get_lastStoreCommitCycle();
					core->set_lastStoreCommitCycle(MAX(lastStoreCommitCycle, reqSatisfiedCycle));

					//storeQueue.markRetire(commitCycle);
					core->sq_markRetire(commitCycle);
				}
				free_recv_buf_addr(lb_addr, core_id);
					if(nicInfo->nic_elem[core_id].packet_pending==true) {
						futex_lock(&nicInfo->nic_elem[core_id].packet_pending_lock);
						nicInfo->nic_elem[core_id].packet_pending=false;
						futex_unlock(&nicInfo->nic_elem[core_id].packet_pending_lock);
					}
			}

			//uint32_t span_phase = ending_phase - start_phase + 1;

			auto coreinfo = nicInfo->nic_elem[core_id];

			nicInfo->nic_elem[core_id].phase_nic_queue[nicInfo->nic_elem[core_id].phase_nic_idx++] = start_phase;//span_phase;
			nicInfo->nic_elem[core_id].phase_nic_queue[nicInfo->nic_elem[core_id].phase_nic_idx++] = ending_phase;
			nicInfo->nic_elem[core_id].ts_nic_queue[nicInfo->nic_elem[core_id].ts_nic_idx++] = start_cycle;
			nicInfo->nic_elem[core_id].ts_nic_queue[nicInfo->nic_elem[core_id].ts_nic_idx++] = end_cycle;
			//nicInfo->nic_elem[core_id].bbl_queue[nicInfo->nic_elem[core_id].bbl_idx++] = start_bbl;
			//nicInfo->nic_elem[core_id].bbl_queue[nicInfo->nic_elem[core_id].bbl_idx++] = end_bbl;

			//uint64_t access_latency = reqSatisfiedCycle - core_cycle;
			//uint64_t p_latency = end_cycle + access_latency - start_cycle;	
		
			uint64_t p_latency = end_cycle - start_cycle;
			insert_latency_stat(p_latency);

			/* stop sending after target number of requests are sent and completed */
			//info("target packet count is %lld", lg_p->target_packet_count);
			if (nicInfo->latencies_size == lg_p->target_packet_count) {
				assert(nicInfo->dpq_size==0);
				assert(nicInfo->done_packet_q_head==NULL);
				lg_p->all_packets_completed = true;
				info("all packets received");
			}
		//std::cout << "Packet Tag: " << ptag << ", core "<<core_id << ", start_cycle: " << start_cycle << ", end_cycle: " << end_cycle << ", p_latency: " << p_latency << std::endl;
		
		}
		else {
			futex_unlock(&(nicInfo->dpq_lock));
			nicInfo->last_deq_dpq_call_valid=false;
			nicInfo->conseq_valid_deq_dpq_count=0;
		}
	//}

	return 0;
}

//void process_wq_entry(wq_entry_t cur_wq_entry, uint64_t core_id, glob_nic_elements* nicInfo)
int process_wq_entry(wq_entry_t cur_wq_entry, uint64_t core_id, glob_nic_elements* nicInfo)
{
	//debug count
	futex_lock(&(nicInfo->ptag_dbug_lock));
	nicInfo->process_wq_entry_count++;
	futex_unlock(&(nicInfo->ptag_dbug_lock));
	/*
	* process_wq_entry - handles the wq_entry by calling appropirate action based on OP
	*/
	if (cur_wq_entry.op == RMC_RECV) {
		free_recv_buf_addr(cur_wq_entry.buf_addr, core_id);
		//if(nicInfo->send_in_loop){
		//	assert(nicInfo->nic_elem[core_id].packet_pending==true);
		if(nicInfo->nic_elem[core_id].packet_pending==true) {
			futex_lock(&nicInfo->nic_elem[core_id].packet_pending_lock);
			nicInfo->nic_elem[core_id].packet_pending=false;
			futex_unlock(&nicInfo->nic_elem[core_id].packet_pending_lock);
		}
		
		return 0;
	}

	if (cur_wq_entry.op == RMC_SEND)
	{	
		//debug count
		futex_lock(&(nicInfo->ptag_dbug_lock));
		nicInfo->rmc_send_count++;
		futex_unlock(&(nicInfo->ptag_dbug_lock));
		/*
		if(nicInfo->send_in_loop){
			assert(nicInfo->nic_elem[core_id].packet_pending==true);
			futex_lock(&nicInfo->nic_elem[core_id].packet_pending_lock);
			nicInfo->nic_elem[core_id].packet_pending=false;
			futex_unlock(&nicInfo->nic_elem[core_id].packet_pending_lock);
		}
		*/
		//TODO - define this somewhere else? decide how to handle nw_roundtrip_delay
		uint64_t nw_roundtrip_delay = nicInfo->nw_roundtrip_delay;

		// using new func getCycles_forSynch, which returns the private cur_cycle of the core
		uint64_t q_cycle = ((OOOCore*) (zinfo->cores[core_id]))->getCycles_forSynch();
		uint64_t rcp_q_cycle = q_cycle + nw_roundtrip_delay;
		uint64_t lbuf_addr = cur_wq_entry.buf_addr;
		uint64_t lbuf_data = *((uint64_t*)lbuf_addr);
		uint64_t length = cur_wq_entry.length;

		uint64_t ptag = cur_wq_entry.nid;

		//TODO - check what we want to use for timestamp
		//log_packet_latency_list(ptag, q_cycle);
		enq_dpq(lbuf_addr, q_cycle, ptag, length);

		enq_rcp_event(rcp_q_cycle, lbuf_addr, lbuf_data, nicInfo, core_id);
		return 1;
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
int nic_rgp_action_new(uint64_t core_id, glob_nic_elements* nicInfo)
{
	/*
	* nic_rgp_action - called when app(core) notifies of new wq_entry
	*       dequeues the wq_entry and processes it (take appropriate action)
	*/
	//TODO: this should be called in ooo_core.cpp where "deq_dpq is called"
	//TODO: check if cur_cycle > "next_cycle" (can send if true)
	/*TODO: check wq & porcess wq for all cores
	*/
	/////////////// TODO this is prototype code with variables yet to be defined
	if(nicInfo->egr_interval != 0){
		//if((nicInfo->next_egr_cycle) > cur_cycle){
			if((nicInfo->next_egr_cycle) > 10000000){
			//can't do anything
			return -1;		
		}
	}
	uint64_t sent_packets=0;
	uint64_t pwq_res=0;

	if (!check_wq(core_id, nicInfo))
	{
		info("nic_rgp_action called but nothing in wq");
		//nothing in wq, return
		return 0;
	}
	wq_entry_t cur_wq_entry = deq_wq_entry(core_id, nicInfo);
	pwq_res = process_wq_entry(cur_wq_entry, core_id, nicInfo);
	sent_packets+=pwq_res;


	/*TODO: add interval X number of packets sent to get "cycle egress is allowed to send again"
	 */
	nicInfo->next_egr_cycle+=(nicInfo->egr_interval)*sent_packets;
	return 0;
}

////////////////////////////////////////////////////////////////////

#endif


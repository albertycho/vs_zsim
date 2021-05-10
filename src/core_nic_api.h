#include "nic_defines.h"
#include "log.h"



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
	if (cq_wr_event_ready(cur_cycle, nicInfo, core_id))
	{
		std::cout << std::dec << "wr_event ready @ cycle      :" << cur_cycle << std::endl;

		cq_wr_event* cqwrev = deq_cq_wr_event(nicInfo, core_id);
		//std::cout << "wrevent_q_cycle:" << cqwrev->q_cycle << std::endl;

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
	((load_generator*)lg_p)->next_cycle = ((load_generator*)lg_p)->next_cycle + 10000; 
	//TODO: will do something more sophisticated for setting next_cycle offset 

	return next_message;
}

uint64_t RRPP_allocate_recv_buf(uint32_t blen, glob_nic_elements* nicInfo, uint32_t core_id) { // reusing(modifying) sim_nic.h function
//TODO: go over this, find problems.. might be better to rewrite
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

	uint32_t rb_head =  RECV_BUF_POOL_SIZE + 1;
	uint64_t recv_buf_addr = (uint64_t)(&(nicInfo->nic_elem[0].recv_buf[rb_head]));
	return recv_buf_addr;
}


int inject_inbound_packet(int message, uint64_t recv_buf_addr) { //input is packet, so type may change with code update
	//write to recv buffer TODO: do this in a function?
	//*((uint64_t*)recv_buf_addr) = message;
	//update uarch state (call access)

	//TODO: what to return? 
	return 0;
}

int create_CEQ_entry(uint64_t recv_buf_addr, uint64_t cur_cycle, glob_nic_elements* nicInfo, uint32_t core_id) {
	//TODO: create and enq CEQ entry - reuse functions from sim_nic.h

	return 0;
}

int RRPP_routine(uint64_t cur_cycle, glob_nic_elements* nicInfo, void* lg_p, uint32_t core_id) {
/*Wrapper for the whole RRPP routine*/
	if (check_load_gen(lg_p, cur_cycle)) {
		int message = get_next_message(lg_p);
		uint64_t recv_buf_addr = RRPP_allocate_recv_buf(1, nicInfo, core_id);
		inject_inbound_packet(message, recv_buf_addr);
		create_CEQ_entry(recv_buf_addr, cur_cycle, nicInfo, core_id);

	}

	return 0;
}



/// RCP functions
/////////////////////

bool check_rcp_eq(uint64_t cur_cycle, glob_nic_elements* nicInfo, uint32_t core_id) {
	//check rcp_eq head. similar to checking CEQ head
	
	//if rcp_eq head == NULL return false;
	//if rcp_eq head -> q_cycle <= cur_cycle return true;

	return false;
}

rcp_event deq_rcp_eq(glob_nic_elements* nicInfo, uint32_t core_id) {
	//similar to deq_cq_wr_event
	rcp_event ret;
	return ret;
}

void process_rcp_event(rcp_event nrcp_event) {
	//write response to local buffer
	//create cq entry (CEQ entry)
}

void RCP_routine(uint64_t cur_cycle, glob_nic_elements* nicInfo, uint32_t core_id) {
	if (check_rcp_eq(cur_cycle, nicInfo, core_id)) {
		rcp_event nrcp_event = deq_rcp_eq(nicInfo, core_id);
		process_rcp_event(nrcp_event);
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
#include "nic_defines.h"
#include "log.h"



/// RRPP functions

bool cq_wr_event_ready(uint64_t cur_cycle, glob_nic_elements* nicInfo, uint64_t core_id)
{
	if (CQ_WR_EV_Q == NULL)
	{
		return false;
	}
	uint64_t q_cycle = CQ_WR_EV_Q->q_cycle;
	return q_cycle <= cur_cycle;
}

cq_wr_event* cq_wr_event_dequeue(glob_nic_elements* nicInfo, uint64_t core_id)
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

		cq_wr_event* cqwrev = cq_wr_event_dequeue(nicInfo, core_id);
		//std::cout << "wrevent_q_cycle:" << cqwrev->q_cycle << std::endl;

		if (process_cq_wr_event(cqwrev, nicInfo, core_id) != 0)
		{
			panic("cq_entry write failed");
			return -1;
		}
	}
	return 0;
}





/// RGP functions

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
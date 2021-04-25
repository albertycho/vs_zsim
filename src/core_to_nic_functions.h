#include "nic_defines.h"
#include "log.h"



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
int core_cq_wr_event_action(uint64_t cur_cycle, glob_nic_elements* nicInfo, uint64_t core_id){
//put all required action in one function to keep core.cpp files simpler
	if (cq_wr_event_ready(cur_cycle, nicInfo, core_id))
	{
		std::cout << std::dec << "wr_event ready @ cycle	  :" << cur_cycle << std::endl;

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


void dummy_function(uint64_t cur_cycle) {
	if (cur_cycle % 5000 == 0)
	{
		std::cout << "dummyfunction";
	}
	
}

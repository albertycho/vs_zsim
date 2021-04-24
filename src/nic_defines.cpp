#include "nic_defines.h"
#include "log.h"

void cq_wr_event_enqueue(uint64_t q_cycle, cq_entry_t cqe, glob_nic_elements* nicInfo, uint64_t core_id)
{
	cq_wr_event * cq_wr_e = gm_calloc<cq_wr_event>();
	cq_wr_e->cqe = cqe;
	cq_wr_e->q_cycle = q_cycle;
	cq_wr_e->next = NULL;

	if (nicInfo->cq_wr_event_q[core_id] == NULL)
	{
		nicInfo->cq_wr_event_q[core_id] = cq_wr_e;
	}
	else 
	{
		cq_wr_event* cq_wr_event_q_tail = nicInfo->cq_wr_event_q[core_id];
		while (cq_wr_event_q_tail->next != NULL)
		{
			cq_wr_event_q_tail = cq_wr_event_q_tail->next;
		}
		cq_wr_event_q_tail->next = cq_wr_e;
	}
}

bool cq_wr_event_ready(uint64_t cur_cycle, glob_nic_elements* nicInfo, uint64_t core_id)
{
	if (nicInfo->cq_wr_event_q[core_id] == NULL)
	{
		return false;
	}
	uint64_t q_cycle = nicInfo->cq_wr_event_q[core_id]->q_cycle;
	return q_cycle <= cur_cycle;
}

cq_wr_event * cq_wr_event_dequeue(glob_nic_elements* nicInfo, uint64_t core_id)
{
	assert(nicInfo->cq_wr_event_q[core_id] != NULL);

	cq_wr_event* ret = nicInfo->cq_wr_event_q[core_id];

	nicInfo->cq_wr_event_q[core_id] = nicInfo->cq_wr_event_q[core_id]->next;

	return ret;
}


int put_cq_entry(cq_entry_t ncq_entry, glob_nic_elements* nicInfo, uint64_t core_id)
{
/*separate out function that deals with the head/tail and SR*/
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

	cq_entry_t ncq_entry=cq_wr->cqe;
	int put_cq_entry_success=put_cq_entry(ncq_entry, nicInfo, core_id);
	if (put_cq_entry_success == -1)
	{
		return -1;
	}

	gm_free(cq_wr);
	return 0;
	
}




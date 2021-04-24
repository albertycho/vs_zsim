#include "galloc.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <string>

#include "nic_defines.h"



#ifndef _SIM_NIC_H_
#define _SIM_NIC_H_

//#include "nic_functions.h"

#define SIM_NICELEM 		sim_nicInfo->nic_elem[procIdx]

glob_nic_elements* sim_nicInfo;

/*
void init_nicInfo();
int create_cq_event(uint32_t procIdx, bool SR, uint32_t success, uint32_t tid, uint64_t recv_buf_addr);


int create_cq_entry(uint32_t procIdx, rmc_cq_t* cq, bool SR, uint32_t success, uint32_t tid, uint64_t recv_buf_addr);

//TODO: write this funciton. return type may have to be some type of
//succesStruct
//int poll_wq(uint32_t procIdx, rmc_wq_t * wq){
wq_entry_t poll_wq(uint32_t procIdx, rmc_wq_t* wq);

uint32_t allocate_recv_buf(uint32_t blen, uint32_t procIdx);

int free_recv_buf(uint32_t head, uint32_t procIdx);

int free_recv_buf_addr(uint64_t buf_addr, uint32_t procIdx);

void run_NIC_proc();
*/

//Issue where when functions are moved to .cpp file, compiler won't recognize them. May have to do with SConstruct

//Functions for Queueing write events

cq_entry_t generate_cqe(uint32_t success, uint32_t tid, uint64_t recv_buf_addr)
{
	cq_entry_t cqe;
	cqe.recv_buf_addr = recv_buf_addr;
	cqe.success = success;
	cqe.tid = tid;
	cqe.valid = 1;
	return cqe;

}

void cq_wr_event_enqueue(uint64_t q_cycle, cq_entry_t cqe, glob_nic_elements* nicInfo, uint64_t core_id)
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



void init_nicInfo() {
	sim_nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
}

int create_cq_event(uint32_t procIdx, bool SR, uint32_t success, uint32_t tid, uint64_t recv_buf_addr) {

	return 0;
}

int create_cq_entry(uint32_t procIdx, rmc_cq_t* cq, bool SR, uint32_t success, uint32_t tid, uint64_t recv_buf_addr) {


	uint64_t cq_head = sim_nicInfo->nic_elem[procIdx].cq_head;

	if (cq->SR == cq->q[cq_head].SR) {
		return -1;
	}
	cq_entry_t ncq_entry;
	ncq_entry.valid = 1;
	ncq_entry.SR = SIM_NICELEM.ncq_SR;
	ncq_entry.success = success;
	ncq_entry.tid = tid;
	ncq_entry.recv_buf_addr = recv_buf_addr;

	cq->q[cq_head] = ncq_entry;

	//increment head here? or outside
	cq_head = cq_head + 1;
	if (cq_head >= MAX_NUM_WQ) {
		cq_head = 0;
		//flip SR!
		SIM_NICELEM.ncq_SR = !(SIM_NICELEM.ncq_SR);
		//std::cout<<"NIC - flip cq SR"<<std::endl;
	}
	sim_nicInfo->nic_elem[procIdx].cq_head = cq_head;
	return 0;

}

//TODO: write this funciton. return type may have to be some type of
//succesStruct
//int poll_wq(uint32_t procIdx, rmc_wq_t * wq){
wq_entry_t poll_wq(uint32_t procIdx, rmc_wq_t* wq) {

	//TODO: poll on wq. should we account for open CQ entry at cq_head?
	//TODO: if wq entry op is RMC_RECV, unset valid bit in CQ and free recv buffer - let's do this in a separate function

	wq_entry_t raw_wq_entry = wq->q[SIM_NICELEM.wq_tail];
	while ((raw_wq_entry.valid == 0) || (SIM_NICELEM.nwq_SR != (raw_wq_entry.SR))) {
		raw_wq_entry = wq->q[SIM_NICELEM.wq_tail];
		usleep(500);
	}

	//FIXME: invalidating WQ at NIC for now. 
	//must be updated s.t. app does this when getting cq entry
	wq->q[SIM_NICELEM.wq_tail].valid = 0;

	SIM_NICELEM.wq_tail = SIM_NICELEM.wq_tail + 1;
	if (SIM_NICELEM.wq_tail >= MAX_NUM_WQ) {
		SIM_NICELEM.wq_tail = 0;
		SIM_NICELEM.nwq_SR = !(SIM_NICELEM.nwq_SR);
		//std::cout<<"NIC - flip wq SR "<<std::endl;
	}

	return raw_wq_entry;
}

uint32_t allocate_recv_buf(uint32_t blen, uint32_t procIdx) {
	uint32_t head = 0;
	while (head < RECV_BUF_POOL_SIZE)
	{
		if (SIM_NICELEM.rb_dir[head].in_use == false)
		{
			bool fit = true;
			for (uint32_t i = head; i < head + blen; i++)
			{
				if (i >= RECV_BUF_POOL_SIZE) {
					return RECV_BUF_POOL_SIZE + 1;
				}
				if (SIM_NICELEM.rb_dir[i].in_use == true)
				{
					fit = false;
					if (SIM_NICELEM.rb_dir[i].is_head)
					{
						head = i + SIM_NICELEM.rb_dir[i].len;
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
				SIM_NICELEM.rb_dir[head].is_head = true;
				SIM_NICELEM.rb_dir[head].len = blen;
				for (uint32_t i = head; i < head + blen; i++)
				{
					SIM_NICELEM.rb_dir[i].in_use = true;
				}
				return head;
			}
		}
		else
		{
			if (SIM_NICELEM.rb_dir[head].is_head)
			{
				head = head + SIM_NICELEM.rb_dir[head].len;
			}
			else//should not happen
			{
				std::cout << "ALLOCATE_RECV_BUF BROKEN - IS_HEAD ERROR" << std::endl;
			}

		}

	}

	return RECV_BUF_POOL_SIZE + 1;
}

int free_recv_buf(uint32_t head, uint32_t procIdx) {
	if (SIM_NICELEM.rb_dir[head].is_head == false) {
		return -1;
	}
	if (SIM_NICELEM.rb_dir[head].in_use == false) {
		return -1;
	}

	uint32_t blen = SIM_NICELEM.rb_dir[head].len;
	for (uint32_t i = head; i < head + blen; i++) {
		SIM_NICELEM.rb_dir[i].in_use = false;
		SIM_NICELEM.rb_dir[i].is_head = false;
		SIM_NICELEM.rb_dir[i].len = 0;
	}

	return 0;

}

int free_recv_buf_addr(uint64_t buf_addr, uint32_t procIdx) {
	uint64_t buf_base = (uint64_t)(&(SIM_NICELEM.recv_buf[0]));
	uint64_t offset = buf_addr - buf_base;
	uint32_t head = (uint32_t)(offset / 8);
	//TODO may need debug prints to check offset and head calculation
	return free_recv_buf(head, procIdx);
}



void run_NIC_proc() {

	init_nicInfo();

	int procIdx = 0;
	uint32_t count = 0;
	uint32_t recv_count = 0;
	while (1) {
		if (recv_count > 32) break;
		usleep(100);
		if (!sim_nicInfo->nic_elem[0].wq_valid) continue;
		if (!sim_nicInfo->nic_elem[0].cq_valid) continue;

		//int ncq_success;
		rmc_cq_t* p0_cq = sim_nicInfo->nic_elem[0].cq;
		uint32_t success = 0x7F;
		uint32_t tid = 0xdd0 + count;
		//uint64_t recv_buf_addr=0xee0 + count;

		uint32_t rb_head = allocate_recv_buf(1, procIdx);

		uint64_t recv_buf_addr = (uint64_t)(&(sim_nicInfo->nic_elem[0].recv_buf[rb_head]));
		sim_nicInfo->nic_elem[0].recv_buf[rb_head] = 0xabc0 + count;

		std::cout << "NIC - before filling recv_buf. count=" << count << ", rb_head=" << rb_head << std::endl;
		
		uint64_t q_cycle = 4000000 + (count * 10000000);
		cq_entry_t cqe = generate_cqe(success, tid, recv_buf_addr);
		cq_wr_event_enqueue(q_cycle, cqe, sim_nicInfo, 0);
		//ncq_success = create_cq_entry(procIdx, p0_cq, SIM_NICELEM.ncq_SR, success, tid, recv_buf_addr);
		//if (ncq_success != 0) {
			//std::cout << "NIC: cq entry enqueu failed\t success:" << p0_cq->q[procIdx].success << std::endl;
		//}

		rmc_wq_t* p0_wq = sim_nicInfo->nic_elem[procIdx].wq;
		//while(p0_wq->q[nicInfo->nic_elem[procIdx].wq_tail].valid==0 && (p0_wq->SR)!=(p0_wq->q[nicInfo->nic_elem[procIdx].wq_tail].SR)){
		//	usleep(500);
		//}

		//wq_entry_t cur_wq_entry=p0_wq->q[nicInfo->nic_elem[procIdx].wq_tail];
		//nicInfo->nic_elem[procIdx].wq_tail=nicInfo->nic_elem[procIdx].wq_tail +1;
		//if(nicInfo->nic_elem[procIdx].wq_tail>=MAX_NUM_WQ){
		//	nicInfo->nic_elem[procIdx].wq_tail=0;
		//	//REVERSE SR
		//}


		tid = SIM_NICELEM.wq_tail;
		wq_entry_t cur_wq_entry = poll_wq(procIdx, p0_wq);
		if (cur_wq_entry.op == RMC_RECV) {
			free_recv_buf_addr(cur_wq_entry.buf_addr, procIdx);
		}

		while (cur_wq_entry.op != RMC_SEND) {
			tid = SIM_NICELEM.wq_tail;
			cur_wq_entry = poll_wq(procIdx, p0_wq);
			if (cur_wq_entry.op == RMC_RECV) {
				free_recv_buf_addr(cur_wq_entry.buf_addr, procIdx);
			}

		}


		//std::cout<<"NIC:wq entry- op:"<<cur_wq_entry.op<<std::hex<<", buf_addr:"<<cur_wq_entry.buf_addr<<", cid:"<<cur_wq_entry.cid<<", nid:"<<cur_wq_entry.nid<<std::endl;

		if (cur_wq_entry.op == RMC_SEND)
		{
			recv_count++;
			std::cout << "NIC: wq op is RMC_SEND - lbuf addr:" << std::hex << cur_wq_entry.buf_addr << ", lbuf_val:" << *((uint32_t*)(cur_wq_entry.buf_addr)) << ", recv_count:" << recv_count << std::endl;
			//send cq_entry so APP can invalidate corresponding wq_entry
			//ncq_success=1;
			//while(ncq_success!=0){
			//	ncq_success=create_cq_entry(procIdx, p0_cq, SIM_NICELEM.ncq_SR, 1, tid, 0);
			//}

		}
		count++;
	}

	std::cout << "NIC: nic process about to exit!" << std::endl;

	exit(0);

}


#endif // _SIM_NIC_H_



//		uint8_t wq_head = nicInfo->nic_elem[0].wq->head;
//		//uint8_t wq_tail = nicInfo->nic_elem[0].wq_tail;
//		uint8_t wq_tail = 0;
//		wq_entry_t cur_wq_entry=nicInfo->nic_elem[0].wq->q[wq_tail];
//		while(cur_wq_entry.valid!=1 || cur_wq_entry.SR!=nicInfo->nic_elem[0].wq->SR){
//			cur_wq_entry=nicInfo->nic_elem[0].wq->q[wq_tail];
//			//std::cout<<"cur_wq_entryt.valdi="<<cur_wq_entry.valid<<", cur_wq_entry.SR="<<cur_wq_entry.SR<<",wq->SR="<<nicInfo->nic_elem[0].wq->SR<<std::endl;
//			usleep(500);
//		}
//
//		uint16_t wq_entry_op = cur_wq_entry.op;
//
//		std::cout<<"wq_entry op is "<<std::hex<<wq_entry_op<<std::endl;
//
//		nicInfo->nic_elem[0].cq->q[0].tid=0xab;
//		nicInfo->nic_elem[0].cq->q[0].SR=nicInfo->nic_elem[0].cq->SR;
//
//		break;

		//if(nicInfo->nic_elem[procIdx].wq_head > nicInfo->nic_elem[procIdx].wq_tail){
		//	uint64_t incoming_msg = (uint64_t) nicInfo->nic_elem[procIdx].wq[nicInfo->nic_elem[procIdx].wq_tail];
		//	uint64_t out_msg = incoming_msg+0xaabb0000;
		//	nicInfo->nic_elem[procIdx].cq[nicInfo->nic_elem[procIdx].cq_head] = out_msg;
		//	nicInfo->nic_elem[procIdx].wq_tail++;
		//	nicInfo->nic_elem[procIdx].cq_head++;
		//	count++;
		//}


#include "galloc.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <string>

#include "nic_defines.h"
#include "zsim.h"




#ifndef _SIM_NIC_H_
#define _SIM_NIC_H_


glob_nic_elements* nicInfo;

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


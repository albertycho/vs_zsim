/*
 * dead code collection for possible need to reference/reuse
 */


//SIM_NIC.C
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


//OOO_CORE.CPP
/*
* old code for microarchitectural packet injection
uint64_t packet_rate = nicInfo->packet_injection_rate
//for (uint64_t i = 0; i < packet_rate; i += 8) {
for (uint64_t i = 0; i < packet_rate; i++) {

    int srcId = getCid(tid);

    uint64_t recv_buf_addr = (uint64_t)(&(nicInfo->nic_elem[procIdx].recv_buf[i]));
    nicInfo->nic_elem[procIdx].recv_buf[i] = i;
    //uint64_t reqSatisfiedCycle = core->l1d->store_norecord(recv_buf_addr, core->curCycle)+ L1D_LAT;
    //uint64_t reqSatisfiedCycle = core->l1d->store(recv_buf_addr, core->curCycle)+ L1D_LAT;

    MemReq req;
    Address rbuf_lineAddr = recv_buf_addr >> lineBits;
    MESIState dummyState = MESIState::I;
    assert((!core->cRec.getEventRecorder()->hasRecord()));
    if (nicInfo->record_nic_access) {
        req = { rbuf_lineAddr, GETX, 0xDA0000, &dummyState, core->curCycle, NULL, dummyState, srcId, 0 };
    }
    else {
        req = { rbuf_lineAddr, GETX, 0xDA0000, &dummyState, core->curCycle, NULL, dummyState, srcId, MemReq::NORECORD };
    }

    uint64_t reqSatisfiedCycle = core->l1d->getParent(recv_buf_addr >> lineBits)->access(req);
    //std::cout << core->l1d->getParent(recv_buf_addr >> lineBits)->getName() << std::endl;
    //assert((!core->cRec.getEventRecorder()->hasRecord()));

    core->cRec.record(core->curCycle, core->curCycle, reqSatisfiedCycle);

}
*/
/*
//TODO: DELETE THIS!! experiemnt code for checking L2 access with procMask
if (!nicInfo->nic_proc_on) {
    info("Direct accessing rbuf_addr var");
    for (int i = 0; i < 2; i++) {
        nicInfo->nic_elem[i].cq->q[0].recv_buf_addr = 0xABCD;
        Address rbuf_addr = (Address)(&(nicInfo->nic_elem[i].cq->q[0].recv_buf_addr));
        Address rbuf_lineAddr = rbuf_addr >> lineBits;
        MESIState dummyState = MESIState::I;
        assert((!core->cRec.getEventRecorder()->hasRecord()));
        //MemReq req = { rbuf_lineAddr, GETX, 0xDA0000, &dummyState, core->curCycle, NULL, dummyState, 0, MemReq::NORECORD };
        int srcId = getCid(tid);

        MemReq req = { rbuf_lineAddr, GETX, 0xDA0000, &dummyState, core->curCycle, NULL, dummyState, srcId, 0 };
        uint64_t reqSatisfiedCycle = core->l1d->getParent(rbuf_addr >> lineBits)->access(req);
        //std::cout << core->l1d->getParent(recv_buf_addr >> lineBits)->getName() << std::endl;
        //assert((!core->cRec.getEventRecorder()->hasRecord()));
        core->cRec.record(core->curCycle, core->curCycle, reqSatisfiedCycle);
    }


    nicInfo->nic_proc_on = true;
}
*/
/*
int message = get_next_message(lg_p);
uint32_t rb_head = allocate_recv_buf(8, nicInfo, core_iterator);

if (rb_head > RECV_BUF_POOL_SIZE) {
    info("core %d out of recv buffer", core_iterator);
    break;
}

uint64_t recv_buf_addr = (uint64_t)(&(nicInfo->nic_elem[core_iterator].recv_buf[rb_head]));

// write message to recv buffer
nicInfo->nic_elem[core_iterator].recv_buf[rb_head] = message;


MemReq req;
Address rbuf_lineAddr = recv_buf_addr >> lineBits;
MESIState dummyState = MESIState::I;
assert((!core->cRec.getEventRecorder()->hasRecord()));
if (nicInfo->record_nic_access) {
    req = { rbuf_lineAddr, GETX, 0xDA0000, &dummyState, core->curCycle, NULL, dummyState, srcId, 0 };
}
else {
    req = { rbuf_lineAddr, GETX, 0xDA0000, &dummyState, core->curCycle, NULL, dummyState, srcId, MemReq::NORECORD };
}

uint64_t reqSatisfiedCycle = core->l1d->getParent(recv_buf_addr >> lineBits)->access(req);
//std::cout << core->l1d->getParent(recv_buf_addr >> lineBits)->getName() << std::endl;
//assert((!core->cRec.getEventRecorder()->hasRecord()));

core->cRec.record(core->curCycle, core->curCycle, reqSatisfiedCycle);

//create CEQ entry
uint64_t ceq_cycle = (uint64_t)(((load_generator*)lg_p)->next_cycle);
create_CEQ_entry(recv_buf_addr, 0x7f, ceq_cycle, nicInfo, core_iterator);
*/



//CORE_NIC_API.H
/*
//This is an older version fo RRPP routine. Shall remove after proper version is stable
int RRPP_routine_backup(uint64_t cur_cycle, glob_nic_elements* nicInfo, void* lg_p, uint32_t core_id) {
    //Wrapper for the whole RRPP routine
    if (!gm_isready()) return 0;
    if (nicInfo->nic_elem[0].cq_valid == false) return 0;

    if (check_load_gen(lg_p, cur_cycle)) {
        int message = get_next_message(lg_p);
        uint32_t rb_head = allocate_recv_buf(1, nicInfo, core_id);
        uint64_t recv_buf_addr = (uint64_t)(&(nicInfo->nic_elem[0].recv_buf[rb_head]));
        inject_inbound_packet(message, recv_buf_addr);
        create_CEQ_entry(recv_buf_addr, 0x7f, cur_cycle, nicInfo, core_id);

    }

    return 0;
}
*/


//QP_TEST.CPP
//////////////////////////////////////////////////
//////// CHECK ONLY RGP&RCP - BEGIN///////////////
//////////////////////////////////////////////////
/*
while (send_serviced <= 32) {

    int send_ret;
    //FIXME: figure out what to do with msg_entry_size
    uint64_t msg_entry_size = 1;
    lbuf_ptr = lbuf_base + send_count;
    //std::cout<<"APP: lbuf_Ptr="<<lbuf_ptr<<std::endl;
    *lbuf_ptr = 0xabcd0 + send_count;
    do {
        send_ret = rmc_hw_send(wq, ctx_id, lbuf_ptr, msg_entry_size, 1);
    } while (send_ret);
    send_count++;

    successStruct recv_completion;
    do {
        recv_completion = rmc_check_cq(wq, cq);
    } while (recv_completion.op != (RMC_INCOMING_RESP));

    std::cout << "APP:cq_resp:" << std::hex << *(uint64_t*)(recv_completion.recv_buf_addr) << std::endl;

    send_serviced++;
}
return 0;
//////////////////////////////////////////////////
//////// CHECK ONLY RGP&RCP - END/////////////////
//////////////////////////////////////////////////
*/
//////////////////////////////////////////////////
//////// CHECK ONLY RRPP - BEGIN//////////////////
//////////////////////////////////////////////////
/*
while (send_serviced <= 32)
{
    successStruct recv_completion;
    do {
        recv_completion = rmc_check_cq(wq, cq);
    } while (recv_completion.op != (RMC_INCOMING_SEND));

    //std::cout<<"APP - recv_completion.op="<<recv_completion.op<<std::endl;
    std::cout << "APP: recvd incoming msg.              recv_count:" << std::dec << send_serviced << ", rbuf_addr:" << std::hex << recv_completion.recv_buf_addr << ", rbuf_val:" << *(uint32_t*)(recv_completion.recv_buf_addr) << std::endl;

    send_serviced++;
    rmc_hw_recv(wq, ctx_id, (void*)recv_completion.recv_buf_addr, msg_entry_size);
}

//register_buffer((void*)0, (void*)0xdead);
return 0;

//////////////////////////////////////////////////
//////// CHECK ONLY RRPP - END////////////////////
//////////////////////////////////////////////////

*/
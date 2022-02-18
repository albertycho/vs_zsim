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
uint32_t assign_core(uint32_t in_core_iterator=0) {
    glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
    uint64_t min_q_size = ~0;//RECV_BUF_POOL_SIZE; // assign some very large number
    uint32_t ret_core_id = in_core_iterator;
    uint32_t core_iterator = in_core_iterator;

    uint32_t numCores = zinfo->numCores; //(nicInfo->expected_core_count + 2);

    //increment it once at beginning for round-robin fairness
    core_iterator++;
    if (core_iterator >= numCores) {
        core_iterator = 0;
    }

    for (uint32_t i = 0; i < numCores; i++) {
        if (nicInfo->nic_elem[core_iterator].cq_valid == true) {
            uint32_t cq_head = nicInfo->nic_elem[core_iterator].cq_head;
            uint32_t cq_tail = nicInfo->nic_elem[core_iterator].cq->tail;
            if (cq_head < cq_tail) {
                cq_head += MAX_NUM_WQ;
            }
            uint32_t cq_size = cq_head - cq_tail;
            if ((nicInfo->nic_elem[core_iterator].ceq_size + cq_size) < min_q_size) {
                ret_core_id = core_iterator;
                min_q_size = nicInfo->nic_elem[core_iterator].ceq_size + cq_size;
                if (min_q_size == 0) {
                    //info("ret_core_id: %d", ret_core_id);
                    return ret_core_id;
                }
            }

        }

        core_iterator++;
        if (core_iterator >= numCores) {
            core_iterator = 0;
        }

    }
//    info("ret_core_id: %d, min_cq_size: %lu, min_ceq_size:%lu", ret_core_id, min_q_size-nicInfo->nic_elem[ret_core_id].ceq_size, nicInfo->nic_elem[ret_core_id].ceq_size);
    return ret_core_id;

}
*/
/*
int OOOCore::nic_ingress_routine(THREADID tid) {

    OOOCore* core = static_cast<OOOCore*>(cores[tid]);

    glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
    void* lg_p = static_cast<void*>(gm_get_lg_ptr());
    uint64_t packet_rate = nicInfo->packet_injection_rate;

    if (((load_generator*)lg_p)->next_cycle == 0) {
        ((load_generator*)lg_p)->next_cycle = core->curCycle;
        nicInfo->sim_start_time = std::chrono::system_clock::now();
        info("starting sim time count");

    }
    uint32_t core_iterator = 0;

    uint32_t inject_fail_counter = 0;

    for (uint64_t i = 0; i < packet_rate; i++) {

        core_iterator++;
        if (core_iterator >= zinfo->numCores) {
            core_iterator = 0;
        }

        core_iterator = assign_core(((load_generator*)lg_p)->last_core);
        ((load_generator*)lg_p)->last_core = core_iterator;

        uint32_t srcId = getCid(tid);
        uint64_t injection_cycle = core->curCycle;
        int inj_attempt = inject_incoming_packet(injection_cycle, nicInfo, lg_p, core_iterator, srcId, core, &(core->cRec), l1d_caches[core_iterator], core->ingr_type);

        if (inj_attempt == -1) {
            //core out of recv buffer. stop injecting for this phase
            inject_fail_counter++;
            if (inject_fail_counter >= (nicInfo->registered_core_count - 1)) {
                break;
            }
        }

    }

    return 0;
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

//checking recv_buf access from NIC and app, from:
//ooo_core.cpp line 277
/*
//TODO: remove experiment code
if (addr == (Address)(&(nicInfo->nic_elem[0].cq->q[0].recv_buf_addr))) {
    if (nicInfo->nic_proc_on) {
        std::cout << "DA to rbuf_addr var has been made by NIC before" << std::endl;
    }
    std::cout << "recv_buf[0] access  time for APP:" << (reqSatisfiedCycle - dispatchCycle) << std::endl;
    std::cout << "procMask: " << procMask << std::endl;
}
if (addr == (Address)(&(nicInfo->nic_elem[1].cq->q[0].recv_buf_addr))) {
    if (nicInfo->nic_proc_on) {
        std::cout << "DA to rbuf_addr var has been made by NIC before" << std::endl;
    }
    std::cout << "recv_buf access[1]  time for APP:" << (reqSatisfiedCycle - dispatchCycle) << std::endl;
    std::cout << "procMask: " << procMask << std::endl;
}
*/

        //TODO:: load balancing for choosing core
        /* find next valid core that is still running */
        /*
        int drop_count = 0;
        while (!(nicInfo->nic_elem[core_iterator].cq_valid)) {
            core_iterator++;
            if (core_iterator >= zinfo->numCores) {
                core_iterator = 0;
            }
            drop_count++;
            if (drop_count > (nicInfo->expected_core_count)) { 
                std::cout << "other cores deregistered NIC" << std::endl;
                break;
            }
            //DBG code
            if (core_iterator >= zinfo->numCores) {
                info("nic_ingress_routine (line803) - core_iterator out of bound: %d, cycle: %lu", core_iterator, core->curCycle);
            }
        }
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



//int tc_linked_list_insert(uint64_t ptag, uint64_t issue_cycle) {
//	/*
//	 * we won't use list so this is obsolete, keeping for possible comparison
//	 */
//
//	load_generator* lg_p = (load_generator*)gm_get_lg_ptr();
//
//
//	p_time_card* ptc = gm_calloc<p_time_card>();
//	ptc->issue_cycle = issue_cycle;
//	ptc->ptag = ptag;
//
//	if (lg_p->ptc_head == NULL)
//	{
//		lg_p->ptc_head = ptc;
//		return 0;
//	}
//
//	uint64_t tcount = 0;
//
//	futex_lock(&lg_p->ptc_lock);
//	p_time_card* head = lg_p->ptc_head;
//	while (head->next != NULL) {
//		head = head->next;
//		tcount++;
//	}
//	head->next = ptc;
//	//info("insert ptc pcount = %d", tcount);
//	futex_unlock(&lg_p->ptc_lock);
//	return 0;
//
//}

//int log_packet_latency_list(uint64_t ptag, uint64_t fin_time) {
//	/*
//	* we won't use list so this is obsolete, keeping for possible comparison
//	*/
//
//	load_generator* lg_p = (load_generator*)gm_get_lg_ptr();
//
//	ofstream list_latency_file("list_latency.txt", ios::app);
//
//	uint64_t tcount = 0;
//
//	assert(lg_p->ptc_head != NULL);
//
//	futex_lock(&lg_p->ptc_lock);
//	p_time_card* tmp = lg_p->ptc_head;
//
//	if (tmp->ptag == ptag) {
//		lg_p->ptc_head = tmp->next;
//	}
//	else {
//		p_time_card* prev = tmp;
//		tmp = tmp->next;
//		while (tmp->ptag != ptag) {
//			tcount++;
//
//			prev = tmp;
//			tmp = tmp->next;
//			//DBG
//			if (tmp == NULL) {
//				info("ptag=%d", ptag);
//			}
//			assert(tmp != NULL);
//		}
//
//		prev->next = tmp->next;
//
//	}
//	uint64_t latency = fin_time - tmp->issue_cycle;
//	//info("log packet latency pcount = %d ,ptag = %d, latency = %d", tcount, ptag, latency);
//	futex_unlock(&lg_p->ptc_lock);
//
//	//uint64_t latency = fin_time - tmp->issue_cycle;
//
//	//LOG latency
//	//out >> "tag= " >> tmp->ptag >> ", lat= " >> latency >> std::endl;
//	list_latency_file << ptag << ", " << (fin_time - (tmp->issue_cycle)) << std::endl;
//
//	list_latency_file.close();
//	//FREE ptc pointer
//	gm_free(tmp);
//	return 0;
//}



/*
	MemReq req;
	Address rbuf_lineAddr = recv_buf_addr >> lineBits;
	MESIState dummyState = MESIState::I;
	assert((!cRec->getEventRecorder()->hasRecord()));

	if (nicInfo->record_nic_access) {
		req = { rbuf_lineAddr, GETX, 0xDA0000, &dummyState, cur_cycle, NULL, dummyState, srcId, MemReq::PKTIN };
	}
	else {
		req = { rbuf_lineAddr, GETX, 0xDA0000, &dummyState, cur_cycle, NULL, dummyState, srcId, MemReq::NORECORD | MemReq::PKTIN };
	}



	uint64_t reqSatisfiedCycle = l1d->getParent(rbuf_lineAddr)->access(req);
*/

/*		
		MemReq req;
		Address lbuf_lineAddr = dp->lbuf_addr >> lineBits;
		MESIState dummyState = MESIState::I;
		assert((!cRec->getEventRecorder()->hasRecord()));


		//TODO: using GETS causes crash... why? and is it okay to use GETX?
		if (nicInfo->record_nic_access) {
			req = { lbuf_lineAddr, GETX, 0xDA0000, &dummyState, core_cycle, NULL, dummyState, srcId, MemReq::PKTOUT };
		}
		else {
			req = { lbuf_lineAddr, GETX, 0xDA0000, &dummyState, core_cycle, NULL, dummyState, srcId, MemReq::NORECORD | MemReq::PKTOUT};
		}

		uint64_t reqSatisfiedCycle = l1d->getParent(lbuf_lineAddr)->access(req);
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


/*
ZSIM HARNESS
/*
    /// latency stat output
    info("writing to map_latency_file");
    std::ofstream map_latency_file("map_latency.txt");


    assert(nicInfo->hist_interval != 0);
    uint64_t hist_width = ((nicInfo->max_latency) / (nicInfo->hist_interval)) + 1;
    //uint64_t* hist_counters = new uint64_t[hist_width];
    //uint64_t* sorted_latencies = new uint64_t[nicInfo->latencies_size];
    //using new or normal calloc causes segfault when going over 10000 packets with herd. using gm_calloc again..
    uint64_t* hist_counters = gm_calloc<uint64_t>(hist_width);
    uint64_t * sorted_latencies = gm_calloc<uint64_t>(nicInfo->latencies_size);

	uint64_t latency_sum=0;
	uint64_t service_time_sum=0;

    uint64_t * sorted_service_time_all = gm_calloc<uint64_t>(nicInfo->latencies_size);

    //gives me garbage if I don't clear
    for (uint64_t iii = 0; iii < hist_width; iii++) {
        hist_counters[iii] = 0;
    }

    for (uint64_t iii = 0; iii < nicInfo->latencies_size; iii++) {
        map_latency_file << nicInfo->latencies[iii] << std::endl;
        uint64_t tmp_index = (nicInfo->latencies[iii] / nicInfo->hist_interval);
        hist_counters[tmp_index]++;

        bool insert_at_begin = true;
        for (uint64_t jjj = iii; jjj > 0; jjj--) {
            if (jjj == 0) {
                info("jjj not supposed to be 0");
            }
            if (sorted_latencies[jjj - 1] > nicInfo->latencies[iii]) {
                sorted_latencies[jjj] = sorted_latencies[jjj - 1];
            }
            else {
                sorted_latencies[jjj] = nicInfo->latencies[iii];
                insert_at_begin = false;
                break;
            }
        }
        if (insert_at_begin) {
            sorted_latencies[0] = nicInfo->latencies[iii];
        }
		latency_sum+=nicInfo->latencies[iii];
    }

    map_latency_file.close();

	uint64_t latency_mean = latency_sum / (nicInfo->latencies_size);

    info("writing to latency_hist file");
    std::ofstream latency_hist_file("latency_hist.txt");

    uint64_t median_index = (nicInfo->latencies_size) / 2;
    uint64_t percentile_80_index = ((nicInfo->latencies_size) * 80) / 100;
    uint64_t percentile_90_index = ((nicInfo->latencies_size) * 90) / 100;
    uint64_t percentile_95_index = ((nicInfo->latencies_size) * 95) / 100;
    uint64_t percentile_99_index = ((nicInfo->latencies_size) * 99) / 100;

    latency_hist_file << "mean        : " << latency_mean << std::endl;
    latency_hist_file << "median        : " << sorted_latencies[median_index] << std::endl;
    latency_hist_file << "80-percentile : " << sorted_latencies[percentile_80_index] << std::endl;
    latency_hist_file << "90-percentile : " << sorted_latencies[percentile_90_index] << std::endl;
    latency_hist_file << "95-percentile : " << sorted_latencies[percentile_95_index] << std::endl;
    latency_hist_file << "99-percentile : " << sorted_latencies[percentile_99_index] << std::endl;

    latency_hist_file << std::endl;

    for (uint64_t iii = 0; iii < hist_width; iii++) {
        latency_hist_file << iii * nicInfo->hist_interval << "," << hist_counters[iii] << "," << std::endl;
    }
    latency_hist_file.close();

/////////////////////////////////////////
/*
	info("start writing to service time file");
    std::ofstream service_time_file("service_times.txt");
    std::ofstream st_stat_file("service_times_stats.txt");
    std::ofstream cq_spin_stat_file("cq_check_spin_count.txt");

	uint64_t total_iter_count=0;
	for(uint64_t kkk = 3; kkk<nicInfo->expected_core_count+3;kkk++){
		service_time_file << "CORE " << kkk-1 << std::endl;
		st_stat_file << "CORE " << kkk-1 << std::endl;
		cq_spin_stat_file << "CORE " << kkk-1 << std::endl;
		cq_spin_stat_file << "rmc_checkcq_spin: "<<nicInfo->nic_elem[kkk].cq_check_spin_count << std::endl;
		cq_spin_stat_file << "inner_loop_spin : "<< nicInfo->nic_elem[kkk].cq_check_inner_loop_count<<std::endl;
        cq_spin_stat_file << "outer_loop_spin : " << nicInfo->nic_elem[kkk].cq_check_outer_loop_count << std::endl;
		
		uint32_t sorted_service_time[65000]; //fixed number for now
		for (uint64_t lll = 0; lll<nicInfo->nic_elem[kkk].st_size+10;lll++){
			sorted_service_time[lll] = 0;
		}

	    for (uint64_t iii = 0; iii < nicInfo->nic_elem[kkk].st_size; iii++){
			service_time_file << nicInfo->nic_elem[kkk].service_times[iii]<<std::endl;

			bool insert_at_begin = true;
        	for (uint64_t jjj = iii; jjj > 0; jjj--) {
        	    if (jjj == 0) {
        	        info("jjj not supposed to be 0");
        	    }
        	    if (sorted_service_time[jjj - 1] > nicInfo->nic_elem[kkk].service_times[iii]) {
        	        sorted_service_time[jjj] = sorted_service_time[jjj - 1];
        	    }
        	    else {
        	        sorted_service_time[jjj] = nicInfo->nic_elem[kkk].service_times[iii];
        	        insert_at_begin = false;
        	        break;
        	    }
        	}
        	if (insert_at_begin) {
        	    sorted_service_time[0] = nicInfo->nic_elem[kkk].service_times[iii];
        	}

        	bool insert_at_begin_for_all = true;
        	for (uint64_t jjj = total_iter_count; jjj > 0; jjj--) {
        	    if (jjj == 0) {
        	        info("jjj not supposed to be 0");
        	    }
        	    if (sorted_service_time_all[jjj - 1] > nicInfo->nic_elem[kkk].service_times[iii]) {
        	        sorted_service_time_all[jjj] = sorted_service_time_all[jjj - 1];
        	    }
        	    else {
        	        sorted_service_time_all[jjj] = nicInfo->nic_elem[kkk].service_times[iii];
        	        insert_at_begin_for_all = false;
        	        break;
        	    }
        	}
        	if (insert_at_begin_for_all) {
        	    sorted_service_time_all[0] = nicInfo->nic_elem[kkk].service_times[iii];
        	}


			total_iter_count++;
			service_time_sum+=nicInfo->nic_elem[kkk].service_times[iii];
		}
		int med_index = nicInfo->nic_elem[kkk].st_size / 2;
		int index_80 = nicInfo->nic_elem[kkk].st_size * 80 / 100;
		int index_90 = nicInfo->nic_elem[kkk].st_size * 90 / 100;
		st_stat_file << "median: " << sorted_service_time[med_index] << std::endl;
		st_stat_file << "80perc: " << sorted_service_time[index_80] << std::endl;
		st_stat_file << "90perc: " << sorted_service_time[index_90] << std::endl;

	}

	int med_index = total_iter_count / 2 ;
	int index_80 = total_iter_count * 80 / 100 ;
	int index_90 = total_iter_count * 90 / 100 ;
	int index_95 = total_iter_count * 95 / 100 ;
	int index_99 = total_iter_count * 99 / 100 ;

	uint64_t service_time_mean = service_time_sum / total_iter_count;

	st_stat_file << "ALL CORE STAT" << std::endl;
	st_stat_file << "mean: " << service_time_mean << std::endl;
	st_stat_file << "median: " << sorted_service_time_all[med_index] << std::endl;
	st_stat_file << "80perc: " << sorted_service_time_all[index_80] << std::endl;
	st_stat_file << "90perc: " << sorted_service_time_all[index_90] << std::endl;
	st_stat_file << "95perc: " << sorted_service_time_all[index_95] << std::endl;
	st_stat_file << "99perc: " << sorted_service_time_all[index_99] << std::endl;


	service_time_file.close();
	st_stat_file.close();
	cq_spin_stat_file.close();



*/

// warmup experiment code
	// if(nicInfo->warmup_phase==1){
	// 	//we can tolerate 2 packets queued up
	// 	if(cq_size < 3){
	// 		for(uint32_t ii=0; ii<lg_p->num_loadgen;ii++){
	// 			lg_p->lgs[ii].next_cycle = cur_cycle;
	// 		}
	// 		nicInfo->warmup_phase=2;
	// 		nicInfo->warmup_packets=lg_p->sent_packets;
	// 		info("Warmup packet count: %d",nicInfo->warmup_packets);
	// 		//CALL dump zsim.out here instead of at the magic op

	// 	}
	// 	else{
	// 		//let it drain more
	// 		return -3;
	// 	}
	// }

	// if(nicInfo->warmup_phase==0){
	// 	if(cq_size>=50){
	// 		//we're warm now. move on to drain phase
	// 		nicInfo->warmup_phase=1;
	// 		info("Moving to drain phase");
	// 	}
	// 	else{
	// 		if(nicInfo->nic_elem[core_i].ceq_size > 50){
	// 			//we don't want to run out of recv buffers during warmup
	// 			return -2;
	// 		}
	// 	}		
	// }

	//if (nicInfo->latencies_size == lg_p->target_packet_count) {
	//	lg_p->all_packets_sent = true;
	//	info("all packets sent");
	//}
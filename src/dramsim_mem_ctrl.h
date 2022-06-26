/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DRAMSIM_MEM_CTRL_H_
#define DRAMSIM_MEM_CTRL_H_

#include <map>
#include <string>
#include "g_std/g_string.h"
#include "memory_hierarchy.h"
#include "pad.h"
#include "stats.h"
#include "zsim.h"

namespace DRAMSim {
    class MultiChannelMemorySystem;
};

class DRAMSimAccEvent;

class DRAMSimMemory : public MemObject { //one DRAMSim controller
    private:
        g_string name;
        uint32_t minLatency;
        uint32_t domain;

        DRAMSim::MultiChannelMemorySystem* dramCore;

        std::multimap<uint64_t, DRAMSimAccEvent*> inflightRequests;

        uint64_t curCycle; //processor cycle, used in callbacks

        // R/W stats
        PAD();
        Counter profReads;
        Counter profWrites;
        Counter profTotalRdLat;
        Counter profTotalWrLat;
        Counter profAccs;
        Counter total_access_count, dirty_evict_ing, dirty_evict_egr, dirty_evict_app, nic_ingr_get;
        PAD();

    public:
        DRAMSimMemory(std::string& dramTechIni, std::string& dramSystemIni, std::string& outputDir, std::string& traceName, uint32_t capacityMB,
                uint64_t cpuFreqHz,  uint32_t _minLatency, uint32_t _domain, const g_string& _name);

        const char* getName() {return name.c_str();}

        void initStats(AggregateStat* parentStat);

        // Record accesses
        uint64_t access(MemReq& req);

        // Event-driven simulation (phase 2)
        uint32_t tick(uint64_t cycle);
        void enqueue(DRAMSimAccEvent* ev, uint64_t cycle);

    private:
        void DRAM_read_return_cb(uint32_t id, uint64_t addr, uint64_t returnCycle);
        void DRAM_write_return_cb(uint32_t id, uint64_t addr, uint64_t returnCycle);
};

//DRAMSIM does not support non-pow2 channels, so:
// - Encapsulate multiple DRAMSim controllers
// - Fan out addresses interleaved across banks, and change the address to a "memory address"
class SplitAddrMemory : public MemObject {
    private:
        const g_vector<MemObject*> mems;
        const g_string name;
    public:
        SplitAddrMemory(const g_vector<MemObject*>& _mems, const char* _name) : mems(_mems), name(_name) {}

        uint64_t access(MemReq& req) {
            Address addr = req.lineAddr;
            uint32_t mem = addr % mems.size();
            Address ctrlAddr = addr/mems.size();
            req.lineAddr = ctrlAddr;
            uint64_t respCycle = mems[mem]->access(req);
            req.lineAddr = addr;
            glob_nic_elements* nicInfo = static_cast<glob_nic_elements*>(gm_get_nic_ptr());
            if(nicInfo->zeroCopy){
                if(req.is(MemReq::INGR_EVCT)){
                    futex_lock(&(nicInfo->txts_lock));
                        auto tx_ts_pair = nicInfo->txts_map.find(addr);
                        if(tx_ts_pair ==nicInfo->txts_map.end()){
                            info("ZCP - @ RX buffer evict, no TX timestamp found");
                        }
                        else{
                            uint64_t tx_ts = tx_ts_pair->second;
                            if(tx_ts > respCycle){
                                info("Warning: tx cycle > eviction cycle for RX Buffer in ZCP");
                            }
                            nicInfo->tx2ev[nicInfo->tx2ev_i] = respCycle - tx_ts;
                            nicInfo->tx2ev_i = nicInfo->tx2ev_i+1;
                        }
                    futex_unlock(&(nicInfo->txts_lock));
                }
            }
            return respCycle;
        }

        const char* getName() {
            return name.c_str();
        }

        void initStats(AggregateStat* parentStat) {
            for (auto mem : mems) mem->initStats(parentStat);
        }
};

#endif  // DRAMSIM_MEM_CTRL_H_

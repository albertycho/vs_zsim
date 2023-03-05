#include <cstdint>
#include <cstdio>
#include <assert.h>

#define PF_WAYS 16
#define PF_SETS 128 //2K entries
#define MAX_DEPTH 64 //64lines per page
#define F_DEPTH 4

typedef struct bpf_entry {
	uint64_t tag;
	uint64_t page_bitvector; //footprint
	uint64_t recent_access_t; // log recent access time for simplicity. in hw, can be done in 4 bits(16ways)
}bpf_entry;

typedef struct pending_entry {
	uint64_t tag;
	uint64_t page_bitvector; //footprint
	uint64_t set_index;
	uint64_t count_down;
	uint64_t added_time;
}pending_entry;

class b_prefetcher {
public:
	bpf_entry pf_lut[PF_SETS][PF_WAYS];
	pending_entry outstanding_entries[MAX_DEPTH]; 
	uint64_t ose_head, ose_tail;

	//for stat
	uint64_t pf_lookup_count=0;
	uint64_t pf_lookup_hit=0;
	
	b_prefetcher() {
		//std::cout << "b_prefetcher init" << std::endl;
		for (uint64_t i = 0; i < PF_SETS; i++) {
			for (uint64_t j = 0; j < PF_WAYS; j++) {
				pf_lut[i][j].tag = 0; //invalid tag
				pf_lut[i][j].recent_access_t = 0;
			}
		}
		for (uint64_t i = 0; i < MAX_DEPTH; i++) {
			outstanding_entries[i].tag = 0;
			outstanding_entries[i].page_bitvector= 0;
			outstanding_entries[i].count_down= 0;
		}
		ose_head = 0;
		ose_tail = 0;
	}

	
	void add_to_lut(pending_entry pe) {
		uint64_t tag = pe.tag;
		uint64_t set_index = pe.set_index;
		uint64_t oldest_way_i = 0;
		uint64_t oldest_t=~0;
		bool sancheck_hit = false;
		for (uint64_t i = 0; i < PF_WAYS; i++) {
			if (pf_lut[set_index][i].tag == tag) { // corner case duplicate while entry was in outstanding. overwrite it or ignore it?
				//avoid redundant entries
				pf_lut[set_index][i].recent_access_t = pe.added_time;
				return;
			}
			if (pf_lut[set_index][i].recent_access_t < oldest_t) {
				oldest_t = pf_lut[set_index][i].recent_access_t;
				oldest_way_i = i;
				sancheck_hit = true;
			}
		}
		assert(sancheck_hit);

		pf_lut[set_index][oldest_way_i].tag = tag;
		pf_lut[set_index][oldest_way_i].page_bitvector = pe.page_bitvector;
		pf_lut[set_index][oldest_way_i].recent_access_t = pe.added_time;

	}
	
	void update_ose(uint64_t addr) {
		uint64_t page_offset = addr & 0xFFF;
		page_offset = page_offset >> lineBits;
		assert(page_offset < 64);

		uint64_t ose_i = ose_head;
		while (ose_i != ose_tail) {
			//DO update - set bitvector corresponding to this access, subtract count_down. 
			//		if count_down==0, insert into lut, and head++. if head==MAX_DEPTH, head=0;
			outstanding_entries[ose_i].page_bitvector= outstanding_entries[ose_i].page_bitvector  | (1 << page_offset);
			outstanding_entries[ose_i].count_down--;
			if (outstanding_entries[ose_i].count_down == 0) {
				////////////INSERT ENTRY
				add_to_lut(outstanding_entries[ose_i]);
				ose_head++;
				if (ose_head == MAX_DEPTH) {
					ose_head = 0;
				}
			}
			
			ose_i++;
			if (ose_i == MAX_DEPTH) {
				ose_i = 0;
			}
		}

	}

	void add_ose(uint64_t input_tag, uint64_t set_index, uint64_t cur_t) {
		outstanding_entries[ose_tail].tag = input_tag;
		outstanding_entries[ose_tail].page_bitvector = 0;
		outstanding_entries[ose_tail].count_down = F_DEPTH;
		outstanding_entries[ose_tail].set_index = set_index;
		outstanding_entries[ose_tail].added_time = cur_t;
		ose_tail++;
		if (ose_tail == MAX_DEPTH) {
			ose_tail = 0;
		}
		assert(ose_tail != ose_head);
	}

	bool lookup_pf(uint64_t miss_pc, uint64_t addr, uint64_t &page_bitvcector, uint64_t cur_t) {

		//std::cout<<"sancheck to see lookup_pf is called!"<<std::endl;

		pf_lookup_count++;
		//if(pf_lookup_count % 1000){
		//	std::cout<<"lookupcount: "<<pf_lookup_count<<", hit perc: "<<(100*pf_lookup_hit / pf_lookup_count)<<"%"<<std::endl;
		//}

		uint64_t lineBits = 6;
		uint64_t pagebits = 12; uint64_t pagesize = 4096;
		uint32_t lineSize = 1 << lineBits;
		//Address miss_pc = bblAddr + (i * linesize);
		Address page_offset = addr & 0xFFF;
		Address page_base = addr & (~0xFFF);
		assert(page_base + page_offset == addr);
		page_offset = page_offset >> lineBits;
		uint64_t input_tag = miss_pc ^ addr;
		uint64_t index = (miss_pc>>3) ^ page_offset; //for indexing, discard lsbs of pc that might just be 0
		index = index % PF_SETS;

		//bool tag_match = false;
		bool match_found = false;
		uint64_t match_ts = 0;
		uint64_t match_way = 0;
		for (uint64_t i = 0; i < PF_WAYS; i++) {
			if (input_tag == pf_lut[index][i].tag) {
				page_bitvcector = pf_lut[index][i].page_bitvector;
				pf_lut[index][i].recent_access_t = cur_t;
				pf_lookup_hit++;
				//std::cout<<"sancheck to see lookup_pf hits tag match!"<<std::endl;
				return true;
			}
			if (pf_lut[index][i].tag != 0) {
				match_found = true;
				if (pf_lut[index][i].recent_access_t > match_ts) {
					match_way = i;
					match_ts = pf_lut[index][i].recent_access_t;
					page_bitvcector = pf_lut[index][i].page_bitvector;
				}
			}
		}
		if(match_found){
			pf_lookup_hit++;
			//std::cout<<"sancheck to see lookup_pf hits at all!"<<std::endl;
		}
		// don't update lru if not exact match
		//if (match_found) {pf_lut[index][match_way].recent_access_t = cur_t;}

		//some entry found, but not exact tag match if code reached here. issue a new entry
		add_ose(input_tag, index, cur_t);

		return match_found;
	}

};

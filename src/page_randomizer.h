#ifndef PAGE_RANDOMIZER_H_
#define PAGE_RANDOMIZER_H_

#include "bithacks.h"
#include "zsim.h"
#include<vector>
#include "g_std/g_unordered_map.h"
#include "g_std/g_vector.h"
#include <x86intrin.h>


class Page_Randomizer : public GlobAlloc {
private:
    g_unordered_map<uint64_t, uint64_t> pfn_to_mfn_map;
    g_vector<uint64_t> mfn_to_pfn_rev_map;
    uint64_t gmPages;
    uint64_t linesPerPageBits;
    lock_t pageMapLock;
    bool enable;

public:
    Page_Randomizer(uint64_t __gmPages, bool __enable) 
        : gmPages(__gmPages), enable(__enable) {
        if (enable) {
            mfn_to_pfn_rev_map.resize(gmPages, 0);
            futex_init(&(pageMapLock));
            linesPerPageBits = 6;
            pfn_to_mfn_map[0] = 0;
            mfn_to_pfn_rev_map[0] = 0;
	    info("page randomizer init done, gmPages: %d",gmPages);
        }
    }

    uint64_t get_addr(uint64_t pLineAddr, uint32_t procIdx) {
        if (!enable) {
            return pLineAddr;
        }
        uint64_t pfn = pLineAddr >> linesPerPageBits;
        uint64_t mfn = (uint64_t)-1;
        int count = 0;
        futex_lock(&(pageMapLock));
        if (pfn_to_mfn_map.find(pfn) != pfn_to_mfn_map.end()) {
            // PFN -> MFN mapping found
            mfn = pfn_to_mfn_map[pfn];
            futex_unlock(&(pageMapLock));
        }
        else {
            futex_unlock(&(pageMapLock));
            // Generate a new PFN -> MFN mapping
            while (mfn == (uint64_t)-1 && count < 5) {
				uint32_t seed = __rdtsc();
				seed = seed+procIdx;
                //uint64_t candidate_mfn = (rand_r(&procIdx))%(gmPages); 
                uint64_t candidate_mfn = (rand_r(&seed))%(gmPages); 
		//info("candidate_mfn: %llx",candidate_mfn);
                futex_lock(&(pageMapLock));
                if(mfn_to_pfn_rev_map[candidate_mfn] == 0) {
                    mfn = candidate_mfn;
                    pfn_to_mfn_map[pfn] = mfn;
                    mfn_to_pfn_rev_map[mfn] = pfn;
                }
                else if (count < 4) {
                    count++;
                }
                else {
					info("failed to find new page, swapping");
					//for(uint64_t i=1; i<gmPages;i++){
					//	if(mfn_to_pfn_rev_map[i]==0){
					//		mfn_to_pfn_rev_map[i]=pfn;
					//		pfn_to_mfn_map[pfn] = i;
					//		mfn=i;
					//		break;
					//	}
					//}

                    // Swap current pfn to generate a valid mapping
                    uint64_t old_pfn = mfn_to_pfn_rev_map[candidate_mfn];
                    mfn = candidate_mfn;
                    pfn_to_mfn_map.erase(old_pfn);
                    pfn_to_mfn_map[pfn] = mfn;
                    mfn_to_pfn_rev_map[mfn] = pfn;
                }
                futex_unlock(&(pageMapLock));
            }
        }
        assert(mfn != (uint64_t)-1);
        uint64_t mLineAddr = ((mfn << linesPerPageBits) | 
                                (pLineAddr & ((1<<linesPerPageBits)  - 1)));
        return mLineAddr;
    }
};

#endif  // PAGE_RANDOMIZER_H_

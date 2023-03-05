#include "page_randomizer.h"

Page_Randomizer::Page_Randomizer(uint64_t __gmPages, bool __enable) 
    : gmPages(__gmPages), enable(__enable) {
    if (enable) {
        mfn_to_pfn_rev_map.resize(gmPages, 0);
        futex_init(&(pageMapLock));
        linesPerPageBits = 6;
        pfn_to_mfn_map.insert(std::pair<uint64_t, uint64_t>(0,0));
        mfn_to_pfn_rev_map[0] = 0;
        info("page randomizer init done, gmPages: %lu", gmPages);
    }
    else {
        info("page randomizer DISABLED");
    }
}

uint64_t Page_Randomizer::get_addr(uint64_t pLineAddr, uint32_t procIdx) {
    if (!enable) {
        // info ("returning original line address");
        return pLineAddr;
    }
    uint64_t pfn = pLineAddr >> linesPerPageBits;
    uint64_t mfn = (uint64_t)-1;
    int count = 0;
    // info ("finding pfn to mfn mapping... enable: %d, map_size: %d", enable, 
    //         pfn_to_mfn_map.size());
    futex_lock(&(pageMapLock));
    auto it = pfn_to_mfn_map.find(pfn);
    if (it != pfn_to_mfn_map.end()) {
        // PFN -> MFN mapping found
        mfn = it->second;
        futex_unlock(&(pageMapLock));
    }
    else {
        // Generate a new PFN -> MFN mapping
        // info("adding new mfn to addr map...");
        while (mfn == (uint64_t)-1 && count < 5) {
            uint32_t seed = __rdtsc();
            seed = seed+procIdx;
            uint64_t candidate_mfn = (rand_r(&seed))%(gmPages); 
            // info("candidate_mfn: %llx",candidate_mfn);
            if(mfn_to_pfn_rev_map[candidate_mfn] == 0) {
                mfn = candidate_mfn;
                pfn_to_mfn_map.insert(std::pair<uint64_t, uint64_t>(pfn,mfn));
                mfn_to_pfn_rev_map[mfn] = pfn;
            }
            else if (count < 4) {
                count++;
            }
            else {
                info("PAGE_RANDOMIZER failed to find new page, swapping");
                // Swap current pfn to generate a valid mapping
                uint64_t old_pfn = mfn_to_pfn_rev_map[candidate_mfn];
                mfn = candidate_mfn;
                pfn_to_mfn_map.erase(old_pfn);
                pfn_to_mfn_map.insert(std::pair<uint64_t, uint64_t>(pfn,mfn));
                mfn_to_pfn_rev_map[mfn] = pfn;
            }
        }
        futex_unlock(&(pageMapLock));
    }
    // info("Mapped %lx to %lx, count: %d", pfn, mfn, count);
    assert(mfn != (uint64_t)-1 && mfn < gmPages);
    uint64_t mLineAddr = ((mfn << linesPerPageBits) | 
                            (pLineAddr & ((1<<linesPerPageBits)  - 1)));
    return mLineAddr;
}
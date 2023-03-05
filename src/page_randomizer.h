#ifndef PAGE_RANDOMIZER_H_
#define PAGE_RANDOMIZER_H_

#include "bithacks.h"
#include "zsim.h"
#include<vector>
#include "g_std/g_unordered_map.h"
#include "g_std/g_vector.h"
#include "pad.h"
#include <x86intrin.h>
#include <random>

class Page_Randomizer : public GlobAlloc {
private:
    g_unordered_map<uint64_t, uint64_t> pfn_to_mfn_map;
    g_vector<uint64_t> mfn_to_pfn_rev_map;
    uint64_t gmPages;
    uint64_t linesPerPageBits;
    PAD();
    lock_t pageMapLock;
    PAD();
    const bool enable;
    PAD();

public:
    Page_Randomizer(uint64_t __gmPages, bool __enable);

    uint64_t get_addr(uint64_t pLineAddr, uint32_t procIdx);
};

#endif  // PAGE_RANDOMIZER_H_

#include "VirtualMemory.h"
#include "MemoryConstants.h"
#include "PhysicalMemory.h"
#include <cassert>
#include <cmath>
#include <tuple>
#include "stdio.h"
#include "stdlib.h"


word_t max_distant_leaf(uint64_t virtualAddress);

uint64_t physicalAddress(uint64_t frame_num, uint64_t offset){
    if (frame_num >= NUM_FRAMES) {
        printf("Invalid frame_num: %lu\n", frame_num);
    }
    if (offset >= PAGE_SIZE) {
        printf("Invalid offset: %lu\n", offset);
    }
    uint64_t address = (frame_num * PAGE_SIZE) | offset;
    return address;
}

void clearFrame(uint64_t frame_num){
    for (uint64_t entry=0; entry< PAGE_SIZE; entry++)
    {
        uint64_t physical_add = physicalAddress(frame_num, entry);
        PMwrite(physical_add, 0);
    }

}

inline uint64_t nextPrefix(uint64_t prefix, uint64_t offset)
{
    return (prefix << OFFSET_WIDTH) | offset;
}


bool isZeroFrame(uint64_t frame) {
    word_t val;
    for (uint64_t offset = 0; offset < PAGE_SIZE; ++offset) {
        PMread(physicalAddress(frame, offset), &val);
        if (val != 0)
            return false;
    }
    return true;
}




static inline uint64_t
virt_index_at_level(uint64_t vaddr, int level)          // root == 0
{
    const int shift = OFFSET_WIDTH * (TABLES_DEPTH-level);
    return (vaddr >> shift) & (PAGE_SIZE-1);
}


void dfs(word_t     frame,             // current table frame
         int        depth,             // 0 == root
         bool       on_path,           // true ⇒ frame lies on access path
         uint64_t   vaddr,             // virtual address we service
         uint64_t&  max_frame,         // OUT
         uint64_t&  zero_candidate,    // OUT
         uint64_t&  zero_parent,       // OUT
         uint64_t&  zero_offset,       // OUT
         word_t     parent = 0,        // caller’s frame
         uint64_t   parent_off = 0)    // … offset in that frame
{
    /* 1. highest frame id so far */
    if (frame > max_frame)
        max_frame = frame;

    /* 2. first empty table that is NOT on the path */
    if (!on_path &&
        zero_candidate == UINT64_MAX &&
        isZeroFrame(frame))
    {
        zero_candidate = frame;
        zero_parent    = parent;
        zero_offset    = parent_off;
    }

    /* 3. stop *before* descending into data pages */
    if (depth == TABLES_DEPTH)
        return;

    /* 4. recurse into non-null children */
    for (uint64_t off = 0; off < PAGE_SIZE; off++)
    {
        word_t child;
        PMread(physicalAddress(frame, off), &child);
        if (child == 0) {
            continue;
        }


        bool child_on_path = on_path &&
                             (off == virt_index_at_level(vaddr, depth));

        dfs(child, depth+1, child_on_path, vaddr,
            max_frame, zero_candidate, zero_parent, zero_offset,
            frame, off);
    }
}

uint64_t allocateFrame(uint64_t virtualAddress) {

    uint64_t max_frame = 0;
    uint64_t zero_candidate = -1;
    uint64_t zero_parent = -1;
    uint64_t zero_offset = -1;

    dfs(0, 0, true, virtualAddress, max_frame, zero_candidate, zero_parent, zero_offset);

    if (zero_candidate != -1) {
        // Option 1: Unlink zero frame and use it
        if (zero_parent != -1 && zero_offset != -1) {
            PMwrite(zero_parent * PAGE_SIZE + zero_offset, 0);
        }
        // clearFrame(zero_candidate);
        return zero_candidate;
    }

    if (max_frame + 1 < NUM_FRAMES) {
        // Option 2: Use next unused frame
        max_frame += 1;
        printf("chose max %d \n", max_frame);
        return max_frame;
    }


    uint64_t f = max_distant_leaf(virtualAddress);
    // clearFrame(f);
    return f;

}

uint64_t cyclic_distance(uint64_t v1, uint64_t v2)
{
    uint64_t diff = (v1 > v2) ? v1 - v2 : v2 - v1;      // absolute difference
    uint64_t shortest = (diff < NUM_PAGES - diff) ? diff
                                                  : NUM_PAGES - diff;

    return shortest;
}

void find_max_distant(uint64_t frame, int depth, uint64_t prefix,
                      uint64_t target_vpage,
                      uint64_t& best_dist,
                      word_t& best_frame,
                      uint64_t& best_vpage,
                      uint64_t& evict_parent,
                      uint64_t& evict_offset) {
    {


        /* stop when we are already at data-page level */
        if (depth == TABLES_DEPTH-1) {
            for (uint64_t offset = 0; offset < PAGE_SIZE; offset++) {
                word_t leaf_frame;
                PMread(physicalAddress(frame, offset), &leaf_frame);
                if (leaf_frame == 0 ) {
                    continue;
                }


                /* build the *full* virtual-page number */
                uint64_t candidate_vpage = (prefix << OFFSET_WIDTH) | offset;

                if (candidate_vpage == target_vpage) {
                    // never evict page we need
                    continue;
                }

                // if (leaf_frame != 0) {
                //     printf("Candidate leaf: frame=%lu vpage=%lu\n", leaf_frame, candidate_vpage);
                // }

                uint64_t dist = cyclic_distance(candidate_vpage, target_vpage);
                if (dist > best_dist) {
                    best_dist   = dist;
                    best_frame  = leaf_frame;
                    best_vpage  = candidate_vpage;
                    evict_parent = frame;
                    evict_offset = offset;
                }
            }
            return;
        }

        /* recurse into child tables */
        for (word_t offset = 0; offset < PAGE_SIZE; ++offset) {
            word_t child;
            PMread(physicalAddress(frame, offset), &child);
            if (child == 0) {
                // empty entry
                continue;
            }

            uint64_t child_prefix = (prefix << OFFSET_WIDTH) | offset;
            find_max_distant(child, depth + 1,
                             child_prefix, target_vpage,
                             best_dist, best_frame, best_vpage,
                             evict_parent, evict_offset);
        }
    }
}



word_t max_distant_leaf(uint64_t virtualAddress) {
    uint64_t best_dist = 0;
    word_t best_frame = NUM_FRAMES;  // Start with invalid frame
    uint64_t target_vpage = virtualAddress>>OFFSET_WIDTH;
    uint64_t best_vpage = 0;
    uint64_t evict_parent = -1;
    uint64_t evict_offset = -1;

    find_max_distant(0, 0, 0, target_vpage,
                     best_dist, best_frame, best_vpage,
                     evict_parent, evict_offset);

    if (best_frame >= NUM_FRAMES ||
        evict_parent == (uint64_t)-1 ||
        evict_offset == (uint64_t)-1)      // search failed
    {
        return UINT64_MAX;                 // propagate “no frame found”
    }

    PMwrite( physicalAddress(evict_parent, evict_offset), 0 );


    PMevict( best_frame, best_vpage );
    return best_frame;

}




void VMinitialize(){
    clearFrame(0);
}



/*
 * tries to go down the tree based on the binary virtual address
 */
uint64_t downTheTree(uint64_t virtualAddress)
{
    uint64_t curr_frame = 0;                 // root = frame 0
    for (int level = 0; level < TABLES_DEPTH ;++level)  {   // ←-1 here

        word_t index = virt_index_at_level(virtualAddress, level);
        uint64_t paddr  = physicalAddress(curr_frame, index);

        word_t next;
        PMread(paddr, &next);
        if (next == 0 ) {                    // missing table, allocate one
            next = allocateFrame(virtualAddress);
            if (level < TABLES_DEPTH-1) {
                clearFrame(next);
            }

            // next = allocateFrame(virtualAddress);
            PMwrite(paddr, next);

        }
        curr_frame = next;
    }



    PMrestore(curr_frame, virtualAddress >> OFFSET_WIDTH);  // bring it in
    return physicalAddress(curr_frame,
                           virtualAddress & (PAGE_SIZE-1)); // byte offset
}





int VMread(uint64_t virtualAddress, word_t* value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0; // invalid address
    }

    uint64_t data_addr = downTheTree(virtualAddress);
    PMread(data_addr, value);

    return 1;
}


int VMwrite(uint64_t virtualAddress, word_t value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0; // invalid address
    }
    // printf("write %d to %d \n", virtualAddress, value);

    uint64_t data_addr = downTheTree(virtualAddress);
    PMwrite(data_addr, value);

    return 1;
}
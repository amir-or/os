// created by Einav 6/6/2025
#include "VirtualMemory.h"
#include "MemoryConstants.h"
#include "PhysicalMemory.h"

#include "stdio.h"


word_t max_distant_leaf(uint64_t virtualAddress);

uint64_t physicalAddress(uint64_t frame_num, uint64_t offset){
    return frame_num * PAGE_SIZE + offset;
}

void clearFrame(uint64_t frame_num){
    for (uint64_t entry=0; entry< PAGE_SIZE; entry++)
    {
        uint64_t physical_add = physicalAddress(frame_num, entry);
        PMwrite(physical_add, 0);
    }

}


bool isZeroFrame(uint64_t frame) {
    word_t val;
    for (uint64_t offset = 0; offset < PAGE_SIZE; ++offset) {
        PMread(frame * PAGE_SIZE + offset, &val);
        if (val != 0)
            return false;
    }
    return true;
}


uint64_t computeVirtualPath(uint64_t virtualAddress, int level) {
    uint64_t shift = OFFSET_WIDTH * (TABLES_DEPTH - 1 - level);
    uint64_t indices = (virtualAddress >> shift) & ((1 << OFFSET_WIDTH) - 1);
    return indices;
}


void dfs(uint64_t frame, int depth, bool still_on_path,
         uint64_t virtualAddress,
         int& max_frame,
         int& zero_candidate, int& zero_parent, int& zero_offset,
         int parent = -1, int parent_offset = -1) {

    if ((int)frame > max_frame) {
        max_frame = (int)frame;
    }

    // Consider as zero candidate only if not on path to current VA
    if (!still_on_path && isZeroFrame(frame) && zero_candidate == -1 && depth != TABLES_DEPTH) {
        zero_candidate = (int)frame;
        zero_parent = parent;
        zero_offset = parent_offset;
        // Do not return — we continue to find max_frame
    }

    // Traverse children
    for (uint64_t offset = 0; offset < PAGE_SIZE; ++offset) {
        word_t child;
        PMread(frame * PAGE_SIZE + offset, &child);
        if (child != 0 && depth < TABLES_DEPTH) {
            bool next_is_on_path = still_on_path &&
                                   (depth < TABLES_DEPTH) &&
                                   (offset == computeVirtualPath(virtualAddress, depth));

            dfs(child, depth + 1, next_is_on_path,
                virtualAddress,
                max_frame, zero_candidate, zero_parent, zero_offset,
                frame, offset);
        }
    }
}

int allocateFrame(uint64_t virtualAddress) {

    int max_frame = 0;
    int zero_candidate = -1;
    int zero_parent = -1;
    int zero_offset = -1;

    dfs(0, 0, true, virtualAddress, max_frame, zero_candidate, zero_parent, zero_offset);

    if (zero_candidate != -1) {
        // Option 1: Unlink zero frame and use it
        if (zero_parent != -1 && zero_offset != -1) {
            PMwrite(zero_parent * PAGE_SIZE + zero_offset, 0);
        }
        return zero_candidate;
    }

    if (max_frame + 1 < NUM_FRAMES) {
        // Option 2: Use next unused frame
        return max_frame + 1;
    }

    // Option 3: eviction
    return max_distant_leaf(virtualAddress);

}

uint64_t cyclic_distance(uint64_t v1, uint64_t v2)
{
    uint64_t diff = (v1 > v2) ? v1 - v2 : v2 - v1;      // absolute difference
    uint64_t shortest = (diff < NUM_PAGES - diff) ? diff
                                                  : NUM_PAGES - diff;

    return shortest;
}

// Recursive helper for max_distant_leaf
void find_max_distant(uint64_t frame, int depth, uint64_t current_vpage,
                      uint64_t target_vpage,
                      uint64_t& best_dist,
                      word_t& best_frame,
                      uint64_t& best_vpage,
                      int& evict_parent,
                      int& evict_offset,
                      int parent = -1,
                      int parent_offset = -1)
{
    if (depth == TABLES_DEPTH) {
        for (uint64_t offset = 0; offset < PAGE_SIZE; ++offset) {
            word_t leaf_frame;
            PMread(physicalAddress(frame, offset), &leaf_frame);
            if (leaf_frame != 0) {
                uint64_t candidate_vpage = (current_vpage << OFFSET_WIDTH) | offset;
                uint64_t dist = cyclic_distance(candidate_vpage, target_vpage);
                if (dist > best_dist) {
                    best_dist = dist;
                    best_frame = leaf_frame;
                    best_vpage = candidate_vpage;
                    evict_parent = frame;
                    evict_offset = offset;
                }
            }
        }
        return;
    }

    for (uint64_t offset = 0; offset < PAGE_SIZE; ++offset) {
        word_t child;
        PMread(physicalAddress(frame, offset), &child);
        if (child != 0) {
            uint64_t next_vpage = (current_vpage << OFFSET_WIDTH) | offset;
            find_max_distant(child, depth + 1, next_vpage, target_vpage,
                             best_dist, best_frame, best_vpage,
                             evict_parent, evict_offset,
                             frame, offset);
        }
    }
}


word_t max_distant_leaf(uint64_t virtualAddress)
{
    uint64_t best_dist = 0;
    word_t best_frame = 0;
    uint64_t target_vpage = virtualAddress >> OFFSET_WIDTH;
    uint64_t best_vpage = 0;
    int evict_parent = -1;
    int evict_offset = -1;

    find_max_distant(0, 0, 0, target_vpage, best_dist, best_frame,
                     best_vpage, evict_parent, evict_offset);
    //unlink from parent
    if (evict_parent != -1 && evict_offset != -1)   {
        PMwrite(physicalAddress(evict_parent, evict_offset), 0);
    }

    //physical eviction
    PMevict(best_frame, best_vpage >> OFFSET_WIDTH);
    printf("eviction success\n");
    return best_frame;
}




void VMinitialize(){
    clearFrame(0);
}



/*
 * tries to go down the tree based on the binary virtual address
 */
uint64_t downTheTree(uint64_t virtualAddress){
    uint64_t curr_frame = 0; // Start at root frame (frame 0)
    uint64_t addr;

    for (int level = 0; level < TABLES_DEPTH; ++level) {
        //extract the number which defines the offset in next table
        uint64_t shift = OFFSET_WIDTH * (TABLES_DEPTH - 1 - level);
        uint64_t index = (virtualAddress >> shift) & (PAGE_SIZE - 1); // extract relevant bits
        addr = physicalAddress(curr_frame, index);

        word_t next_frame;
        PMread(addr, &next_frame);

        if (next_frame == 0) {
            // page not mapped, we get the next unused frame
            // opt1: find a zero table

            int empty_frame = allocateFrame(virtualAddress);
            printf("created %d \n", empty_frame);


            PMwrite(addr,empty_frame);

            //clear child
            clearFrame(empty_frame);

            next_frame = empty_frame;

        }

        curr_frame = next_frame;
    }

    // restore the page from hard disk
    PMrestore(curr_frame, virtualAddress);
    return physicalAddress(curr_frame, addr%PAGE_SIZE);
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

    uint64_t data_addr = downTheTree(virtualAddress);
    PMwrite(data_addr, value);

    return 1;
}



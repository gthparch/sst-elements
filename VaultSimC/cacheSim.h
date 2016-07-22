#ifndef CACHESIM_H
#define CACHESIM_H

#include <cinttypes>

#include <cmath>
#include <iostream>
#include <iomanip>
#include <bitset>
#include <cstring>

#include <sst/elements/memHierarchy/memEvent.h>

using namespace std;

class cacheSim {
private:
    struct cache_stats_t;

public:
    void setupCache(uint64_t c, uint64_t b, uint64_t s, uint64_t v, uint64_t k);
    void cacheAccess(char rw, uint64_t address);
    void completeCache();
    void printStatistics();

private:
    struct cache_stats_t {
        uint64_t accesses;
        uint64_t reads;
        uint64_t read_misses;
        uint64_t read_misses_combined;
        uint64_t writes;
        uint64_t write_misses;
        uint64_t write_misses_combined;
        uint64_t misses;
    	uint64_t write_backs;
    	uint64_t vc_misses;
    	uint64_t prefetched_blocks;
    	uint64_t useful_prefetches;
    	uint64_t bytes_transferred;

    	double   hit_time;
        double   miss_penalty;
        double   miss_rate;
        double   avg_access_time;

        uint64_t evictions;
        uint64_t misses_with_VC;
        uint64_t read_hits;
        uint64_t write_hits;
        uint64_t bytes_read;
        uint64_t bytes_written;
        uint64_t VC_hit_read;
        uint64_t VC_hit_write;
        uint64_t VC_hits;
        uint64_t stride_match;
        uint64_t blocks_need_prefetch;
        uint64_t blocks_actually_prefetched;
        uint64_t prefetch_hit;
        uint64_t prefetch_again;
        uint64_t bytes_prefetched;
    };

public:
    cache_stats_t* p_stats;


public:
    /** Argument to cache_access rw. Indicates a load */
    static const char     READ = 'r';
    /** Argument to cache_access rw. Indicates a store */
    static const char     WRITE = 'w';


private:

    static const uint64_t DEFAULT_C = 15;   /* 32KB Cache */
    static const uint64_t DEFAULT_B = 6;    /* 64-byte blocks */
    static const uint64_t DEFAULT_S = 3;    /* 8 blocks per set */
    static const uint64_t DEFAULT_V = 0;    /* 0 victim blocks */
    static const uint64_t DEFAULT_K = 0;	/* 0 prefetch distance */


    unsigned int offset_bit;
    unsigned int set_bit;
    unsigned int indx_bit;
    unsigned int tag_bit;
    unsigned int num_block_per_set;
    unsigned int num_set_in_cache;
    unsigned int VC_size;
    uint64_t** cache;
    unsigned int** LRU;
    bool** dirty;
    bool** prftch_count;
    uint64_t** VC;
    bool* dirty_VC;
    unsigned int* FIFO;
    bool* prftch_count_VC;

    unsigned int* num_block_in_set_used;
    unsigned int num_block_in_VC_used;
    unsigned int num_prefetch_used;

    uint64_t all_one = ~0;
    uint64_t mask_index;
    uint64_t mask_teg;

    unsigned int prefetch_deg;
    int64_t stride = 0;
    uint64_t last_miss_addrss = 0;

};

#endif /* CACHESIM_H */

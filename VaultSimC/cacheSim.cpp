#include "cacheSim.h"

 /*
 * @c The total number of bytes for data storage is 2^C
 * @b The size of a single cache line in bytes is 2^B
 * @s The number of blocks in each set is 2^S
 * @v The number of blocks in the victim cache is 2^V
 * @k The prefetch distance is K
 */
void cacheSim::setupCache(uint64_t c, uint64_t b, uint64_t s, uint64_t v, uint64_t k) {
	//global cache setting
	offset_bit = b;
	set_bit = s;
	indx_bit = c-b-s;
	tag_bit = 64-(c-s);

	num_block_per_set = pow(2,s);
	num_set_in_cache = pow(2,(c-b-s));
	VC_size = v;

	mask_index = all_one >> (64-indx_bit);
	if (s == c-b)
		mask_index = 0;

	prefetch_deg = k;

	//TABLES ALLOCATION****************************************
	//setup tag store, index store
	cache = new uint64_t* [num_set_in_cache];
	for (unsigned int i=0; i<num_set_in_cache; i++)
		cache[i] = new uint64_t [num_block_per_set];

	//setup LRU table
	LRU = new unsigned int* [num_set_in_cache];
	for (unsigned int i=0; i<num_set_in_cache; i++)
		LRU[i] = new unsigned int [num_block_per_set];

	//setup dirty bit table
	dirty = new bool* [num_set_in_cache];
	for (unsigned int i=0; i<num_set_in_cache; i++)
		dirty[i] = new bool [num_block_per_set];

	//setup VC
	if (VC_size != 0) {									//if we have a VC
		VC = new uint64_t* [VC_size];
		for (unsigned int i=0; i<VC_size; i++)
			VC[i] = new uint64_t [2];					//set/index - tag

		num_block_in_VC_used = 0;

		dirty_VC = new bool [VC_size];

		FIFO = new unsigned int [VC_size];

		prftch_count_VC = new bool [VC_size];
	}

	//setup prefetch history and count table
	prftch_count = new bool* [num_set_in_cache];
	for (unsigned int i=0; i<num_set_in_cache; i++) {
		prftch_count[i] = new bool [num_block_per_set];
	}

	//to trace number of used sets and blocks in searches
	num_block_in_set_used = new unsigned int [num_set_in_cache];
	for (unsigned int i=0; i<num_set_in_cache; i++)
		num_block_in_set_used[i] = 0;
 	//memset(num_block_in_set_used, 0, sizeof(unsigned int)*num_set_in_cache);

	//stats reset
	p_stats = new cache_stats_t;
	memset(p_stats, 0, sizeof(cache_stats_t));

	return;
}


/**
 * Subroutine that simulates the cache one trace event at a time.
 *
 * @rw The type of event. Either READ or WRITE
 * @address  The target memory address
 * @p_stats Pointer to the statistics structure
 */
void cacheSim::cacheAccess(char rw, uint64_t address) {
	p_stats->accesses++;
	if (rw == READ) p_stats->reads++;
	if (rw == WRITE) p_stats->writes++;

	//extract tag and index
	uint64_t address_no_offset;
	unsigned int index;
	unsigned int i_hit_block;
	unsigned int i_hit_VC;
	uint64_t tag;

	address_no_offset = address >> offset_bit;
	index = address_no_offset & mask_index;
	tag = address_no_offset >> indx_bit;

	//HIT?MISS************************************************************
	bool MISS = true;
	for (unsigned int i=0; i<num_block_in_set_used[index]; i++)
		if (tag == cache[index][i]) {
			i_hit_block = i;
			MISS= false;

			//check for useful prefetch
			if (prftch_count[index][i_hit_block]==1) {
				prftch_count[index][i_hit_block]=0;
				p_stats->useful_prefetches++;
			}

			break;
		}
	if (MISS) {
		if (rw == READ) p_stats->read_misses++;
		if (rw == WRITE) p_stats->write_misses++;
	}
	//Check VC if miss
	bool VC_MISS = true;
	if (MISS && VC_size != 0) {	//remove "&& VC_size != 0" based on official results! (Fall2014) -> returned it summer 2016
		for (unsigned int i =0; i<num_block_in_VC_used; i++)
			if (VC[i][0] == index && VC[i][1] == tag) {
				VC_MISS = false;
				i_hit_VC = i;

				//check for useful prefetch
				if (prftch_count_VC[i_hit_VC]==1) {
					prftch_count_VC[i_hit_VC]=0;
					p_stats->useful_prefetches++;
				}

				p_stats->VC_hits++;
				if (rw == READ)	p_stats->VC_hit_read++;
				if (rw == WRITE) p_stats->VC_hit_write++;
				break;
			}
		if(VC_MISS)
			p_stats->vc_misses++;
	}

	//TEST
	/*
	printf("%c\n", rw );
	printf("%d, %d\n", MISS, VC_MISS);
	printf("%" PRIx64 "\n", address);
	printf("%" PRIx64 "\t%" PRIx64 "\n",index,tag);
	printf("LINE BEFORE:\n");
	for (unsigned int i=0; i<num_block_in_set_used[index]; i++)
		printf("%" PRIx64 "\t%d\t%d\n",cache[index][i],LRU[index][i],dirty[index][i]);
	printf("VC Before:\n");
	for (unsigned int i=0; i<num_block_in_VC_used; i++)
		printf("%" PRIx64 "\t%" PRIx64 "\t%d\t%d\n",VC[i][0],VC[i][1],FIFO[i],dirty_VC[i]);
	*/

	//********************************************************************
	//READ   *************************************************************
	//********************************************************************
	//--READ MISS
	if (rw == READ) {
		if (MISS && VC_MISS) {												//**ordinary miss (without VC or VC miss)
			if (num_block_in_set_used[index] < num_block_per_set) { 		//**We have palce for it
				cache[index][num_block_in_set_used[index]] = tag;			//bring it
				dirty[index][num_block_in_set_used[index]] = 0; 			//write dirty bit
				prftch_count[index][num_block_in_set_used[index]] = 0;

				LRU[index][num_block_in_set_used[index]] = 1; 				//update LRU
				for (unsigned int i=0; i<num_block_in_set_used[index]; i++)
					LRU[index][i]++;

				num_block_in_set_used[index]++;								//update the number of used block
			}

			else { 															//**Cahce line is full; We need REPLACMENT
				unsigned int oldest_entry;
				for (unsigned int i=0; i<num_block_per_set; i++)
					if (LRU[index][i] == num_block_per_set) {				//find the oldest entry
						oldest_entry = i;
						break;
					}
				uint64_t temp_tag = cache[index][oldest_entry];
				bool temp_dirty = dirty[index][oldest_entry];
				bool temp_prftch_count = prftch_count[index][oldest_entry];

				cache[index][oldest_entry] = tag;							//write to cache
				dirty[index][oldest_entry] = 0;
				prftch_count[index][oldest_entry] = 0;
				LRU[index][oldest_entry] = 0;								//update all LRUs
				for (unsigned int j=0; j<num_block_per_set; j++)
					LRU[index][j]++;

				if (VC_size == 0) {											//**No VC, evict
					p_stats->evictions++;
					if (temp_dirty == 1) 									//if it is dirty, write back
						p_stats->write_backs++;
				}
				else {														//**VC! copy to VC!
					if (num_block_in_VC_used < VC_size) {					//**VC is not full
						VC[num_block_in_VC_used][0] = index;
						VC[num_block_in_VC_used][1] = temp_tag;
						prftch_count_VC[num_block_in_VC_used] = temp_prftch_count;
						dirty_VC[num_block_in_VC_used] = temp_dirty;
						FIFO[num_block_in_VC_used] = 1;
						for (unsigned int j=0; j<num_block_in_VC_used; j++)
							FIFO[j]++;

						num_block_in_VC_used++;
					}
					else {													//**VC is full! Lot of work!
						unsigned int FIFO_entry;
						for (unsigned int j=0; j<VC_size; j++)
							if(FIFO[j] == VC_size) {
								FIFO_entry = j;
								break;
							}
						p_stats->evictions++;
						if (dirty_VC[FIFO_entry] == 1)
							p_stats->write_backs++;

						VC[FIFO_entry][0] = index;
						VC[FIFO_entry][1] = temp_tag;
						prftch_count_VC[FIFO_entry] = temp_prftch_count;
						dirty_VC[FIFO_entry] = temp_dirty;
						FIFO[FIFO_entry] = 0;
						for (unsigned int k=0; k<VC_size; k++)
							FIFO[k]++;
					}
				}
			}
		}

		else if (MISS && !VC_MISS) { 										//**VC hit!
			unsigned int oldest_entry;
			for (unsigned int i=0; i<num_block_per_set; i++)
				if (LRU[index][i] == num_block_per_set) {					//find the oldest enrty
					oldest_entry = i;
					break;
				}

			uint64_t temp_tag = cache[index][oldest_entry];
			bool temp_dirty = dirty[index][oldest_entry];
			bool temp_prftch_count = prftch_count[index][oldest_entry];

			cache[index][oldest_entry] = VC[i_hit_VC][1];					//fill cache
			dirty[index][oldest_entry] = dirty_VC[i_hit_VC];
			prftch_count[index][oldest_entry] = prftch_count_VC[i_hit_VC];
			LRU[index][oldest_entry] = 0;
			for (unsigned int j=0; j<num_block_per_set; j++)
				LRU[index][j]++;

			VC[i_hit_VC][0] = index;										//update VC
			VC[i_hit_VC][1] = temp_tag;
			prftch_count_VC[i_hit_VC] = temp_prftch_count;
			dirty_VC[i_hit_VC] = temp_dirty;
			//for (unsigned int j=0; j<num_block_in_VC_used; j++)			//NO FIFO UPDATING!!!
			//	if (FIFO[j] < FIFO[i_hit_VC])
			//		FIFO[j]++;
			//FIFO[i_hit_VC] = 1;
		}
	//-------------------------------------------------------------------
	//--READ HIT
		else { //(!MISS)													//It is a HIT --- update LRU table
			unsigned int hitted_LRU = LRU[index][i_hit_block];
			for (unsigned int i=0; i<num_block_in_set_used[index]; i++) {
				if(LRU[index][i] < hitted_LRU)
					LRU[index][i]++;
			}
			LRU[index][i_hit_block] = 1;
			p_stats->read_hits++;
		}
	}
	//********************************************************************
	//Write  *************************************************************
	//********************************************************************
	//--WIRTE MISS
	else if(rw == WRITE) {
		if (MISS && VC_MISS) {													//we should write allocate it, bring it, then write it
			if (num_block_in_set_used[index] < num_block_per_set) { 			//**We have palce for it
				cache[index][num_block_in_set_used[index]] = tag;				//bring it
				dirty[index][num_block_in_set_used[index]] = 1; 				//write dirty bit
				prftch_count[index][num_block_in_set_used[index]] = 0;

				LRU[index][num_block_in_set_used[index]] = 1; 					//update LRU
				for (unsigned int i=0; i<num_block_in_set_used[index]; i++)
					LRU[index][i]++;

				num_block_in_set_used[index]++;									//update the number of used block
			}

			else { 																//**Cahce line is full; We need REPLACMENT
				unsigned int oldest_entry;
				for (unsigned int i=0; i<num_block_per_set; i++)
					if (LRU[index][i] == num_block_per_set) {					//find the oldest entry
						oldest_entry = i;
						break;
					}
				uint64_t temp_tag = cache[index][oldest_entry];					//save the data
				bool temp_dirty = dirty[index][oldest_entry];
				bool temp_prftch_count = prftch_count[index][oldest_entry];

				cache[index][oldest_entry] = tag;								//write data
				dirty[index][oldest_entry] = 1;
				prftch_count[index][oldest_entry] = 0;
				LRU[index][oldest_entry] = 0;									//update all LRUs
				for (unsigned int j=0; j<num_block_per_set; j++)
					LRU[index][j]++;

				if (VC_size == 0) {												//**No VC, eviction
					p_stats->evictions++;
					if (temp_dirty == 1)										//if it is dirty, write back
						p_stats->write_backs++;
				}
				else{															//**VC! copy to VC!
					if (num_block_in_VC_used < VC_size) {						//**VC is not full
						VC[num_block_in_VC_used][0] = index;
						VC[num_block_in_VC_used][1] = temp_tag;
						prftch_count_VC[num_block_in_VC_used] = temp_prftch_count;
						dirty_VC[num_block_in_VC_used] = temp_dirty;
						FIFO[num_block_in_VC_used] = 1;
						for (unsigned int j=0; j<num_block_in_VC_used; j++)
							FIFO[j]++;

						num_block_in_VC_used++;
					}
					else{														//**VC is full! Lot of work!
						unsigned int FIFO_entry;
						for (unsigned int j=0; j<VC_size; j++)
							if(FIFO[j] == VC_size) {
								FIFO_entry = j;
								break;
							}
						p_stats->evictions++;
						if (dirty_VC[FIFO_entry] == 1)
							p_stats->write_backs++;

						VC[FIFO_entry][0] = index;
						VC[FIFO_entry][1] = temp_tag;
						prftch_count_VC[FIFO_entry] = temp_prftch_count;
						dirty_VC[FIFO_entry] = temp_dirty;
						FIFO[FIFO_entry] = 0;
						for (unsigned int k=0; k<VC_size; k++)
							FIFO[k]++;
					}
				}
			}
		}

		else if (MISS && !VC_MISS) { 										//**VC hit!
			unsigned int oldest_entry;
			for (unsigned int i=0; i<num_block_per_set; i++)
				if (LRU[index][i] == num_block_per_set) {					//find the oldest enrty
					oldest_entry = i;
					break;
				}

			uint64_t temp_tag = cache[index][oldest_entry];
			bool temp_dirty = dirty[index][oldest_entry];
			bool temp_prftch_count = prftch_count[index][oldest_entry];

			cache[index][oldest_entry] = VC[i_hit_VC][1];					//fill cache
			dirty[index][oldest_entry] = 1;
			prftch_count[index][oldest_entry] = prftch_count_VC[i_hit_VC];
			LRU[index][oldest_entry] = 0;
			for (unsigned int j=0; j<num_block_per_set; j++)
				LRU[index][j]++;

			VC[i_hit_VC][0] = index;										//update VC
			VC[i_hit_VC][1] = temp_tag;
			prftch_count_VC[i_hit_VC] = temp_prftch_count;
			dirty_VC[i_hit_VC] = temp_dirty;
			//for (unsigned int j=0; j<num_block_in_VC_used; j++)			//NO FIFO UPDATING!!!
			//	if (FIFO[j] < FIFO[i_hit_VC])
			//		FIFO[j]++;
			//FIFO[i_hit_VC] = 1;
		}
		//-------------------------------------------------------------------
		//--WIRTE HIT
		else  {	//(!MISS)													//It is a HIT --- update LRU table
			unsigned int hitted_LRU = LRU[index][i_hit_block];
			for (unsigned int i=0; i<num_block_in_set_used[index]; i++) {
				if(LRU[index][i] < hitted_LRU)
					LRU[index][i]++;
			}
			LRU[index][i_hit_block] = 1;
			dirty[index][i_hit_block] = 1;
			p_stats->write_hits++;
		}

	}

	//TEST
	/*
	printf("LINE AFTER:\n");
	for (unsigned int i=0; i<num_block_in_set_used[index]; i++)
		printf("%" PRIx64 "\t%d\t%d\n",cache[index][i],LRU[index][i],dirty[index][i]);
	printf("VC After:\n");
	for (unsigned int i=0; i<num_block_in_VC_used; i++)
		printf("%" PRIx64 "\t%" PRIx64 "\t%d\t%d\n",VC[i][0],VC[i][1],FIFO[i],dirty_VC[i]);
	*/

	//********************************************************************
	//Prefetch ***********************************************************
	//********************************************************************
	if (prefetch_deg != 0) {
		if (MISS) {
			uint64_t address_no_offset = address >> offset_bit;
			uint64_t address_zero_offset = address_no_offset << offset_bit;
			int64_t d = address_zero_offset-last_miss_addrss;
			last_miss_addrss = address_zero_offset;
			if (stride == d) {																	//match->fetch K blocks
			p_stats->stride_match++;
				for (unsigned int i=0; i<prefetch_deg; i++){
					uint64_t prefetch_add = address + ((i+1)*d);
					uint64_t prefetch_add_no_offset = prefetch_add >> offset_bit;
					uint64_t prefetch_index = prefetch_add_no_offset & mask_index;
					uint64_t prefetch_tag = prefetch_add_no_offset >> indx_bit;

					p_stats->prefetched_blocks++;

					bool MISS = true;
					bool VC_MISS = true;
					unsigned int i_hit_VC;

					for (unsigned int i=0; i<num_block_in_set_used[prefetch_index]; i++)		//**Check if present
						if (prefetch_tag == cache[prefetch_index][i]) {
							MISS = false;
							// you don't need to change history, cause you actually never
							// insert this data as prefetch data
							break;
						}
					if (!MISS) {p_stats->prefetch_hit++; continue;}								//**HIT, skip to next block

					if (VC_size != 0)
						for (unsigned int i=0; i<num_block_in_VC_used; i++)
							if ((VC[i][0] ==  prefetch_index) && (VC[i][1] == prefetch_tag)) {
								VC_MISS = false;
								i_hit_VC = i;
								// you don't need to change history, cause you actually never
								// insert this data as prefetch data
								break;
							}


					if (num_block_in_set_used[prefetch_index] < num_block_per_set)	{			//**Cache is empty
						//if (!VC_MISS)															//**VC Hit(imposs)
						//	cout << "IMPOSSIBLE" << endl;
																								//**Bring to Cache
						cache[prefetch_index][num_block_in_set_used[prefetch_index]] = prefetch_tag;
						dirty[prefetch_index][num_block_in_set_used[prefetch_index]] = 0;
						prftch_count[prefetch_index][num_block_in_set_used[prefetch_index]] = 1;

						LRU[prefetch_index][num_block_in_set_used[prefetch_index]] =
													num_block_in_set_used[prefetch_index]+1; 	//update LRU

						num_block_in_set_used[prefetch_index]++;

						p_stats->blocks_actually_prefetched++;									//update prefetch data
					}

					else {																		//**Cache is full
						unsigned int oldest_entry;
						for (unsigned int i=0; i<num_block_per_set; i++)
						if (LRU[prefetch_index][i] == num_block_per_set) {						//find the oldest enrty
							oldest_entry = i;
							break;
						}

						uint64_t temp_tag = cache[prefetch_index][oldest_entry];
						bool temp_dirty = dirty[prefetch_index][oldest_entry];
						bool temp_prftch_count = prftch_count[prefetch_index][oldest_entry];


						if (!VC_MISS) {															//**VC Hit
							p_stats->prefetch_hit++;

							cache[prefetch_index][oldest_entry] = prefetch_tag;					//swap
							dirty[prefetch_index][oldest_entry] = dirty_VC[i_hit_VC];
							prftch_count[prefetch_index][oldest_entry] = 1;
							//no need to update LRU since it is the oldest element

							VC[i_hit_VC][0] = prefetch_index;									//update VC
							VC[i_hit_VC][1] = temp_tag;
							prftch_count_VC[i_hit_VC] = temp_prftch_count;
							dirty_VC[i_hit_VC] = temp_dirty;
							//for (unsigned int j=0; j<num_block_in_VC_used; j++)				//NO FIFO UPDATING!!!
							//	if (FIFO[j] < FIFO[i_hit_VC])
							//		FIFO[j]++;
							//FIFO[i_hit_VC] = 1;
						}

						else {																	//**All Miss
							cache[prefetch_index][oldest_entry] = prefetch_tag;					//copy pref-data to cache
							dirty[prefetch_index][oldest_entry] = 0;
							prftch_count[prefetch_index][oldest_entry] = 1;
							//no need to update LRU since it is the oldest element

							p_stats->blocks_actually_prefetched++;								//update prefetch data

							if (VC_size == 0) {													//**No VC
								if (temp_dirty == 1)
									p_stats->write_backs++;
								p_stats->evictions++;
							}
							else if (num_block_in_VC_used < VC_size) {							//**VC is not full
								VC[num_block_in_VC_used][0] = prefetch_index;
								VC[num_block_in_VC_used][1] = temp_tag;
								prftch_count_VC[num_block_in_VC_used] = temp_prftch_count;
								dirty_VC[num_block_in_VC_used] = temp_dirty;
								FIFO[num_block_in_VC_used] = 1;
								for (unsigned int j=0; j<num_block_in_VC_used; j++)
									FIFO[j]++;

								num_block_in_VC_used++;
							}
							else {																//**VC is full
								unsigned int FIFO_entry;
								for (unsigned int j=0; j<VC_size; j++)
									if(FIFO[j] == VC_size) {
										FIFO_entry = j;
										break;
									}
								p_stats->evictions++;
								if (dirty_VC[FIFO_entry] == 1)
									p_stats->write_backs++;

								VC[FIFO_entry][0] = prefetch_index;
								VC[FIFO_entry][1] = temp_tag;
								prftch_count_VC[FIFO_entry] = temp_prftch_count;
								dirty_VC[FIFO_entry] = temp_dirty;
								FIFO[FIFO_entry] = 0;
								for (unsigned int k=0; k<VC_size; k++)
									FIFO[k]++;
							}
						}
					}
				}
			}
			stride = d;
		}
	}

	//test
	/*
	printf("LINE AFTER PRE:\n");
	for (unsigned int i=0; i<num_block_in_set_used[index]; i++)
		printf("%" PRIx64 "\t%d\t%d\n",cache[index][i],LRU[index][i],dirty[index][i]);
	printf("VC After PRE:\n");
	for (unsigned int i=0; i<num_block_in_VC_used; i++)
		printf("%" PRIx64 "\t%" PRIx64 "\t%d\t%d\n",VC[i][0],VC[i][1],FIFO[i],dirty_VC[i]);
	printf("Wirte-Backs:%d\n",p_stats->write_backs);
	printf("VC Hits:%d\n",p_stats->VC_hits);
	printf("Prefetch Block:%d\n",p_stats->blocks_actually_prefetched++);
	printf("-------------------\n");
	*/
}


/**
 * Subroutine for cleaning up any outstanding memory operations and calculating overall statistics
 * such as MISS rate or average access time.
 *
 * @p_stats Pointer to the statistics structure
 */
void cacheSim::completeCache() {
	p_stats->read_misses_combined = p_stats->read_misses - p_stats->VC_hit_read;
	p_stats->write_misses_combined = p_stats->write_misses - p_stats->VC_hit_write;

	p_stats->misses = p_stats->read_misses + p_stats->write_misses;		//Total misses from read/write without VC
	p_stats->misses_with_VC = p_stats->read_misses_combined + p_stats->write_misses_combined;		//with VC


	p_stats->miss_penalty = 200;														//Based on project pdf
	p_stats->hit_time = 2 + 0.2*set_bit;												//Based on project pdf
	p_stats->miss_rate = (double)p_stats->misses / p_stats->accesses;					//MISS Rate for only L1
	double miss_rate_with_VC = (double)p_stats->misses_with_VC / p_stats->accesses;
	p_stats->avg_access_time = 	p_stats->hit_time +
							   (miss_rate_with_VC * (double)p_stats->miss_penalty);		//Hah! ATT is with VC

	p_stats->blocks_need_prefetch = p_stats->stride_match * prefetch_deg;

	p_stats->bytes_read = (p_stats->read_misses_combined + p_stats->write_misses_combined)
							* pow(2,offset_bit);										//from read+write misses

	p_stats->bytes_written = p_stats->write_backs * pow(2,offset_bit);					//
	p_stats->bytes_prefetched = p_stats->prefetched_blocks * pow(2,offset_bit);			//Prefetcher is dumb! it brings it then check
	p_stats->bytes_transferred = p_stats->bytes_written + p_stats->bytes_read
														+ p_stats->bytes_prefetched;
}

/**
 * Print stats
 */
 void cacheSim::printStatistics() {
		printf("Cache Statistics\n");
		printf("Accesses: %" PRIu64 "\n", p_stats->accesses);
		printf("Reads: %" PRIu64 "\n", p_stats->reads);
		printf("Read misses: %" PRIu64 "\n", p_stats->read_misses);
		printf("Read misses combined: %" PRIu64 "\n", p_stats->read_misses_combined);
		printf("Writes: %" PRIu64 "\n", p_stats->writes);
		printf("Write misses: %" PRIu64 "\n", p_stats->write_misses);
		printf("Write misses combined: %" PRIu64 "\n", p_stats->write_misses_combined);
		printf("Misses: %" PRIu64 "\n", p_stats->misses);
		printf("Writebacks: %" PRIu64 "\n", p_stats->write_backs);
		printf("Victim cache misses: %" PRIu64 "\n", p_stats->vc_misses);
		printf("Prefetched blocks: %" PRIu64 "\n", p_stats->prefetched_blocks);
		printf("Useful prefetches: %" PRIu64 "\n", p_stats->useful_prefetches);
		printf("Bytes transferred to/from memory: %" PRIu64 "\n", p_stats->bytes_transferred);
		printf("Hit Time: %f\n", p_stats->hit_time);
		printf("Miss Penalty: %.0f\n", p_stats->miss_penalty);
		printf("Miss rate: %f\n", p_stats->miss_rate);
		printf("Average access time (AAT): %f\n", p_stats->avg_access_time);

		/*
		printf("\n----My Statistics--------------\n");
		printf("Evictions: %" PRIu64 "\n", p_stats->evictions);
		printf("Misses with VC: %" PRIu64 "\n", p_stats->misses_with_VC);
		printf("Read hits: %" PRIu64 "\n", p_stats->read_hits);
		printf("Wirte hits: %" PRIu64 "\n", p_stats->write_hits);
		printf("Bytes Read: %" PRIu64 "\n", p_stats->bytes_read);
		printf("Bytes Written: %" PRIu64 "\n", p_stats->bytes_written);
		printf("VC Hits Reads: %" PRIu64 "\n", p_stats->VC_hit_read);
		printf("VC Hits Writes: %" PRIu64 "\n", p_stats->VC_hit_write);
		printf("VC Hits: %" PRIu64 "\n", p_stats->VC_hits);
		printf("Stride Match: %" PRIu64 "\n", p_stats->stride_match);
		printf("Prefetch Hits: %" PRIu64 "\n", p_stats->prefetch_hit);
		printf("Raw Blocks for Prefetch: %" PRIu64 "\n", p_stats->blocks_need_prefetch);
		printf("Blocks Actually Prefetch: %" PRIu64 "\n", p_stats->blocks_actually_prefetched);
		printf("Prefetches that was useful before: %" PRIu64 "\n", p_stats->prefetch_again++);
		*/
 }

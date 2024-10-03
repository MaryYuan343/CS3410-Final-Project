#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "cache.h"
#include "print_helpers.h"

cache_t *make_cache(int capacity, int block_size, int assoc, enum protocol_t protocol, bool lru_on_invalidate_f)
{
  cache_t *cache = malloc(sizeof(cache_t));
  cache->stats = make_cache_stats();

  cache->capacity = capacity;     // in Bytes
  cache->block_size = block_size; // in Bytes
  cache->assoc = assoc;           // 1, 2, 3... etc.

  // init n_cache_line to capacity divided by block size
  cache->n_cache_line = capacity / block_size;
  // init n_set to capacity divided by (block size times assoc)
  cache->n_set = capacity / (block_size * assoc);
  // init n_offset_bit to log2 of block size
  cache->n_offset_bit = log2(block_size);
  // init n_index_bit to log2 of n_set
  cache->n_index_bit = log2(cache->n_set);
  // init n_tag_bit to ADDRESS_SIZE - n_offset_bit - n_index_bit
  cache->n_tag_bit = ADDRESS_SIZE - cache->n_offset_bit - cache->n_index_bit;

  // next create the cache lines and the array of LRU bits
  // - malloc an array with n_rows
  // - for each element in the array, malloc another array with n_col
  // malloc cache->lines to be a 2d array with n_set rows and assoc columns
  cache->lines = malloc(cache->n_set * sizeof(cache_line_t *));
  for (int i = 0; i < cache->n_set; i++)
  {
    cache->lines[i] = malloc(cache->assoc * sizeof(cache_line_t));
  }
  // malloc cache->lru_way to be an array of ints with n_cache_line elements
  cache->lru_way = malloc(cache->n_cache_line * sizeof(int));

  // initializes cache tags to 0, dirty bits to false,
  // state to INVALID, and LRU bits to 0
  for (int i = 0; i < cache->n_set; i++)
  {
    for (int j = 0; j < cache->assoc; j++)
    {
      cache->lines[i][j].tag = 0;
      cache->lines[i][j].dirty_f = false;
      cache->lines[i][j].state = INVALID;
    }
    cache->lru_way[i] = 0;
  }

  cache->protocol = protocol;
  cache->lru_on_invalidate_f = lru_on_invalidate_f;

  return cache;
}

/* Given a configured cache, returns the tag portion of the given address.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_tag(0b111101010001) returns 0b1111
 * in decimal -- get_cache_tag(3921) returns 15
 */
unsigned long get_cache_tag(cache_t *cache, unsigned long addr)
{
  return addr >> (ADDRESS_SIZE - cache->n_tag_bit);
}

/* Given a configured cache, returns the index portion of the given address.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_index(0b111101010001) returns 0b0101
 * in decimal -- get_cache_index(3921) returns 5
 */
unsigned long get_cache_index(cache_t *cache, unsigned long addr)
{
  return (addr >> cache->n_offset_bit) & ((1 << cache->n_index_bit) - 1);
}

/* Given a configured cache, returns the given address with the offset bits zeroed out.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_block_addr(0b111101010001) returns 0b111101010000
 * in decimal -- get_cache_block_addr(3921) returns 3920
 */
unsigned long get_cache_block_addr(cache_t *cache, unsigned long addr)
{
  return addr & ~((1 << cache->n_offset_bit) - 1);
}

bool msi_cache(cache_t *cache, unsigned long addr, enum action_t action)
{
  unsigned long tag1 = get_cache_tag(cache, addr);
  unsigned long index = get_cache_index(cache, addr);

  bool hit = false;
  bool writeback = false;
  bool upgrade_miss = false;
  int way = cache->lru_way[index];

  // check if the tag is in the cache, if it is, then set hit equal to true and exit the for loop
  for (int i = 0; i < cache->assoc; i++)
  {
    if (cache->lines[index][i].tag == tag1 && cache->lines[index][i].state != INVALID)
    {
      // if the data is there, then way is equal to i
      way = i;
      hit = true;
      break;
    }
  }

  if (!hit) // cache miss
  {
    // load miss or store miss: stay in invalid
    if (action == LD_MISS || action == ST_MISS)
    {
      update_stats(cache->stats, hit, writeback, upgrade_miss, action);
      calculate_stat_rates(cache->stats, cache->block_size);
      return hit;
    }

    // if the bits are dirty, writeback
    if (cache->lines[index][way].dirty_f && cache->lines[index][way].state != INVALID)
    {
      writeback = true;
      cache->lines[index][way].dirty_f = false;
    }

    cache->lines[index][way].tag = tag1;

    if (action == LOAD)
    {
      cache->lines[index][way].state = SHARED;
    }

    // transitioning from invalid to modified
    if (action == STORE)
    {
      cache->lines[index][way].state = MODIFIED;
      cache->lines[index][way].dirty_f = true;
    }

    // change lru way
    if (cache->assoc > 1)
    {
      cache->lru_way[index] = (way + 1) % cache->assoc;
    }
  }
  else // cache hit
  {
    // stay in modified
    if (action == STORE || action == LOAD)
    {
      cache->lru_way[index] = (way + 1) % cache->assoc;
    }

    if (action == STORE)
    {
      // transition from shared to modified
      if (cache->lines[index][way].state == SHARED)
      {
        cache->lines[index][way].state = MODIFIED;
        hit = false;
        upgrade_miss = true;
      }
      cache->lines[index][way].dirty_f = true;
    }

    if (action == LD_MISS)
    {
      if (cache->lines[index][way].state == MODIFIED)
      {
        cache->lines[index][way].state = SHARED;
        cache->lines[index][way].dirty_f = false;
        writeback = true;
      }
    }

    // transitions if ST_MISS in either modified or shared
    if (action == ST_MISS)
    {
      cache->lines[index][way].state = INVALID;
      writeback = cache->lines[index][way].dirty_f;
      if (cache->lru_on_invalidate_f)
        cache->lru_way[index] = (way) % cache->assoc;
    }
  }

  // log sets and ways for simulator trace
  log_set(index);
  log_way(way);

  update_stats(cache->stats, hit, writeback, upgrade_miss, action);
  calculate_stat_rates(cache->stats, cache->block_size);
  return hit;
}

/* this method takes a cache, an address, and an action
 * it proceses the cache access. functionality in no particular order:
 *   - look up the address in the cache, determine if hit or miss
 *   - update the LRU_way, cacheTags, state, dirty flags if necessary
 *   - update the cache statistics (call update_stats)
 * return true if there was a hit, false if there was a miss
 * Use the "get" helper functions above. They make your life easier.
 */
bool access_cache(cache_t *cache, unsigned long addr, enum action_t action)
{
  if (cache->protocol == MSI)
  {
    return msi_cache(cache, addr, action);
  }

  // first, get the tag and index from the address
  unsigned long tag1 = get_cache_tag(cache, addr);
  unsigned long index = get_cache_index(cache, addr);

  bool hit = false;
  bool writeback = false;
  bool upgrade_miss = false;
  int way = cache->lru_way[index];

  // check if the tag is in the cache, if it is, then set hit equal to true and exit the for loop
  for (int i = 0; i < cache->assoc; i++)
  {
    if (cache->lines[index][i].tag == tag1 && cache->lines[index][i].state == VALID)
    {
      // if the data is there, then way is equal to i
      way = i;
      hit = true;
      break;
    }
  }

  // if the tag is not in the cache (if it missed)
  if (!hit)
  {
    if (action == STORE || action == LOAD)
    { // if the bit is dirty
      if (cache->lines[index][way].dirty_f)
      {
        writeback = true;
        cache->lines[index][way].dirty_f = false;
      }
      cache->lines[index][way].tag = tag1;
      cache->lines[index][way].state = VALID;
    }
  }

  // if it hits
  else
  {
    if (action == LD_MISS || action == ST_MISS)
    {
      if (cache->protocol == VI)
      {
        if (cache->lines[index][way].dirty_f && cache->lines[index][way].state == VALID)
        {
          writeback = true;
          cache->lines[index][way].dirty_f = false;
        }
        cache->lines[index][way].state = INVALID;
      }
    }

    if (action == STORE || action == LOAD)
    {
      cache->lines[index][way].state = VALID;
      // set dirty bit to true
      if (action == STORE)
      {
        cache->lines[index][way].dirty_f = true;
      }
    }
  }

  // if it is set associative (more than one way)
  if (cache->assoc > 1)
  {
    cache->lru_way[index] = (way + 1) % cache->assoc;
  }

  log_set(index);
  log_way(way);

  update_stats(cache->stats, hit, writeback, upgrade_miss, action);
  calculate_stat_rates(cache->stats, cache->block_size);

  return hit;
}

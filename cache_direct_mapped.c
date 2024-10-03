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

  // FIX THIS CODE!
  // first, correctly set these 5 variables. THEY ARE ALL WRONG
  // note: you may find math.h's log2 function useful
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
  // FIX THIS CODE!
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
  // FIX THIS CODE!
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
  // FIX THIS CODE!
  // first, get the tag and index from the address
  unsigned long tag1 = get_cache_tag(cache, addr);
  unsigned long index = get_cache_index(cache, addr);

  // implement lookup for a direct-mapped cache
  // check if the tag matches the tag in the cache line
  // if it does, update the cache line, update the stats, and return true
  // print all tags in cache line

  for (int i = 0; i < cache->assoc; i++)
  {
    printf("%lu\n", cache->lines[index][i].tag);
    if (cache->lines[index][i].tag == tag1)
    {
      update_stats(cache->stats, true, false, false, action);
      return true;
    }
    cache->lines[index][i].tag = tag1;
  }

  update_stats(cache->stats, false, false, false, action);

  // if the index does not match any index in the first row of the cache 2d array, return false
  return false;

  // return true;  // cache hit should return true
}

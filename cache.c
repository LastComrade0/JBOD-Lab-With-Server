#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

/*typedef struct {
  bool valid;
  int disk_num;
  int block_num;
  uint8_t block[JBOD_BLOCK_SIZE];
  int access_time;
} cache_entry_t;
*/

int cache_create(int num_entries) {

  //Check if no cache is created
  if(cache == NULL){
    //Check if number of entries is in bound
    if((num_entries <= 1) || (num_entries > 4096)){
      return -1;
    }
    
    
    cache_size = num_entries; //Declare cache Size as number of entries
    cache = calloc (num_entries, sizeof(cache_entry_t)); //Create a dynamic array of cache

    return 1;
  }
  
  //Check if cache is NULL to avoid creating cache again
  if(cache != NULL){
    return -1;
  }

  return -1;

}

int cache_destroy(void) {
  if(cache != NULL){
    free(cache); //Freeing cache does not set it to NULL
    cache = NULL; //Make cache NULL again
    return 1;
  }

  if(cache == NULL){
    return -1;
  }

  return -1;

}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  //Check if cache is NULL
  if(cache == NULL){
    return -1;
  }

  //Check if buf is NULL
  if(buf == NULL){
    return -1;
  }

  if(cache != NULL){
    for(int search_cache = 0; search_cache < cache_size; search_cache += 1 ){
      //If entry is found
      if( (cache[search_cache].disk_num == disk_num) && (cache[search_cache].block_num == block_num) && (cache[search_cache].valid == true )){ // && (cache[search_cache].valid == true)
        memcpy(buf, cache[search_cache].block, 256);
        

        num_queries += 1;
        num_hits += 1;
        clock += 1;
        cache[search_cache].access_time = clock;
        return 1;
        break;
      }

      /*else{
        num_queries += 1;
        //return -1;
      }*/


    }
    //Still increment query if entry is not found
    num_queries += 1;
  }
  
  return -1; //If nothing is found in cache?

}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  
  for(int search_cache = 0; search_cache < cache_size; search_cache += 1 ){
    if( (cache[search_cache].disk_num == disk_num) && (cache[search_cache].block_num == block_num) && (cache[search_cache].valid == true) ){
      memcpy(cache[search_cache].block, buf, 256);
      clock += 1;
      cache[search_cache].access_time = clock;
    }
  }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  //Check if cache is NULL
  if(cache == NULL){
    return -1;
  }

  //Check if buff is NULL
  if(buf == NULL){
    return -1;
  }

  //Check if disk/block is out of boundary
  if(disk_num > 16 || disk_num < 0 || block_num > 256 || block_num < 0){
    return -1;
  }

  //First look if there is empty slot for cache or identical entry
  for(int search_cache = 0; search_cache < cache_size; search_cache += 1 ){

    //Special case if disk & block are 0 but invalid, mainly when inserting(disk 0, block 0)
    if( (cache[search_cache].disk_num == disk_num) && (cache[search_cache].block_num == block_num) && (cache[search_cache].valid == false) ){
      memcpy(cache[search_cache].block, buf, 256);
      cache[search_cache].disk_num = disk_num;
      cache[search_cache].block_num = block_num;
      cache[search_cache].valid = true;

      clock += 1;
      cache[search_cache].access_time = clock;
      return 1;
      break;
      
    }

    //Check if there is empty place in cache
    if( (cache[search_cache].disk_num != disk_num) && (cache[search_cache].block_num != block_num) && (cache[search_cache].valid == false ) ){
      memcpy(cache[search_cache].block, buf, 256);
      cache[search_cache].disk_num = disk_num;
      cache[search_cache].block_num = block_num;
      cache[search_cache].valid = true;

      clock += 1;
      cache[search_cache].access_time = clock;
      return 1;
      break;
    }

    //Check if there is identical entry to avoid a repeat insert
    if((cache[search_cache].disk_num == disk_num) && (cache[search_cache].block_num == block_num) && (cache[search_cache].valid == true )){
      return -1;
      break;
    }

  }


  //Search for LRU
  int smallest = cache[0].access_time;
  int smallest_cache = 0;
  for(int search_cache = 0; search_cache < cache_size; search_cache++ ){
    if(cache[search_cache].access_time < smallest){
      smallest = cache[search_cache].access_time;
      smallest_cache = search_cache;
    }
  }

  

  //Replace the LRU cache slot to new one if the cache is full
  memcpy(cache[smallest_cache].block, buf, 256);
  cache[smallest_cache].disk_num = disk_num;
  cache[smallest_cache].block_num = block_num;
  cache[smallest_cache].valid = true;

  clock += 1;
  cache[smallest_cache].access_time = clock;

  
  return 1;
}

bool cache_enabled(void) {
  if(cache_size > 2){
    return true;
  }

  return false;
  
}

void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}

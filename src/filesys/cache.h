/* cache.h */
#ifndef CACHE_H
#define CACHE_H
#include <hash.h>
#include <inttypes.h>
#include <stdio.h>
#include "threads/malloc.h"
#include "devices/block.h"
#include "devices/ide.h"
#include <string.h>
#define SUCCESS 1
#define FAILURE 0
struct hash buffer_cache;

enum access_t
{
   READ,
   WRITE
};
void buffer_cache_init (void);
int block_cache_read (struct block *block, block_sector_t sector, void *buffer);
int block_cache_write (struct block *block, block_sector_t sector, const void *buffer);
int cache_insert (struct block *block, block_sector_t sector, const void *buffer, enum access_t);
int cache_read (struct block *block, block_sector_t sector, void *buffer);
int cache_write (struct block *block, block_sector_t sector, void *buffer);
bool cache_is_full(void);
void evict_cache_line(void);

unsigned block_hash (const struct hash_elem *p_, void *aux UNUSED);
bool block_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
struct cache_entry * cache_lookup (void *address);

#endif

/* cache.h */

struct cache_entry
{
   void * block;               /* Pointer to a block */
   bool dirty;                 /* Indicates if this entry was modified */
   bool read_only;             /* R/W */
   struct hash_elem hash_elem; /* Each cache_entry is an element in the buffer_cache hash table */
}

int block_cache_read (struct block *block, block_sector_t sector, void *buffer);
void block_cache_write (struct block *block, block_sector_t sector, const void *buffer);
void cache_insert (block_sector_t sector);
void cache_read (void);
void cache_write (void);
bool cache_is_full(void);
void evict_cache_line(void);

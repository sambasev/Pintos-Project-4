/* cache.c */
struct cache_entry
{
   void * block;               /* Pointer to a block */
   bool dirty;                 /* Indicates if this entry was modified */
   bool read_only;             /* R/W */
   struct hash_elem hash_elem; /* Each cache_entry is an element in the buffer_cache hash table */
}

/* Best effort cache_read */
int block_cache_read (struct block *block, block_sector_t sector, void *buffer)
{
   if (!cache_read (buffer, sector))
     {
        evict_cache_line ();   /* Cache is full, evict cache line  */
        cache_insert (sector, READ);
        set_accessed (sector);
     }
}

/* Best effort cache_write */
void block_cache_write (struct block *block, block_sector_t sector, const void *buffer)
{
   if (!cache_write (buffer, sector))
     {
        evict_cache_line ();
        cache_insert (sector, WRITE);
        set_dirty (sector);
     }
}

void cache_insert (block_sector_t sector)
{
   if (cache_is_full(void))
     {
        evict_cache_line(void);
     }
   else
     {
        /* Insert Cache entry */
     }
}

/* If entry found in cache, reads into buffer and returns success.
   Else, if cache is not full, fills cache with entry and returns success
   Fails only if entry not found and cache is full */
void cache_read ()
{
}
/* If entry found in cache, writes buffer into it, and returns success.
   Else, if cache not full, fills cache with entry and returns success.
   Fails only if entry not found and cache is full */
void cache_write ()
{
}

bool cache_is_full(void)
{
   return (entries_in_cache == CACHE_SIZE);
}

void evict_cache_line(void)
{
   /* Least Recently Used Algorithm */
}
                                      


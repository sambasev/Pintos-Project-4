/* cache.c */
struct cache_entry
{
   void * block;               /* Pointer to a block */
   bool dirty;                 /* Indicates if this entry was modified */
   bool read_only;             /* R/W */
   struct hash_elem hash_elem; /* Each cache_entry is an element in the buffer_cache hash table */
}

void buffer_cache_init (void)
{
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
        evict_cache_line ();   /* Cache is full, evict cache line  */
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



/* HASH TABLE ACCESSES FOR BUFFER CACHE */
                                      
unsigned block_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct cache_entry *p = hash_entry (p_, struct cache_entry, hash_elem);
  return hash_bytes (&p->block, sizeof p->block);
}

bool block_less (const struct hash_elem *a_, const struct hash_elem *b_,
                void *aux UNUSED)
{
  const struct cache_entry *a = hash_entry (a_, struct cache_entry, hash_elem);
  const struct cache_entry *b = hash_entry (b_, struct cache_entry, hash_elem);

  return a->block < b->block;
}

struct cache_entry * block_lookup (void *address)
{
  struct thread *t = thread_current();
  struct cache_entry p;
  struct hash_elem *e;
  p.block = address;
  e = hash_find (&t->buffer_cache, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct cache_entry, hash_elem) : NULL;
}

void insert_cache (struct cache_entry * entry)
{
  struct thread *t = thread_current();
  hash_insert (&t->buffer_cache, &cache_entry->hash_elem);
}


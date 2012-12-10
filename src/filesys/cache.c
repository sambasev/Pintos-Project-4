/* cache.c */

#include "filesys/cache.h"
#include <hash.h>
#include <inttypes.h>
#include <stdio.h>
#include <bitmap.h>
#include "threads/malloc.h"
#include "devices/block.h"
#include "devices/ide.h"
#include "devices/timer.h"

/* Debug macro taken from xappsoftware blog */
#define DEBUG 0
#if DEBUG
   #define LOG printf 
#else
   #define LOG(format, args...) ((void)0)
#endif
#define CACHE_SIZE 64
#define THIRTY_SECONDS TIMER_FREQ * 30

struct cache_entry
{
   struct hash_elem hash_elem; /* Each cache_entry is an element in the buffer_cache hash table */
   struct list_elem list_elem; /* Member in lru list */
   block_sector_t sector;      /* The key is block_sector_t and the data returned is data */
   void * data;                /* Actual data read from the block */
   bool accessed;	       /* Entry accessed  */
   bool dirty;                 /* Indicates if this entry was modified */
};

struct hash buffer_cache;
struct list lru;	       /* Used for evicting LRU cache line */	
struct list_elem * update_lru (struct cache_entry *e, bool insert);

int entries_in_cache = 0;
int disk_access = 0;	       /* Used for measuring performance improvement due to cache */	
int total_access = 0;
int64_t time = 0;

void buffer_cache_init (void)  		  
{
   /* Hash Table Initialization */
   hash_init (&buffer_cache, block_hash, block_less, NULL);
   list_init (&lru);
   time = timer_ticks ();      /* Get current time */
}

/* Best effort cache_read */
int block_cache_read (struct block *block, block_sector_t sector, void *buffer)
{
   total_access++;
   timer_update();		/* Any cache access updates timer */
   return cache_read (block, sector, buffer);
}

int block_cache_read_partial (struct block *block, block_sector_t sector, 
          void *buffer, int ofs, int chunk_size)
{
   uint8_t *bounce = malloc (BLOCK_SECTOR_SIZE);
   int rc = block_cache_read (block, sector, bounce);	
   memcpy (buffer, bounce + ofs, chunk_size);
   free (bounce); 	
   bounce = NULL;
   return rc;
}
/* Best effort cache_write */
int block_cache_write (struct block *block, block_sector_t sector, const void *buffer)
{
   total_access++;
   timer_update();		/* Any cache access updates timer */
   return cache_write (block, sector, buffer);
}

int block_cache_write_partial (struct block * block, block_sector_t sector, 
	  void *buffer, int ofs, int chunk_size)
{
   uint8_t *bounce = malloc (BLOCK_SECTOR_SIZE);
   int rc = SUCCESS;
   if (ofs > 0 || chunk_size < (BLOCK_SECTOR_SIZE - ofs))
     {
	rc = block_cache_read (block, sector, bounce);
     }       
   else
     {
	memset (bounce, 0, BLOCK_SECTOR_SIZE);
     }  
   memcpy (bounce + ofs, buffer, chunk_size);
   block_cache_write (block, sector, bounce);
   free (bounce);
   bounce = NULL;
   return rc;
}

int cache_insert (struct block *block, block_sector_t sector, void *buffer, enum access_t access)
{
   struct cache_entry *buf = malloc (sizeof(struct cache_entry));	
   buf->data = malloc(BLOCK_SECTOR_SIZE);	
   buf->sector = sector;
   if (access == WRITE) 	/* write-behind cache: write to disk on evict */
     {
       buf->dirty = true;			
       memcpy (buf->data, buffer, BLOCK_SECTOR_SIZE);
     }
   else				/* Read from disk and populate cache */
     {			      
       block_read (block_get_role (BLOCK_FILESYS), sector, buf->data); 
       disk_access++;
       memcpy (buffer, buf->data, BLOCK_SECTOR_SIZE);
     }
   struct list_elem *full = update_lru (buf, true);
   if (full)
     {
       cache_evict (full);
     }
   hash_insert (&buffer_cache, &buf->hash_elem);
   entries_in_cache++;
   return SUCCESS;
}

/* If entry found in cache, reads into buffer and returns success.
   Else fills cache with entry. Returns failure if cache insert failed */
int cache_read (struct block *block, block_sector_t sector, void *buffer)
{
   struct cache_entry *found = cache_lookup (sector);
   if (found)
     {
	found->accessed = true;
	memcpy (buffer, found->data, BLOCK_SECTOR_SIZE);
	/* Update corresponding item in lru list */
	update_lru (found, false);    
	return SUCCESS;
     }
   else
     {
	return (cache_insert (block, sector, buffer, READ));
     }
}

/* If entry found in cache, writes buffer into it, and returns success.
   Else, if cache not full, fills cache with entry and returns success.
   Fails only if entry not found and cache is full */
int cache_write (struct block *block, block_sector_t sector, const void *buffer)
{
   struct cache_entry *found = cache_lookup (sector);
   if (found)
     {
	found->dirty = true;
	found->accessed = true;
	memcpy (found->data, buffer, BLOCK_SECTOR_SIZE);
	/* TODO: Synch call to hash access so only one person can modify cache AND lru list */
	/* Update Hash Table */
	struct hash_elem *old = hash_replace (&buffer_cache, &found->hash_elem); 
	update_lru (found, false);
	return SUCCESS;
     }
   else 
     {
	return cache_insert (block, sector, buffer, WRITE);	
     }
}

void cache_evict (struct list_elem *e)
{
   if (e)
     {
        struct cache_entry *lru_entry = list_entry (e, struct cache_entry, list_elem);
     	/* Write to disk only if dirty */
        if (lru_entry->dirty == true)
          {
     	    block_write (block_get_role (BLOCK_FILESYS), lru_entry->sector, lru_entry->data); 
	    disk_access++;
	    free (lru_entry->data);
	    lru_entry->data = NULL;
   	  }
        /* Delete line from buffer_cache */ 	
        hash_delete (&buffer_cache, &lru_entry->hash_elem);
	/* Free resources */
        free (lru_entry);			
	lru_entry = NULL;

        entries_in_cache--;
     }
}

bool cache_is_full (void)
{
   return (entries_in_cache >= CACHE_SIZE);
}

/* Called periodically (every 30 seconds) by timer interrupt event */
void cache_flush (void)
{
   while (!list_empty(&lru))
      {
	cache_evict (list_pop_front(&lru));
      } 
}

/* Every 30 seconds or so, flush the cache */
void timer_update (void)
{
   if (timer_elapsed (time) >= THIRTY_SECONDS)
     {
	time = timer_ticks();		/* Reset time */
	cache_flush ();
     }
}
/* TODO: Acquire lock before modifying list or hash */
/* If insert is true, puts element at top of list, and returns element to evict, if any.
   If insert is false, removes item from current position and puts it at the front. 
   Returns NULL on all cases except when an item needs to evict. It is the caller's
   responsibility (cache_insert() in this case) to evict the item returned.
*/
struct list_elem * update_lru (struct cache_entry *e, bool insert)
{
   if (insert==false) 
     {
        /* Access was read/write - put element at top of the list */
	list_remove (&e->list_elem);
	list_push_front (&lru, &e->list_elem);
	return NULL;
     }
   else
     {	
	/* Access is an insert - return item to evict if cache full, update lru otherwise */
	if (cache_is_full())
	  {
	     list_push_front (&lru, &e->list_elem); 
	     return list_pop_back (&lru);	
	  }
	else
	  {
	     list_push_front (&lru, &e->list_elem); 
	     return NULL;	
	  }	
     }
}

/* HASH TABLE ACCESSES FOR BUFFER CACHE - Basic hash table manipulation from Pintos Reference Doc */
                                      
unsigned block_hash (const struct hash_elem *p_, void *aux UNUSED)
{
   const struct cache_entry *p = hash_entry (p_, struct cache_entry, hash_elem);
   return hash_bytes (&p->sector, sizeof p->sector);
}

bool block_less (const struct hash_elem *a_, const struct hash_elem *b_,
                void *aux UNUSED)
{
   const struct cache_entry *a = hash_entry (a_, struct cache_entry, hash_elem);
   const struct cache_entry *b = hash_entry (b_, struct cache_entry, hash_elem);

   return a->sector < b->sector;
}

struct cache_entry * cache_lookup (block_sector_t sector)
{
   struct cache_entry p;
   struct hash_elem *e;
   p.sector = sector;
   e = hash_find (&buffer_cache, &p.hash_elem);
   return e != NULL ? hash_entry (e, struct cache_entry, hash_elem) : NULL;
}


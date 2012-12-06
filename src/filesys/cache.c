/* cache.c */

#include "filesys/cache.h"
#include <hash.h>
#include <inttypes.h>
#include <stdio.h>
#include "threads/malloc.h"
#include "devices/block.h"
#include "devices/ide.h"
/* Debug macro taken from xappsoftware blog */
#define DEBUG 0
#if DEBUG
   #define LOG printf 
#else
   #define LOG(format, args...) ((void)0)
#endif
#define BUFFER_CACHE_LINES 64

struct cache_entry
{
   struct hash_elem hash_elem; /* Each cache_entry is an element in the buffer_cache hash table */
   struct list_elem list_elem; /* Member in lru list */
   block_sector_t sector;      /* The key is block_sector_t and the data returned is data */
   char name[16]; 	       /* Block device name. You can call block_get_by_name on this */
   void * data;                /* Actual data read from the block */
   bool accessed;	       /* Used for LRU Clock Algorithm */
   bool dirty;                 /* Indicates if this entry was modified */
   bool read_only;             /* R/W */
};
struct hash buffer_cache;
static struct bitmap *free_map;
struct list lru;	       /* Used for evicting LRU cache line */	

int entries_in_cache = 0;

void buffer_cache_init (void)  /* Called in inode.c */
{
   /* Hash Table Initialization */
   hash_init (&buffer_cache, block_hash, block_less, NULL);
   list_init (&lru);
   free_map = bitmap_create (BUFFER_CACHE_LINES); /* TODO: Use this instead of entries_in_cache */
}

/* Best effort cache_read */
int block_cache_read (struct block *block, block_sector_t sector, void *buffer)
{
   LOG("<1> block: %x sector %x buffer %x\n", (uint32_t)block, (uint32_t)sector, (uint32_t) buffer);
   return cache_read (block, sector, buffer);
}

/* Best effort cache_write */
int block_cache_write (struct block *block, block_sector_t sector, const void *buffer)
{
   return cache_write (block, sector, buffer);
}

int cache_insert (struct block *block, block_sector_t sector, void *buffer, enum access_t access)
{
   if (cache_is_full())
     {
        evict_cache_line(); /* Cache is full, evict cache line and retry */
     }
   else 
     {
	/* TODO: Check malloc success */
	struct cache_entry *buf = malloc (sizeof(struct cache_entry));	
	buf->sector = sector;
	if (access == WRITE) 
	  {
	    buf->data = malloc(BLOCK_SECTOR_SIZE);	/* Write to cache. Don't touch disk */
	  }
	else
	  {
	    block_read (block_get_role (BLOCK_FILESYS), sector, buffer); /* Read from disk and populate cache */
	  }
	memcpy (buf->data, buffer, BLOCK_SECTOR_SIZE);
	struct list_elem *full = update_lru (buf, true);
	if (full)
	  {
	    cache_evict (full);
	  }
  	hash_insert (&buffer_cache, &buf->hash_elem);
	entries_in_cache++;
	LOG("entries : %d access: %d\n", entries_in_cache, access);
	return SUCCESS;
     }
   return FAILURE;
}

/* If entry found in cache, reads into buffer and returns success.
   Else fills cache with entry. Returns failure if cache insert failed */
int cache_read (struct block *block, block_sector_t sector, void *buffer)
{
   struct cache_entry *found = cache_lookup (sector);
   if (found)
     {
	found->accessed = true;
	update_lru (found, false);    /* Update corresponding item in lru list */
	memcpy (buffer, found->data, BLOCK_SECTOR_SIZE);
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
	update_lru (found, false);
	memcpy (found->data, buffer, BLOCK_SECTOR_SIZE);
	/* TODO: Synch call to hash access so only one person can modify cache AND lru list */
	struct hash_elem *old = hash_replace (&buffer_cache, &found->hash_elem);
	LOG ("cache_write \n");
	return SUCCESS;
     }
   else 
     {
	return cache_insert (block, sector, buffer, WRITE);	
     }
}

bool cache_is_full (void)
{
   LOG ("cache full");
   return (entries_in_cache >= CACHE_SIZE);
}

void cache_evict (struct list_elem *e)
{
   if (e)
     {
        struct cache_entry *lru_entry = list_entry (e, struct cache_entry, list_elem);
     	/* Write to disk only if dirty */
        if (lru_entry->dirty)
          {
     	    block_write (block_get_role (BLOCK_FILESYS), sector, buffer); 
   	  }
        /* Delete line from buffer_cache */ 	
        hash_delete (&buffer_cache, &lru_entry->hash_elem);
        free (lru_entry);
        entries_in_cache--;
     }
}

/* Called periodically (every 30 seconds) by timer interrupt event */
void flush_cache (void)
{
   
}

/* If found, entry already exists, update its position in LRU list
   Else, delete lru from list, and insert new item at list head 
   Eviction should be done by caller */
struct list_elem * UNUSED_update_lru (struct cache_entry *e, bool insert)
{
   if (list_size (&lru) >= CACHE_SIZE)	/* Might need to evict */
     {
	list_push_front (&lru, &e->list_elem);
	struct list_elem *evict_elem = list_pop_back (&lru); /* Might want to evict_cache_line */
	return evict_elem;
     }
   else
     {
	struct list_elem *found = list_find (&lru, sector);
	if (found)
          {
	     list_push_front(&lru, list_remove (found));
          }
	else
	  {
	     list_push_front (&lru, &cache_lookup(sector)->list_elem);
	  }
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
   if (!insert) /* Access was read/write - put element at top of the list */
     {
	list_push_front (&lru, list_remove (&e->list_elem));
	return NULL;
     }
   else
     {		/* Access is an insert - return item to evict if cache full, update lru otherwise */
	if (cache_is_full())
	  {
	     list_push_front (&lru, &e->list_elem); /* List will have MAX+1 items temporarily */
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


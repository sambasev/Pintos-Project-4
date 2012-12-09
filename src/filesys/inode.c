#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

#define DEBUG 0
#if DEBUG
   #define LOG printf
#else
   #define LOG(format, args...) ((void)0)
#endif
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
/* DONT CHANGE - each struct that goes on disk can only be
   BLOCK_SECTOR_SIZE bytes long, so touching this should 
   add an unused padding, and vice-versa */
#define DIRECT_BLOCKS 10		
#define INDIRECT_BLOCKS 125		
#define DBL_INDIRECT_BLOCKS 125	
#define TOTAL_BLOCKS 15760	
#define MAX_FILE_SIZE 8069120		/* Appx. 8 MB */
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* UNUSED First data sector. */

    block_sector_t blocks[DIRECT_BLOCKS];/* For files <5 kB */
    off_t length;                       /* File size in bytes. */
    block_sector_t self;		/* Block in which this inode is stored. 
					   Same as sector in struct inode*/
    block_sector_t indirect;		/* Block where inode_indirect struct is stored */
    block_sector_t dbl_indirect;  	/* Block where inode_dbl_indirect struct is stored */ 
    unsigned indirect_used;		/* Indicates if indirect used */
    unsigned dbl_indirect_used ;	/* Indicates if dbl_indirect is used */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[110];               /* Not used. */
  };

/* On-disk indirect block - Each indirect block contains an array of 125 
   blocks each of which can hold BLOCK_SECTOR_SIZE bytes of data for a total
   of 125 * BLOCK_SECTOR_SIZE bytes of data */
struct inode_indirect
  {
    block_sector_t sector;		/* sector containing this data structure */
    block_sector_t parent;		/* The inode_disk to which this belongs */
    off_t length;			/* # of data blocks used */
    block_sector_t blocks[INDIRECT_BLOCKS];
					/* Each block holds data */
  };

/* On-disk double indirect block - Each dbl_indirect block can hold an array
   of 125 blocks each of which can hold 125 indirect blocks */
struct inode_dbl_indirect
  {
    block_sector_t sector;
    block_sector_t parent;              /* The inode_disk to which this belongs */
    off_t length;			/* # of indirect blocks used */
    block_sector_t indirect[INDIRECT_BLOCKS];
					/* Each block holds an indirect block */
  };
/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Each inode can address 128 + (128*128)* blocks which contain 
   a total of 8.0625 MB. The direct blocks will be used for small 
   files (for quick access)*/

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */

    off_t length;
    struct inode_indirect ind_data;     /* Each indirect block holds 128 block #s */
    struct inode_dbl_indirect dbl_indirect;
					/* One dbly indirect block can address 16384 blocks */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  struct inode_indirect *disk_indirect = NULL;
  struct inode_dbl_indirect *disk_dbindirect = NULL;
  size_t direct = 0, indirect = 0, dbl_indirect = 0, remaining = 0;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  disk_indirect = calloc (1, sizeof *disk_indirect);
  disk_dbindirect = calloc (1, sizeof *disk_dbindirect);
   
  if (disk_inode != NULL && disk_indirect != NULL && disk_dbindirect != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      if (sector_allocation (sectors, &direct, &indirect, &dbl_indirect, &remaining))
	{
	  success = true;
	}
      else				/* File too big */
	{
	  free_resources (disk_inode, disk_indirect, disk_dbindirect);
	  return false;
	}
      LOG ("<4> sectors: %d direct:%d indirect:%d dbl:%d remain:%d\n",
		(int)sectors, (int)direct, (int)indirect, (int)dbl_indirect, (int)remaining);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->self = sector;	/* Not needed */
      /* IMPORTANT TODO: What about files with 0 file_size? */
      if (direct)
	{
	  alloc_direct_sectors (disk_inode->blocks, 0, direct);
	}
      /* Allocate block in disk for struct inode_indirect */ 	
      if (indirect)
        {
          if (!free_map_allocate (1, &disk_inode->indirect))
	    {	
	      success = false;
	    }
	  else
	    {
	      alloc_indirect_sectors (disk_indirect->blocks, 0, indirect);
      	      block_cache_write (fs_device, disk_inode->indirect, disk_indirect);
	    }
        }
      /* Allocate block in disk for struct inode_dbl_indirect */
      if (dbl_indirect || remaining)
        {
	  if (!free_map_allocate (1, &disk_inode->dbl_indirect))
	    {
	      success = false;
	    }
	  else
	    {
              alloc_double_indirect_sectors (disk_dbindirect->indirect, 
	                                 0, dbl_indirect, remaining);
      	      block_cache_write (fs_device, disk_inode->dbl_indirect, disk_dbindirect);
	    }
        }
      /* TODO: If not success, roll back the writes */
      if (success)
	{
 	  block_cache_write (fs_device, sector, disk_inode);
	}
      /*if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          block_cache_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                block_cache_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true; 
        } */
      free_resources (disk_inode, disk_indirect, disk_dbindirect);
    }
  return success;
}

void free_resources (void * p1, void * p2, void * p3)
{
  free (p1);
  free (p2);
  free (p3);
}

/* Input: # of sectors. Output: how many of each type is required to fill those sectors */
bool sector_allocation (size_t sectors, size_t *direct_p, 
	size_t *indirect_p, size_t *dbl_p, size_t *remain_p)
{
  size_t direct = 0, indirect = 0, dbl = 0, remain = 0;
  
  direct = MIN (sectors, DIRECT_BLOCKS);
  sectors -= MIN (direct, sectors);
  
  indirect = MIN (sectors, INDIRECT_BLOCKS);
  sectors -= indirect;

  dbl = MIN ((sectors / INDIRECT_BLOCKS), DBL_INDIRECT_BLOCKS);
  sectors -= dbl * INDIRECT_BLOCKS;

  remain = sectors % INDIRECT_BLOCKS;
  sectors -= remain;

  *direct_p   = direct;
  *indirect_p = indirect; 
  *dbl_p      = dbl;
  *remain_p   = remain;
 
  if (sectors)
    {
      return false;	/* File too big! Return failure, but use allocated blocks */	
    }
  return true;		/* SUCCESS */
}

/* Allocates and initializes num_sectors # of blocks starting from ptr[index] 
   and returns # of sectors successfully allocated */
size_t alloc_direct_sectors (block_sector_t *ptr, int index, size_t sectors)
{
  int i = index, count = 0;
  i = index;
  static char zeros[BLOCK_SECTOR_SIZE];
  if (!sectors)
    {
      return 0;
    }
  count = MIN (sectors, DIRECT_BLOCKS);
  while (count--)
    {
      if (free_map_allocate (1, &ptr[i]))
        {
	  block_cache_write (fs_device, ptr[i], zeros); 
	  i++;
        }	
    }
  return i;
}

/* Allocates and initializes sectors # of blocks starting from ptr[index] 
   and returns # of sectors successfully allocated */
size_t alloc_indirect_sectors (block_sector_t *ptr, int index, size_t sectors)
{
  int i = index, count = 0;
  static char zeros[BLOCK_SECTOR_SIZE];
  if (!sectors)
    {
      return 0;
    }
  /* Not needed but good to have */
  count = MIN (sectors, INDIRECT_BLOCKS);
  while (count--)
    {
      if (free_map_allocate (1, &ptr[i]))
        {
	  block_cache_write (fs_device, ptr[i], zeros); 
	  i++;
        }	
    }
  return i;
}

/* Allocates double indirect blocks and initializes them */
size_t alloc_double_indirect_sectors (block_sector_t *ptr, int index,  
  					size_t sectors, size_t remaining)
{
  int i = index, count = 0;
  LOG("alloc_double_indirect_sectors index:%d sectors:%d count:%d remain:%d\n",
	(uint32_t)index, (uint32_t)sectors, (uint32_t)count, (uint32_t)remaining);
  if (!sectors && !remaining)
    {
      return 0;
    }
  count = MIN (sectors, DBL_INDIRECT_BLOCKS);
  if (remaining)
    {
      count++;				/* For the remaining sectors */
    }
  /* TODO:Only do this for all but the last indirect sector */
  while (count--)
    {
      struct inode_indirect *disk_indirect = NULL;
      disk_indirect = calloc (1, sizeof (*disk_indirect));
      if (!free_map_allocate (1, &ptr[i]))
	{
	  return 0;
	}
      if (!count && remaining)
	{
          alloc_indirect_sectors (disk_indirect->blocks, 0, remaining);	
	}
      else
	{
          alloc_indirect_sectors (disk_indirect->blocks, 0, INDIRECT_BLOCKS);	
	}
      block_cache_write (fs_device, ptr[i], disk_indirect);
      free (disk_indirect);
      i++;
    }
   return i;
}

bool eof_reached (struct inode *node, off_t pos)
{
   return (pos >= node->data.length);
}

int no_bytes (void)
{
   return 0;
}

/* Gets the block from the disk inode 
   Caller should make sure the block is a direct block before calling */
block_sector_t direct_block (struct inode *node, off_t block)
{
   ASSERT (node != NULL);
   return node->data.blocks[block]; 		    /* node->data read at inode_open */
}

block_sector_t indirect_block (struct inode *node, off_t block)
{
   ASSERT (node != NULL);
   /* Get the indirect block */
   block_cache_read (fs_device, node->data.indirect, &node->ind_data); 
   return node->ind_data.blocks[block];
}

block_sector_t dbl_indirect_block (struct inode *node, off_t block)
{
   LOG("<1> dbl_indirect_block %d\n", (uint32_t) block);
   ASSERT (node != NULL); 
   off_t indirect_block = block / DBL_INDIRECT_BLOCKS;
   off_t indirect_index = block % INDIRECT_BLOCKS;
   /* First, read double indirect block */
   block_cache_read (fs_device, node->data.dbl_indirect, &node->dbl_indirect);
   /* Get the indirect block */
   struct inode_indirect *disk_indirect = NULL;
   disk_indirect = calloc (1, sizeof (*disk_indirect));
   block_cache_read (fs_device, 
		node->dbl_indirect.indirect[indirect_block], disk_indirect);
   /* Get the actual block which contains data */
   block_sector_t data =  disk_indirect->blocks[indirect_index];
   LOG("<1> block_sector_t data %d\n", (uint32_t)data);
   free (disk_indirect);
   return data;
}

/* Returns the block corresponding to the inode struct and offset 
   If block is within a direct block, reads disk_inode and gets the block #
   If block is within an indirect block, reads disk_inode, indirect_inode and returns blcok 
   If block is within a double indirect block, reads disk_inode, dbl_indirect_inode
   then the indirect_inode and returns block */
block_sector_t get_inode_block (struct inode *node, off_t pos, bool read)
{ 
   ASSERT (node != NULL);
   ASSERT (pos < MAX_FILE_SIZE);		/* File too big */
   size_t blk = pos / BLOCK_SECTOR_SIZE; 	/* Get the block offset */
   if (pos < node->data.length)		/* Check for File Extension */
     {
       if (blk < DIRECT_BLOCKS)
    	 {
           return direct_block (node, blk);
    	 }
       if (blk < (DIRECT_BLOCKS + INDIRECT_BLOCKS)) 
         {
	   blk -= DIRECT_BLOCKS; 
           return indirect_block (node, blk);
	 }
       if (blk < TOTAL_BLOCKS)			/* Catch-all */	
         {
	   blk -= (DIRECT_BLOCKS + INDIRECT_BLOCKS);
	   LOG("<3>dbl indirect block: %d\n", (int)blk); 
           return dbl_indirect_block (node, blk);
	 }
     }
   else						/* File extension  */
     {
       if (read)
           return -1;				/* no bytes to read */
       else					/* Write will extend file */
           return extend_file (node, blk);
     }	
   return -1; 	
}

/* Extends file from current size to given offset by filling intermediate blocks
   with zeroes. Returns the block corresponding to offset */
block_sector_t extend_file (struct inode *node, size_t sectors)	
{
  ASSERT (node != NULL);
  LOG ("<1> Extend_file sectors %d node->sector %d file size: %d\n", 
	(uint32_t)sectors, (uint32_t)node->sector, (uint32_t)node->length);
  size_t old_direct = 0, old_indirect = 0, old_dbl = 0, old_remaining = 0;
  size_t new_direct = 0, new_indirect = 0, new_dbl = 0, new_remaining = 0;
  size_t cur_sectors = node->length / BLOCK_SECTOR_SIZE;
  if (cur_sectors % BLOCK_SECTOR_SIZE)
    {
      cur_sectors++;
    }  
  sector_allocation (cur_sectors, &old_direct, &old_indirect, &old_dbl, &old_remaining);
  sector_allocation (sectors, &new_direct, &new_indirect, &new_dbl, &new_remaining);
  block_sector_t new_sector = 0; 

  if (new_direct > old_direct)
    {
      alloc_direct_sectors (node->data.blocks, old_direct+1, new_direct);
    }

  if (new_indirect > old_indirect)
    {
      struct inode_indirect *disk_indirect = NULL;
      disk_indirect = calloc (1, sizeof *disk_indirect);
      block_cache_read (fs_device, node->data.indirect, disk_indirect);

      /*TODO: FIX ME  old_direct passed should not be occupied. i.e. pass old+1 */
      alloc_indirect_sectors (disk_indirect->blocks, old_direct+1, new_indirect);
      block_cache_write (fs_device, node->data.indirect, disk_indirect);    

      free (disk_indirect);
    }

  if (new_dbl > old_dbl)
    {
      struct inode_dbl_indirect * disk_dbindirect = NULL;
      disk_dbindirect = calloc (1,sizeof *disk_dbindirect);
      block_cache_read (fs_device, node->data.dbl_indirect, disk_dbindirect);
      
      alloc_double_indirect_sectors (disk_dbindirect->indirect, 
				old_dbl, new_dbl, new_remaining);
      block_cache_write (fs_device, node->data.dbl_indirect, disk_dbindirect);
      
      free (disk_dbindirect);
    }

  if (old_remaining != new_remaining)
    {

    } 
  return new_sector; 
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  //block_read (fs_device, inode->sector, &inode->data);
  block_cache_read (fs_device, inode->sector, &inode->data);
  inode->length = inode->data.length;
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      // block_sector_t sector_idx = byte_to_sector (inode, offset);
      block_sector_t sector_idx = get_inode_block (inode, offset, true);
      if ((uint32_t)offset > 69000) 
	{
          LOG("<2> offset %d size %d filesize %d\n", 
		(uint32_t)offset, (uint32_t)size, (uint32_t)inode->length);
	}
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_cache_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
	  block_cache_read_partial (fs_device, sector_idx, 
		buffer + bytes_read, sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      // block_sector_t sector_idx = byte_to_sector (inode, offset);
      block_sector_t sector_idx = get_inode_block (inode, offset, false);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_cache_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          block_cache_write_partial (fs_device, sector_idx, 
          buffer + bytes_written, sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

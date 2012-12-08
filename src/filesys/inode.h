#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

void free_resources (void * p1, void * p2, void * p3);
bool sector_allocation (size_t sectors, size_t *direct_p,
 	size_t *indirect_p, size_t *dbl_p, size_t *remain_p);
size_t alloc_direct_sectors (block_sector_t *ptr, int index, size_t sectors);
size_t alloc_indirect_sectors (block_sector_t *ptr, int index, size_t sectors);
size_t alloc_double_indirect_sectors (block_sector_t *ptr, int index,
					 size_t sectors, size_t remaining);

bool eof_reached (struct inode *node, off_t offset);
int no_bytes (void);
block_sector_t get_inode_block (struct inode *node, off_t offset);
void extend_file (struct inode *node, off_t offset);
#endif /* filesys/inode.h */

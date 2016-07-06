#define FFS_BLOCK_H
#ifndef FFS_BLOCK_H

#include "ffs_disk.h"

#define FFS_BLOCK_FREE			 0
#define FFS_BLOCK_LAST			-2
#define FFS_BLOCK_INVALID		-1

#define FFS_BLOCK_SUPERBLOCK	 0
#define FFS_BLOCK_FAT			 (FFS_BLOCK_SUPERBLOCK + 1)

// Allocate a new block after specified block
// If specified block is FFS_BLOCK_LAST then newly allocated block is first
// Returns new block on success; otherwise, returns FFS_BLOCK_INVALID
int ffs_block_alloc(ffs_disk disk, int parent_block);

// Mark all blocks in the block list starting at specified block as FFS_BLOCK_FREE
// If parent block is FFS_BLOCK_INVALID, block is assumed to have no parent
// Returns zero on success; otherwise, returns non-zero
int ffs_block_free(ffs_disk disk, int parent_block, int block);

// Get the next block in the block list
// Returns next block on success; otherwise, returns FFS_BLOCK_LAST
int ffs_block_next(ffs_disk disk, int block);

// Read entire contents of specified block to buffer
// Buffer must be size of a block
// Returns zero on success; otherwise, returns non-zero
int ffs_block_read(ffs_disk disk, int block, void *buffer);

// Write entire contents of specified block from buffer
// Buffer must be size of a block
// Returns zero on success; otherwise, returns non-zero
int ffs_block_write(ffs_disk disk, int block, const void *buffer);

#endif

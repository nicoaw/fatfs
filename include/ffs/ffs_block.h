#ifndef FFS_BLOCK_H
#define FFS_BLOCK_H

#include "ffs_disk.h"

#define FFS_BLOCK_FREE			 0
#define FFS_BLOCK_INVALID		-1
#define FFS_BLOCK_LAST			-2

#define FFS_BLOCK_SUPERBLOCK	 0
#define FFS_BLOCK_FAT			 (FFS_BLOCK_SUPERBLOCK + 1)

#define FFS_BLOCK_VALID(block) 	 (block != FFS_BLOCK_FREE || block != FFS_BLOCK_INVALID || block != FFS_BLOCK_LAST)

// A FAT filesystem block id
typedef uint32_t ffs_block;

// Allocate a block before next
// Next can be FFS_BLOCK_LAST
// Returns FFS_BLOCK_INVALID on failure
ffs_block ffs_block_alloc(ffs_disk disk, ffs_block next);

// Free head in block list
// Head must be valid
// Returns non-zero on failure
int ffs_block_free(ffs_disk disk, ffs_block head);

// Get next block in block list
// Previous must be valid or FFS_BLOCK_LAST
// Returns FFS_BLOCK_LAST when previous is last block
// Returns FFS_BLOCK_INVALID on failure
ffs_block ffs_block_next(ffs_disk disk, ffs_block previous);

// Read entire contents of specified block to buffer
// Offset must be valid
// Buffer must be size of a block
// Returns non-zero on failure
int ffs_block_read(ffs_disk disk, ffs_block offset, void *buffer);

// Write entire contents of specified block from buffer
// Offset must be valid
// Buffer must be size of a block
// Returns zero on success; otherwise, returns non-zero
int ffs_block_write(ffs_disk disk, ffs_block offset, const void *buffer);

#endif

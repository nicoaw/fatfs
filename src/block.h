#ifndef BLOCK_H
#define BLOCK_H

#include "disk.h"

// Block states
#define BLOCK_FREE			 0
#define BLOCK_INVALID		-1
#define BLOCK_LAST			-2

// Static block locations
#define BLOCK_SUPERBLOCK	 0
#define BLOCK_FAT			 (BLOCK_SUPERBLOCK + 1)

#define BLOCK_VALID(block) 	 (block != BLOCK_INVALID && block != BLOCK_LAST)

// FAT access helpers
#define BLOCK_FAT_ENTRY_COUNT(sb)	(sb->block_size / sizeof(block))
#define BLOCK_FAT_BLOCK(sb, block)	(BLOCK_FAT + block / BLOCK_FAT_ENTRY_COUNT(sb))
#define BLOCK_FAT_ENTRY(sb, block)	(block % BLOCK_FAT_ENTRY_COUNT(sb))

// A filesystem block pointer
typedef uint32_t block;

// Allocate a block before next
// Next can be BLOCK_LAST
// Returns BLOCK_INVALID on failure
block block_alloc(disk disk, block next);

// Free head in block list
// Head must be valid
// Returns non-zero on failure
int block_free(disk disk, block head);

// Get next block in block list
// Previous must be valid or BLOCK_LAST
// Returns BLOCK_LAST when previous is last block
// Returns BLOCK_INVALID on failure
block block_next(disk disk, block previous);

// Read entire contents of specified block to buffer
// Offset must be valid
// Buffer must be size of a block
// Returns non-zero on failure
int block_read(disk disk, block offset, void *buffer);

// Write entire contents of specified block from buffer
// Offset must be valid
// Buffer must be size of a block
// Returns zero on success; otherwise, returns non-zero
int block_write(disk disk, block offset, const void *buffer);

#endif

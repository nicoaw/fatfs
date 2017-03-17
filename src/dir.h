#ifndef DIR_H
#define DIR_H

#include "block.h"

// Test address validity using superblock
// Returns zero when invalid
#define DIR_ADDRESS_VALID(sb, address) (BLOCK_VALID(address.end_block) && address.end_offset <= sb->block_size)

// Perform only directory read access
#define dir_read(d, offset, data, size) dir_access(d, offset, data, NULL, size)

// Perform only directory write access
#define dir_write(d, offset, data, size) dir_access(d, offset, NULL, data, size)

// A pointer to data
typedef struct {
	block end_block; // Block at end of data access
	uint32_t end_offset; // Offset at past-the-end of data access in a block
} address;

// An invalid pointer
extern const address DIR_ADDRESS_INVALID;

// Access at most size bytes of data from offset
// Stops accessing at end of block list
// Returns amount of bytes accessed
uint32_t dir_access(disk d, address offset, void *readdata, const void *writedata, uint32_t size);

// Seek an address by offset backwards
// Returns invalid address on failure
address dir_seek(disk d, address addr, uint32_t offset);

#endif

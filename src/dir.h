#ifndef DIR_H
#define DIR_H

#include "disk.h"

#define DIR_NAME_LENGTH		 23

// Directory flags
#define DIR_DIRECTORY		 1
#define DIR_FILE			 2

// Offset to access entry for read/write
#define DIR_ENTRY_OFFSET	-1

#define DIR_ADDRESS_VALID(sb, address) (BLOCK_VALID(address.block) && address.offset < sb->block_size)

// A FAT filesystem directory entry
struct __attribute__((__packed__)) entry {
    char name[DIR_NAME_LENGTH + 1];
    uint64_t create_time;
    uint64_t modify_time;
    uint64_t access_time;
    uint32_t size;
    uint32_t start_block;
    uint32_t flags;
    uint32_t unused;
};

// A pointer to FAT filesystem directory entry
typedef struct {
	uint32_t block;
	uint32_t offset;
} address;

extern const address DIR_ADDRESS_INVALID;

// Allocate size bytes at beginning of directory
// Returns non-zero on failure
int dir_alloc(disk disk, address entry, uint32_t size);

// Free size bytes at beginning of directory
// Returns non-zero on failure
int dir_free(disk disk, address entry, uint32_t size);

// Find directory entry address from absolute path
// Returns invalid address on failure
address dir_find(disk disk, const char *path);

// Reads at most size bytes of data from offset
// Stops reading at end of directory
// Reads from entry when offset is DIR_ENTRY_OFFSET, size should be size of entry
// Returns count of bytes read
uint32_t dir_read(disk disk, address entry, uint32_t offset, void *data, uint32_t size);

// Writes at most size bytes of data to offset
// Stops reading at end of directory
// Writes to entry when offset is DIR_ENTRY_OFFSET, size should be size of entry
// Returns count of bytes written
uint32_t dir_write(disk disk, address entry, uint32_t offset, const void *data, uint32_t size);

#endif

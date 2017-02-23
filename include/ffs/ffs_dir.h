#ifndef FFS_DIR_H
#define FFS_DIR_H

#include "ffs_disk.h"

#define FFS_DIR_NAME_LENGTH		 23

// Directory flags
#define FFS_DIR_DIRECTORY		 1
#define FFS_DIR_FILE			 2

// Offset to access entry for read/write
#define FFS_DIR_ENTRY_OFFSET	-1

#define FFS_DIR_ADDRESS_VALID(sb, address) (FFS_BLOCK_VALID(address.block) && address.offset < sb->block_size)

// A FAT filesystem directory entry
struct __attribute__((__packed__)) ffs_entry {
    char name[FFS_DIR_NAME_LENGTH + 1];
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
} ffs_address;

extern const ffs_address FFS_DIR_ADDRESS_INVALID;

// Allocate size bytes at beginning of directory
// Returns non-zero on failure
int ffs_dir_alloc(ffs_disk disk, ffs_address entry, uint32_t size);

// Free size bytes at beginning of directory
// Returns non-zero on failure
int ffs_dir_free(ffs_disk disk, ffs_address entry, uint32_t size);

// Find directory entry address from absolute path
// Returns invalid address on failure
ffs_address ffs_dir_find(ffs_disk disk, const char *path);

// Reads at most size bytes of data from offset
// Stops reading at end of directory
// Reads from entry when offset is FFS_DIR_ENTRY_OFFSET, size should be size of entry
// Returns count of bytes read
uint32_t ffs_dir_read(ffs_disk disk, ffs_address entry, uint32_t offset, void *data, uint32_t size);

// Writes at most size bytes of data to offset
// Stops reading at end of directory
// Writes to entry when offset is FFS_DIR_ENTRY_OFFSET, size should be size of entry
// Returns count of bytes written
uint32_t ffs_dir_write(ffs_disk disk, ffs_address entry, uint32_t offset, const void *data, uint32_t size);

#endif

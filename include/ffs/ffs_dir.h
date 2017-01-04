#ifndef FFS_DIR_H
#define FFS_DIR_H

#include "ffs_disk.h"

#define FFS_DIR_NAME_LENGTH		 23

// Directory flags
#define FFS_DIR_DIRECTORY		 1
#define FFS_DIR_FILE			 2

#define FFS_DIR_ADDRESS_VALID(address) (FFS_BLOCK_VALID(address.block))

// A FAT filesystem directory or file information
struct __attribute__((__packed__)) ffs_directory {
    char name[FFS_DIR_NAME_LENGTH + 1];
    uint64_t create_time;
    uint64_t modify_time;
    uint64_t access_time;
    uint32_t length;
    uint32_t start_block;
    uint32_t flags;
    uint32_t unused;
};

// A pointer to data in a FAT filesystem directory
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

// Find directory entry address given relative path from root
// Returns invalid address on failure
ffs_address ffs_dir_find(ffs_disk disk, ffs_address root, const char *path);

// Read size bytes of data from offset
// Returns non-zero on failure
int ffs_dir_read(ffs_disk disk, ffs_address offset, void *data, uint32_t size);

// Get root address
// Returns invalid address on failure
ffs_address ffs_dir_root(ffs_disk disk);

// Get address offset bytes from start
// Returns invalid address on failure
ffs_address ffs_dir_seek(ffs_disk disk, ffs_address start, uint32_t offset);

// Write size bytes of data to offset
// Returns non-zero on failure
int ffs_dir_write(ffs_disk disk, ffs_address offset, const void *data, uint32_t size);

#endif

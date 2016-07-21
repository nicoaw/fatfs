#ifndef FFS_DIR_H
#define FFS_DIR_H

#include "ffs_disk.h"

#define FFS_DIR_DIRECTORY		 0
#define FFS_DIR_FILE			 1

// An pointer to a FAT filesystem directory
typedef struct {
	int block;
	int directory_index;
} ffs_address;

extern const ffs_address FFS_DIR_ADDRESS_INVALID;

// A FAT filesystem directory or file information
struct ffs_directory {
    char name[24];
    uint64_t create_time;
    uint64_t modify_time;
    uint64_t access_time;
    uint32_t length;
    int32_t start_block;
    int32_t flags;
    int32_t unused;
};

// Allocate space for a new child directory of specified parent address
// Returns address of allocated directory on success; otherwise, returns FFS_DIR_ADDRESS_INVALID
ffs_address ffs_dir_alloc(ffs_disk disk, ffs_address parent_address);

// Free space in parent directory by removing specified directory address
// Directory must be a child of parent directory
// Directory data will not be freed, use ffs_block_free on its start block
// Returns zero on success; otherwise, returns non-zero
int ffs_dir_free(ffs_disk disk, ffs_address parent_address, ffs_address address);

// Get next sibling address, not guaranteed to be allocated
// Returns address of allocated directory on success; otherwise, returns FFS_DIR_ADDRESS_INVALID
ffs_address ffs_dir_next(ffs_disk disk, ffs_address sibling_address);

// Get address for directory pointed to by path, starting at specified start address
// Path must be relative to specified start address
// Returns address of allocated directory on success; otherwise, returns FFS_DIR_ADDRESS_INVALID
ffs_address ffs_dir_path(ffs_disk disk, ffs_address start_address, const char *path);

// Read directory information from specified address
// Returns zero on success; otherwise, returns non-zero
int ffs_dir_read(ffs_disk disk, ffs_address address, struct ffs_directory *directory);

// Write directory information to specified address
// Returns zero on success; otherwise, returns non-zero
int ffs_dir_write(ffs_disk disk, ffs_address address, const struct ffs_directory *directory);

#endif

#ifndef FFS_DIR_H
#define FFS_DIR_H

#include "ffs_disk.h"

#define FFS_DIR_NAME_LENGTH		 23

// Directory flags
#define FFS_DIR_DIRECTORY		 1
#define FFS_DIR_FILE			 2

#define FFS_DIR_OFFSET_INVALID	-1

#define FFS_DIR_ADDRESS_VALID(address) (address.block != FFS_BLOCK_INVALID && address.offset != FFS_DIR_OFFSET_INVALID)

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

// Allocate size bytes in the directory associated with parent address
// Returns allocated address on success; otherwise, returns FFS_DIR_ADDRESS_INVALID
ffs_address ffs_dir_alloc(ffs_disk disk, ffs_address parent_address, uint32_t size);

// Free size bytes at offset in the directory associated with parent address
// Offset must be a child address of specified directory
// Returns zero on success; otherwise, returns non-zero
int ffs_dir_free(ffs_disk disk, ffs_address parent_address, ffs_address offset_address, uint32_t size);

// Get address for the directory pointed to by path, starting at specified root directory
// Path must be relative to specified root address
// Returns address of allocated directory on success; otherwise, returns FFS_DIR_ADDRESS_INVALID
ffs_address ffs_dir_path(ffs_disk disk, ffs_address root_address, const char *path);

// Read size bytes of data from specified address
// The space to be read must already be allocated
// Returns zero on success; otherwise, returns non-zero
int ffs_dir_read(ffs_disk disk, ffs_address address, void *data, uint32_t size);

// Get root address
// Returns root address on success; otherwise, returns FFS_DIR_ADDRESS_INVALID
ffs_address ffs_dir_root(ffs_disk disk);

// Get the offset address of a directory associated with parent address
// The specified offset must be less than or equal to the directory length
// The address block is guaranteed to be allocated on success
// The space in the block is not guaranteed to be allocated
// Returns address at offset on success; otherwise, returns FFS_DIR_ADDRESS_INVALID
ffs_address ffs_dir_seek(ffs_disk disk, ffs_address parent_address, uint32_t offset);

// Get offset from offset address of a directory associated with parent address
// Offset must be a child address of specified directory
// Returns offset on success; otherwise FFS_DIR_OFFSET_INVALID
uint32_t ffs_dir_seek(ffs_disk disk, ffs_address parent_address, ffs_address offset);

// Write size bytes of data to specified address
// The space to be written must already be allocated
// Returns zero on success; otherwise, returns non-zero
int ffs_dir_write(ffs_disk disk, ffs_address address, const void *data, uint32_t size);

#endif

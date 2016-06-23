#include <ffs/ffs_block.h>
#include <ffs/ffs_dir.h>

// Get block and directory index the address points to
// If address is FFS_DIR_ADDRESS_INVALID then block and directory are set to FFS_BLOCK_INVALID and -1 respectively
static void ffs_dir_address_get(ffs_disk disk, size_t address, int *block, int *directory_index);

// Get address to point to specified directory index in specified block
// Returns address on success; otherwise, returns FFS_DIR_ADDRESS_INVALID
static size_t ffs_dir_address_make(ffs_disk disk, int block, int directory_index);

size_t ffs_dir_alloc(ffs_disk disk, size_t parent_address)
{
	int block;
	int directory_index;
	ffs_dir_address_get(parent_address, &block, &directory_index);

	// Parent address is not a valid address
	if(block == FFS_BLOCK_INVALID || directory_index == -1) {
		return FFS_DIR_ADDRESS_INVALID;
	}
}

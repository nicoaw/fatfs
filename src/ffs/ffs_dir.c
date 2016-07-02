#include <ffs/ffs_block.h>
#include <ffs/ffs_dir.h>

// Check if address is valid
// Returns zero on success; otherwise, returns non-zero
static int ffs_dir_address_valid(ffs_disk disk, ffs_address address);

static int ffs_dir_address_valid(ffs_disk disk, ffs_address address)
{
    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
        return FFS_DIR_ADDRESS_INVALID;
    }

    const size_t block_directory_count = superblock->block_size / sizeof(struct ffs_directory);

    return address.block == FFS_DIR_ADDRESS_INVALID.block || address.directory_index >= 0 || address.directory_index < block_directory_count ? 0 : -1;
}

ffs_address ffs_dir_alloc(ffs_disk disk, ffs_address parent_address)
{
    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
        return FFS_DIR_ADDRESS_INVALID;
    }

	if(ffs_dir_address_valid(parent_address) != 0) {
        return FFS_DIR_ADDRESS_INVALID;
    }

    // Need parent directory to allocate new child directory
    struct ffs_directory parent_directory;
    if(ffs_dir_read(disk, parent_address, &parent_directory) != 0) {
        return FFS_DIR_ADDRESS_INVALID;
    }

    // Need to allocate start block if there is none
    if(parent_directory.start_block == FFS_BLOCK_LAST) {
		parent_directory.start_block = ffs_block_alloc(disk, FFS_BLOCK_LAST);
    }

	ffs_address address = {parent_directory.start_block, 0};
    for(size_t length_scanned = 0; length_scanned < parent_directory.length; length_scanned += sizeof(struct ffs_directory)) {
		ffs_address next_address = ffs_dir_next(disk, address);

		// Need to allocate another block since next block is last
		if(next_address.block == FFS_BLOCK_LAST) {
			next_address.block = ffs_block_alloc(disk, address.block);
			next_address.directory_index = 0;
		}

		address = next_address;
    }

	if(ffs_dir_address_valid(address) != 0) {
        return FFS_DIR_ADDRESS_INVALID;
    }

	// Need to update parent directory length (and possibly start block)
	parent_directory.length += sizeof(struct ffs_directory);
    if(ffs_dir_write(disk, parent_address, &parent_directory) != 0) {
        return FFS_DIR_ADDRESS_INVALID;
    }

	return address;
}

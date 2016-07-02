#include <ffs/ffs_block.h>
#include <ffs/ffs_dir.h>

// Check if address is valid
// Returns zero on success; otherwise, returns non-zero
static int ffs_dir_address_valid(ffs_disk disk, ffs_address address);

static int ffs_dir_address_valid(ffs_disk disk, ffs_address address)
{
    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
        return -1;
    }

    const size_t block_directory_count = superblock->block_size / sizeof(struct ffs_directory);

    return address.block == FFS_BLOCK_LAST || address.block == FFS_BLOCK_INVALID || address.directory_index >= 0 || address.directory_index < block_directory_count ? 0 : -1;
}

ffs_address ffs_dir_alloc(ffs_disk disk, ffs_address parent_address)
{
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

int ffs_dir_free(ffs_disk disk, ffs_address parent_address, ffs_address address)
{
	if(ffs_dir_address_valid(parent_address) != 0 || ffs_dir_address_valid(address) != 0) {
		return -1;
	}

    // Need parent directory to allocate new child directory
    struct ffs_directory parent_directory;
    if(ffs_dir_read(disk, parent_address, &parent_directory) != 0) {
        return -1;
    }

    // Need to allocate start block if there is none
    if(parent_directory.start_block == FFS_BLOCK_LAST) {
		return -1;
    }

	// Find last address and check if specified address is in directory
	const size_t scan_length = parent_directory.length - sizeof(struct ffs_directory);
	int found_address = 0;
	ffs_address last_address = {parent_directory.start_block, 0};
    for(size_t length_scanned = 0; length_scanned < scan_length; length_scanned += sizeof(struct ffs_directory)) {
		if(last_address.block == address.block && last_address.directory_index == address.directory_index) {
			found_address = 1;
		}

		last_address = ffs_dir_next(disk, last_address);
    }

	if(!found_address || ffs_dir_address_valid(last_address) != 0) {
		return -1;
	}

	// Need to perserve last directory information
	struct ffs_directory last_directory;
    if(ffs_dir_read(disk, last_address, &last_directory) != 0) {
        return -1;
    }

    if(ffs_dir_write(disk, address, &last_directory) != 0) {
        return -1;
    }

	// Free last block since last address was first in block
	if(last_address.directory_index == 0) {
		if(ffs_block_free(disk, last_address.block) != 0) {
			return -1;
		}
	}

	parent_directory.length -= sizeof(struct ffs_directory);

	if(parent_directory.length == 0) {
		parent_directory.start_block = FFS_BLOCK_LAST;
	}

	// Need to update parent directory length (and possibly start block)
    if(ffs_dir_write(disk, parent_address, &parent_directory) != 0) {
        return FFS_DIR_ADDRESS_INVALID;
    }

	return 0;
}

ffs_address ffs_dir_next(ffs_disk disk, ffs_address sibling_address)
{
	if(ffs_dir_address_valid(sibling_address) != 0) {
		return FFS_DIR_ADDRESS_INVALID;
	}

	++sibling_address.directory_index;

	// If new directory index is invalid then next directory is in the next block
	if(ffs_dir_address_valid(sibling_address) != 0) {
		sibling_address.block = ffs_block_next(disk, sibling_address.block);
		sibling_address.directory_index = 0;
	}

	return sibling_address;
}

int ffs_dir_read(ffs_disk disk, ffs_address address, struct ffs_directory *directory)
{
	if(ffs_dir_address_valid(address) != 0) {
		return -1;
	}

    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
        return -1;
    }

	// Read block directory is in
	struct ffs_directory *directory_buffer = malloc(superblock->block_size);
	if(ffs_block_read(disk, address.block, directory_buffer) != 0) {
		free(directory_buffer):
		return -1;
	}

	*directory = *(directory_buffer + address.directory_index);

	free(directory_buffer);
	return 0;
}

int ffs_dir_write(ffs_disk disk, ffs_address address, const struct ffs_directory *directory)
{
	if(ffs_dir_address_valid(address) != 0) {
		return -1;
	}

    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
        return -1;
    }

	// Read block directory is in
	struct ffs_directory *directory_buffer = malloc(superblock->block_size);
	if(ffs_block_read(disk, address.block, directory_buffer) != 0) {
		free(directory_buffer):
		return -1;
	}
	
	*(directory_buffer + address.directory_index) = *directory;

	// Write updated block
	if(ffs_block_write(disk, address.block, directory_buffer) != 0) {
		free(directory_buffer):
		return -1;
	}

	free(directory_buffer):
	return 0;
}

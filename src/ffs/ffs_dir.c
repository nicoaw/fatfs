#include <ffs/ffs_block.h>
#include <ffs/ffs_debug.h>
#include <ffs/ffs_dir.h>
#include <stdlib.h>
#include <string.h>

const ffs_address FFS_DIR_ADDRESS_INVALID = {FFS_BLOCK_INVALID, -1};

// Recursively find path address in parent directory starting with name
// Returns address of path on success; otherwise, returns FFS_DIR_ADDRESS_INVALID
static ffs_address ffs_dir_path_impl(ffs_disk disk, ffs_address parent_address, const char *name);

int ffs_dir_address_valid(ffs_disk disk, ffs_address address)
{
	FFS_LOG(1, "disk=%p address={block=%d directory_index=%d}", disk, address.block, address.directory_index);

	const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
	if(!superblock) {
		FFS_ERR(1, "superblock retrieval failed");
		return -1;
	}

	const uint32_t block_directory_count = superblock->block_size / sizeof(struct ffs_directory);

	return address.block == FFS_BLOCK_LAST || address.block == FFS_BLOCK_INVALID || address.directory_index >= 0 || address.directory_index < block_directory_count ? 0 : -1;
}

ffs_address ffs_dir_alloc(ffs_disk disk, ffs_address parent_address)
{
	FFS_LOG(1, "disk=%p parent_address={block=%d directory_index=%d}", disk, parent_address.block, parent_address.directory_index);

	if(ffs_dir_address_valid(disk, parent_address) != 0) {
		FFS_ERR(1, "parent address invalid");
		return FFS_DIR_ADDRESS_INVALID;
	}

	// Need parent directory to allocate new child directory
	struct ffs_directory parent_directory;
	if(ffs_dir_read(disk, parent_address, &parent_directory) != 0) {
		FFS_ERR(1, "parent directory read failed");
		return FFS_DIR_ADDRESS_INVALID;
	}

	// Need to allocate start block if there is none
	if(parent_directory.start_block == FFS_BLOCK_LAST) {
		parent_directory.start_block = ffs_block_alloc(disk, FFS_BLOCK_LAST);
	}

	ffs_address address = {parent_directory.start_block, 0};
	for(uint32_t length_scanned = 0; length_scanned < parent_directory.length; length_scanned += sizeof(struct ffs_directory)) {
		ffs_address next_address = ffs_dir_next(disk, address);

		// Need to allocate another block since next block is last
		if(next_address.block == FFS_BLOCK_LAST) {
			next_address.block = ffs_block_alloc(disk, address.block);
			next_address.directory_index = 0;
		}

		address = next_address;
	}

	if(ffs_dir_address_valid(disk, address) != 0) {
		FFS_ERR(1, "allocated address invalid");
		return FFS_DIR_ADDRESS_INVALID;
	}

	// Need to update parent directory length (and possibly start block)
	parent_directory.length += sizeof(struct ffs_directory);
	if(ffs_dir_write(disk, parent_address, &parent_directory) != 0) {
		FFS_ERR(1, "parent directory write failed");
		return FFS_DIR_ADDRESS_INVALID;
	}

	return address;
}

int ffs_dir_free(ffs_disk disk, ffs_address parent_address, ffs_address address)
{
	FFS_LOG(1, "disk=%p parent_address={block=%d directory_index=%d} address={block=%d directory_index=%d}", disk, parent_address.block, parent_address.directory_index, address.block, address.directory_index);

	if(ffs_dir_address_valid(disk, parent_address) != 0 || ffs_dir_address_valid(disk, address) != 0) {
		FFS_ERR(1, "specified addresses invalid");
		return -1;
	}

	// Need parent directory to allocate new child directory
	struct ffs_directory parent_directory;
	if(ffs_dir_read(disk, parent_address, &parent_directory) != 0) {
		FFS_ERR(1, "specified addresses invalid");
		return -1;
	}

	// Find last address and check if specified address is in directory
	const uint32_t scan_length = parent_directory.length - sizeof(struct ffs_directory);
	int found_address = 0;
	uint32_t last_block_parent = FFS_BLOCK_INVALID;
	ffs_address last_address = {parent_directory.start_block, 0};
	for(uint32_t length_scanned = 0; length_scanned < scan_length; length_scanned += sizeof(struct ffs_directory)) {
		if(last_address.block == address.block && last_address.directory_index == address.directory_index) {
			found_address = 1;
		}

		last_block_parent = last_address.block;
		last_address = ffs_dir_next(disk, last_address);
	}

	if(!found_address || ffs_dir_address_valid(disk, last_address) != 0) {
		FFS_ERR(1, "address not found or last address invalid");
		return -1;
	}

	// Need to perserve last directory information
	struct ffs_directory last_directory;
	if(ffs_dir_read(disk, last_address, &last_directory) != 0) {
		FFS_ERR(1, "last directory read failed");
		return -1;
	}

	if(ffs_dir_write(disk, address, &last_directory) != 0) {
		FFS_ERR(1, "swapped last directory write failed");
		return -1;
	}

	// Free last block since last address was first in block
	if(last_address.directory_index == 0) {
		if(ffs_block_free(disk, last_block_parent, last_address.block) != 0) {
			FFS_ERR(1, "last block free failed");
			return -1;
		}
	}

	parent_directory.length -= sizeof(struct ffs_directory);

	if(parent_directory.length == 0) {
		parent_directory.start_block = FFS_BLOCK_LAST;
	}

	// Need to update parent directory length (and possibly start block)
	if(ffs_dir_write(disk, parent_address, &parent_directory) != 0) {
		FFS_ERR(1, "parent directory write failed");
		return -1;
	}

	return 0;
}

ffs_address ffs_dir_next(ffs_disk disk, ffs_address sibling_address)
{
	FFS_LOG(1, "disk=%p sibling_address={block=%d directory_index=%d}", disk, sibling_address.block, sibling_address.directory_index);

	if(ffs_dir_address_valid(disk, sibling_address) != 0) {
		FFS_ERR(1, "specified sibling address invalid");
		return FFS_DIR_ADDRESS_INVALID;
	}

	++sibling_address.directory_index;

	// If new directory index is invalid then next directory is in the next block
	if(ffs_dir_address_valid(disk, sibling_address) != 0) {
		sibling_address.block = ffs_block_next(disk, sibling_address.block);
		sibling_address.directory_index = 0;
	}

	return sibling_address;
}

ffs_address ffs_dir_path(ffs_disk disk, ffs_address start_address, const char *path)
{
	FFS_LOG(1, "disk=%p start_address={block=%d directory_index=%d} path=%s", disk, start_address.block, start_address.directory_index, path);

	// Need mutable path for strtok
	char *mutable_path = calloc(strlen(path) + 1, sizeof(char));
	strcpy(mutable_path, path);

	const char *name = strtok(mutable_path, "/");
	ffs_address result = ffs_dir_path_impl(disk, start_address, name);

	free(mutable_path);
	return result;
}

static ffs_address ffs_dir_path_impl(ffs_disk disk, ffs_address parent_address, const char *name)
{
	FFS_LOG(1, "disk=%p parent_address={block=%d directory_index=%d} name=%s", disk, parent_address.block, parent_address.directory_index, name);

	if(ffs_dir_address_valid(disk, parent_address) != 0) {
		FFS_ERR(1, "parent address invalid");
		return FFS_DIR_ADDRESS_INVALID;
	}

	// Parent address is pointed to by path (ie. done searching)
	if(!name) {
		return parent_address;
	}

	// Need parent directory to find directory with specified name
	struct ffs_directory parent_directory;
	if(ffs_dir_read(disk, parent_address, &parent_directory) != 0) {
		FFS_ERR(1, "parent directory read failed");
		return FFS_DIR_ADDRESS_INVALID;
	}

	ffs_address address = {parent_directory.start_block, 0};
	for(uint32_t length_scanned = 0; length_scanned < parent_directory.length; length_scanned += sizeof(struct ffs_directory)) {
		struct ffs_directory directory;
		if(ffs_dir_read(disk, address, &directory) != 0) {
			FFS_ERR(1, "directory read failed");
			return FFS_DIR_ADDRESS_INVALID;
		}

		// Found directory with specified name
		if(strcmp(directory.name, name) == 0) {
			const char *child_name = strtok(NULL, "/");
			return ffs_dir_path_impl(disk, address, child_name);
		}

		address = ffs_dir_next(disk, address);
	}

	FFS_ERR(1, "No address found");
	return FFS_DIR_ADDRESS_INVALID;
}

int ffs_dir_read(ffs_disk disk, ffs_address address, struct ffs_directory *directory)
{
	FFS_LOG(1, "disk=%p address={block=%d directory_index=%d} directory=%p", disk, address.block, address.directory_index, directory);

	if(ffs_dir_address_valid(disk, address) != 0) {
		FFS_ERR(1, "specified address invalid");
		return -1;
	}

	const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
	if(!superblock) {
		FFS_ERR(1, "superblock retrieval failed");
		return -1;
	}

	// Read block directory is in
	struct ffs_directory *directory_buffer = malloc(superblock->block_size);
	if(ffs_block_read(disk, address.block, directory_buffer) != 0) {
		free(directory_buffer);
		FFS_ERR(1, "block read failed");
		return -1;
	}

	*directory = *(directory_buffer + address.directory_index);

	FFS_LOG(1, "read directory:\n"
			"\tname=%s\n"
			"\tcreate_time=%lld\n"
			"\tmodify_time=%lld\n"
			"\taccess_time=%lld\n"
			"\tlength=%d\n"
			"\tstart_block=%d\n"
			"\tflags=%d\n"
			"\tunused=%d\n",
			directory->name,
			directory->create_time,
			directory->modify_time,
			directory->access_time,
			directory->length,
			directory->start_block,
			directory->flags,
			directory->unused
		   );

	free(directory_buffer);
	return 0;
}

ffs_address ffs_dir_root(ffs_disk disk)
{
	FFS_LOG(1, "disk=%p", disk);

	const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
	if(!superblock) {
		FFS_ERR(1, "superblock retrieval failed");
		return FFS_DIR_ADDRESS_INVALID;
	}
	
	ffs_address address = {superblock->root_block, 0};
	return address;
}

int ffs_dir_write(ffs_disk disk, ffs_address address, const struct ffs_directory *directory)
{
	FFS_LOG(1, "disk=%p address={block=%d directory_index=%d} directory=%p", disk, address.block, address.directory_index, directory);

	if(ffs_dir_address_valid(disk, address) != 0) {
		FFS_ERR(1, "specified address invalid");
		return -1;
	}

	const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
	if(!superblock) {
		FFS_ERR(1, "superblock retrieval failed");
		return -1;
	}

	// Read block directory is in
	struct ffs_directory *directory_buffer = malloc(superblock->block_size);
	if(ffs_block_read(disk, address.block, directory_buffer) != 0) {
		free(directory_buffer);
		FFS_ERR(1, "block read failed");
		return -1;
	}

	*(directory_buffer + address.directory_index) = *directory;

	FFS_LOG(1, "write directory:\n"
			"\tname=%s\n"
			"\tcreate_time=%lld\n"
			"\tmodify_time=%lld\n"
			"\taccess_time=%lld\n"
			"\tlength=%d\n"
			"\tstart_block=%d\n"
			"\tflags=%d\n"
			"\tunused=%d\n",
			directory->name,
			directory->create_time,
			directory->modify_time,
			directory->access_time,
			directory->length,
			directory->start_block,
			directory->flags,
			directory->unused
		   );


	// Write updated block
	if(ffs_block_write(disk, address.block, directory_buffer) != 0) {
		free(directory_buffer);
		FFS_ERR(1, "block write failed");
		return -1;
	}

	free(directory_buffer);
	return 0;
}

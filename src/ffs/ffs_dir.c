#include <ffs/ffs_aux.h>
#include <ffs/ffs_block.h>
#include <ffs/ffs_dir.h>
#include <stdlib.h>
#include <string.h>

extern const ffs_address FFS_DIR_ADDRESS_INVALID = {FFS_BLOCK_INVALID, 0};

// Recursively find address in parent directory starting with name
// Returns invalid address on failure
ffs_address ffs_dir_find_impl(ffs_disk disk, const struct ffs_directory *parent, const char *name);

ffs_address ffs_dir_alloc(ffs_disk disk, ffs_address entry, uint32_t size)
{
	FFS_LOG(1, "disk=%p entry={block=%u offset=%u} size=%u", disk, entry.block, entry.offset, size);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	// Read directory to allocate space for
	struct ffs_directory directory;
	if(ffs_dir_read(disk, entry, &directory, sizeof(struct ffs_directory)) != 0) {
		FFS_ERR(1, "directory read failed");
		return -1;
	}

	const uint32_t unallocated = sb->block_size - directory.length % sb->block_size;
	uint32_t allocated = 0;

	// Psuedo-allocate rest of head block
	if(unallocated != sb->block_size) {
		allocated += unallocated;
	}

	// Allocate appropriate amount of blocks
	while(allocated < size) {
		directory.start_block = ffs_block_alloc(disk, directory.start_block);
		if(!FFS_BLOCK_VALID(directory.start_block)) {
			// TODO: already allocated blocks are lost because the directory is not updated
			FFS_ERR(1, "block allocation failure");
			return -1;
		}

		allocated += sb->block_size;
	}

	// Write updated directory information
	directory.length += size;
	if(ffs_dir_write(disk, entry, &directory, sizeof(struct ffs_directory)) != 0) {
		FFS_ERR(1, "directory write failed");
		return -1;
	}

	return address;
}

int ffs_dir_free(ffs_disk disk, ffs_address entry, uint32_t size)
{
	FFS_LOG(1, "disk=%p entry={block=%u offset=%u} size=%u", disk, entry.block, entry.offset, size);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	// Read directory to free space from
	struct ffs_directory directory;
	if(ffs_dir_read(disk, entry, &directory, sizeof(struct ffs_directory)) != 0) {
		FFS_ERR(1, "directory read failed");
		return -1;
	}

	// Cannot free more space then the directory currently has allocated
	if(size > directory.length) {
		FFS_ERR(1, "invalid free range");
		return -1;
	}

	// Psuedo-free allocated space in head block
	const uint32_t allocated = directory.length % sb->block_size;
	uint32_t freed = allocated;

	while(freed <= size) {
		// Get next block before freeing the head
		block next = ffs_block_next(disk, directory.start_block);
		if(next != FFS_BLOCK_LAST && !FFS_BLOCK_VALID(next)) {
			FFS_ERR(1, "failed to get next block");
			return -1;
		}

		if(ffs_block_free(disk, directory.start_block) != 0) {
			FFS_ERR(1, "failed to free block");
			return -1;
		}

		directory.start_block = next;
		freed += sb->block_size;
	}
	
	// Write updated directory information
	directory.length -= size;
	if(ffs_dir_write(disk, entry, &directory, sizeof(struct ffs_directory)) != 0) {
		FFS_ERR(1, "directory write failed");
		return -1;
	}

	return 0;
}

ffs_address ffs_dir_path(ffs_disk disk, ffs_address root_address, const char *path)
{
	FFS_LOG(1, "disk=%p root_address={block=%u offset=%u} path=%s", disk, root_address.block, root_address.offset, path);

	// Need mutable path for strtok
	char *mutable_path = calloc(strlen(path) + 1, sizeof(char));
	strcpy(mutable_path, path);

	const char *name = strtok(mutable_path, "/");
	ffs_address result = ffs_dir_path_impl(disk, root_address, name);

	free(mutable_path);
	return result;
}

ffs_address ffs_dir_path_impl(ffs_disk disk, ffs_address parent_address, const char *name)
{
	FFS_LOG(1, "disk=%p parent_address={block=%u offset=%u} name=%s", disk, parent_address.block, parent_address.offset, name);

	const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
	if(!superblock) {
		FFS_ERR(1, "superblock retrieval failed");
		return FFS_DIR_ADDRESS_INVALID;
	}

	if(!FFS_DIR_ADDRESS_VALID(parent_address, superblock->block_size)) {
		FFS_ERR(1, "parent address invalid");
		return FFS_DIR_ADDRESS_INVALID;
	}

	// Parent address is pointed to by path (ie. done searching)
	if(!name) {
		return parent_address;
	}

	// Need parent directory to find directory with specified name
	struct ffs_directory parent_directory;
	if(ffs_dir_read(disk, parent_address, &parent_directory, sizeof(struct ffs_directory)) != 0) {
		FFS_ERR(1, "parent directory read failed");
		return FFS_DIR_ADDRESS_INVALID;
	}

	ffs_address address = {parent_directory.start_block, 0};
	for(uint32_t length_read = 0; length_read < parent_directory.length; length_read += sizeof(struct ffs_directory)) {
		//TODO: somehow address is FFS_BLOCK_LAST with parenet_directory.length of 64 for .swp files
		//		maybe they aren't allocated correctly
		//		maybe other files aren't being freed correctly
		struct ffs_directory directory;
		if(ffs_dir_read(disk, address, &directory, sizeof(struct ffs_directory)) != 0) {
			FFS_ERR(1, "directory read failed");
			return FFS_DIR_ADDRESS_INVALID;
		}

		// Found directory with specified name
		if(strcmp(directory.name, name) == 0) {
			const char *child_name = strtok(NULL, "/");
			return ffs_dir_path_impl(disk, address, child_name);
		}

		address = ffs_dir_seek(disk, address, sizeof(struct ffs_directory));
		if(!FFS_DIR_ADDRESS_VALID(address, superblock->block_size)) {
			FFS_ERR(1, "failed to seek to next directory");
			return FFS_DIR_ADDRESS_INVALID;
		}
	}

	FFS_ERR(1, "No address found");
	return FFS_DIR_ADDRESS_INVALID;
}

int ffs_dir_read(ffs_disk disk, ffs_address address, void *data, uint32_t size)
{
	FFS_LOG(1, "disk=%p address={block=%u offset=%u} data=%p size=%u", disk, address.block, address.offset, data, size);

	const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
	if(!superblock) {
		FFS_ERR(1, "superblock retrieval failed");
		return -1;
	}

	if(!FFS_DIR_ADDRESS_VALID(address, superblock->block_size)) {
		FFS_ERR(1, "specified address invalid");
		return -1;
	}

	// Optimization for reading zero bytes
	if(size == 0) {
		return 0;
	}

	uint32_t length_read = 0;
	void *buffer = malloc(superblock->block_size);

	// Read data chunk by chunk
	while(length_read < size) {
		const uint32_t chunk_length = min_ui32(superblock->block_size - address.offset, size - length_read);

		// Read block
		if(ffs_block_read(disk, address.block, buffer) != 0) {
			free(buffer);
			FFS_ERR(1, "block read failed");
			return -1;
		}

		// Copy appropriate data
		memcpy((char *) data + length_read, (char *) buffer + address.offset, chunk_length);
		length_read += chunk_length;

		// Seek next address to read from
		address = ffs_dir_seek(disk, address, chunk_length);
		if(!FFS_DIR_ADDRESS_VALID(address, FFS_DIR_OFFSET_INVALID)) {
			free(buffer);
			FFS_ERR(1, "failed to seek to next chunk");
			return -1;
		}
	}

	free(buffer);
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

ffs_address ffs_dir_seek(ffs_disk disk, ffs_address start_address, uint32_t offset)
{
	FFS_LOG(1, "disk=%p start_address={block=%u offset=%u} offset=%u", disk, start_address.block, start_address.offset, offset);

	const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
	if(!superblock) {
		FFS_ERR(1, "superblock retrieval failed");
		return FFS_DIR_ADDRESS_INVALID;
	}

	if(!FFS_DIR_ADDRESS_VALID(start_address, superblock->block_size)) {
		FFS_ERR(1, "specified start address invalid");
		return FFS_DIR_ADDRESS_INVALID;
	}

	// Optimization for seeking offset zero
	if(offset == 0) {
		return start_address;
	}

	// Seek chunk by chunk
	uint32_t length_scanned = 0;
	while(length_scanned < offset) {
		const uint32_t remain_block = superblock->block_size - start_address.offset;
		const uint32_t remain_offset = offset - length_scanned;

		if(remain_block < remain_offset) {
			// Seek to next block
			start_address.offset = 0;
			start_address.block = ffs_block_next(disk, start_address.block);
			if(start_address.block == FFS_BLOCK_LAST) {
				FFS_ERR(1, "failed to get next block");
				return FFS_DIR_ADDRESS_INVALID;
			}

			length_scanned += remain_block;
		} else {
			// Seek to offset
			start_address.offset = remain_offset;
			length_scanned += remain_offset;
		}
	}

	if(length_scanned != offset) {
		FFS_ERR(1, "failed to seek to correct offset");
		return FFS_DIR_ADDRESS_INVALID;
	}

	return start_address;
}

uint32_t ffs_dir_tell(ffs_disk disk, ffs_address start_address, ffs_address offset_address)
{
	FFS_LOG(1, "disk=%p start_address={block=%u offset=%u} offset_address={block=%u offset=%u}", disk, start_address.block, start_address.offset, offset_address.block, offset_address.offset);

	const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
	if(!superblock) {
		FFS_ERR(1, "superblock retrieval failed");
		return -1;
	}


	if(!FFS_DIR_ADDRESS_VALID(start_address, superblock->block_size)) {
		FFS_ERR(1, "specified start address invalid");
		return FFS_DIR_OFFSET_INVALID;
	}

	if(!FFS_DIR_ADDRESS_VALID(offset_address, superblock->block_size)) {
		FFS_ERR(1, "specified offset address invalid");
		return FFS_DIR_OFFSET_INVALID;
	}

	uint32_t offset = 0;
	while(start_address.block != offset_address.block) {
		offset += superblock->block_size - start_address.offset;

		start_address.block = ffs_block_next(disk, start_address.block);
		start_address.offset = 0;
		if(start_address.block == FFS_BLOCK_LAST) {
			FFS_ERR(1, "failed to get next block");
			return FFS_DIR_OFFSET_INVALID;
		}
	}

	return offset + offset_address.offset;
}

int ffs_dir_write(ffs_disk disk, ffs_address address, const void *data, uint32_t size)
{
	FFS_LOG(1, "disk=%p address={block=%u offset=%u} data=%p size=%u", disk, address.block, address.offset, data, size);

	const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
	if(!superblock) {
		FFS_ERR(1, "superblock retrieval failed");
		return -1;
	}

	if(!FFS_DIR_ADDRESS_VALID(address, superblock->block_size)) {
		FFS_ERR(1, "specified address invalid");
		return -1;
	}

	// Optimization for writing zero bytes
	if(size == 0) {
		return 0;
	}

	uint32_t length_written = 0;
	void *buffer = malloc(superblock->block_size);

	// Write data chunk by chunk
	while(length_written < size) {
		const uint32_t chunk_length = min_ui32(superblock->block_size - address.offset, size - length_written);

		// Read block to copy existing data
		// Optimization: Don't need to do this if we are reading the entire block
		if(chunk_length < superblock->block_size) {
			if(ffs_block_read(disk, address.block, buffer) != 0) {
				free(buffer);
				FFS_ERR(1, "block read failed");
				return -1;
			}
		}

		// Copy appropriate data
		memcpy((char *) buffer + address.offset, (char *) data + length_written, chunk_length);
		length_written += chunk_length;

		// Write block
		if(ffs_block_write(disk, address.block, buffer) != 0) {
			free(buffer);
			FFS_ERR(1, "block write failed");
			return -1;
		}

		// Seek next address to write from
		address = ffs_dir_seek(disk, address, chunk_length);
		if(!FFS_DIR_ADDRESS_VALID(address, FFS_DIR_OFFSET_INVALID)) {
			free(buffer);
			FFS_ERR(1, "failed to seek to next chunk");
			return -1;
		}
	}

	free(buffer);
	return 0;
}

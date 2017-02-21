#include <ffs/ffs_aux.h>
#include <ffs/ffs_block.h>
#include <ffs/ffs_dir.h>
#include <stdlib.h>
#include <string.h>

extern const ffs_address FFS_DIR_ADDRESS_INVALID = {FFS_BLOCK_INVALID, 0};

// Recursively find address in parent directory starting with name
// Returns invalid address on failure
ffs_address ffs_dir_find_impl(ffs_disk disk, ffs_address parent, const char *name);

// Seek to offset in directory
// Returns invalid address on failure
ffs_address ffs_dir_seek(ffs_disk disk, ffs_address entry, uint32_t offset);

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

	const uint32_t unallocated = sb->block_size - directory.size % sb->block_size;
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
	directory.size += size;
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
	if(size > directory.size) {
		FFS_ERR(1, "invalid free range");
		return -1;
	}

	// Psuedo-free allocated space in head block
	const uint32_t allocated = directory.size % sb->block_size;
	uint32_t freed = allocated;

	while(freed <= size) {
		// Get next block before freeing the head
		ffs_block next = ffs_block_next(disk, directory.start_block);
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
	directory.size -= size;
	if(ffs_dir_write(disk, entry, &directory, sizeof(struct ffs_directory)) != 0) {
		FFS_ERR(1, "directory write failed");
		return -1;
	}

	return 0;
}

ffs_address ffs_dir_find(ffs_disk disk, ffs_address root, const char *path)
{
	FFS_LOG(1, "disk=%p root={block=%u offset=%u} path=%s", disk, root.block, root.offset, path);

	// Need mutable path for strtok
	char *mutable_path = malloc((strlen(path) + 1) * sizeof(char));
	strcpy(mutable_path, path);

	const char *name = strtok(mutable_path, "/");

	ffs_address result = ffs_dir_find_impl(disk, root, name);

	free(mutable_path);
	return result;
}

ffs_address ffs_dir_find_impl(ffs_disk disk, ffs_address parent, const char *name)
{
	FFS_LOG(1, "disk=%p parent=%p name=%s", disk, parent, name);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	// Parent address is pointed to by path (ie. done searching)
	if(!name) {
		return parent;
	}

	// Need parent directory to find directory with specified name
	struct ffs_directory directory;
	if(ffs_dir_read(disk, parent, &directory, sizeof(struct ffs_directory)) != 0) {
		FFS_ERR(1, "parent directory read failed");
		return FFS_DIR_ADDRESS_INVALID;
	}

	uint32_t alloctated = directory.length % sb->block_size; // allocated space of start block
	for(ffs_block b = directory.start_block; b != FFS_BLOCK_LAST; b = ffs_block_next(disk, b)) {
		if(!FFS_BLOCK_VALID(b)) {
			FFS_ERR(1, "failed to get next block");
			return FFS_DIR_ADDRESS_INVALID;
		}

		// Check each child entry in block
		for(uint32_t offset = 0; offset < allocated; offset += sizeof(struct ffs_directory)) {
			ffs_address address = {b, offset};

			struct ffs_directory child;
			if(ffs_dir_read(disk, address, &child, sizeof(struct ffs_directory)) != 0) {
				FFS_ERR(1, "child directory read failed");
				return FFS_DIR_ADDRESS_INVALID;
			}

			// Found directory with specified name
			if(strcmp(child.name, name) == 0) {
				const char *child_name = strtok(NULL, "/");
				return ffs_dir_find_impl(disk, address, child_name);
			}
		}

		allocated = sb->block_size; // allocated space of next block
	}

	FFS_ERR(1, "No address found");
	return FFS_DIR_ADDRESS_INVALID;
}

uint32_t ffs_dir_read(ffs_disk disk, ffs_address entry, uint32_t offset, const void *data, uint32_t size)
{
	FFS_LOG(1, "disk=%p offset={block=%u offset=%u} data=%p size=%u", disk, offset.block, offset.offset, data, size);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	if(!FFS_DIR_ADDRESS_VALID(sb, entry)) {
		FFS_ERR(1, "entry address invalid");
		return 0;
	}

	// Seek to end of where data is going to be read
	ffs_address address = ffs_dir_seek(disk, entry, offset + size);
	if(!FFS_DIR_ADDRESS_VALID(sb, address)) {
		FFS_ERR(1, "seeked address invalid");
		return 0;
	}

	void *buffer = malloc(sb->block_size);
	uint32_t read = 0;

	// Read data chunk by chunk in reverse
	while(read < size) {
		const uint32_t chunk_size = min_ui32(sb->block_size - address.offset, size - read);
		const uint32_t data_offset = size - (read + chunk_size);
		const uint32_t buffer_offset = sb->block_size - chunk_size;

		// Read block
		if(ffs_block_read(disk, address.block, buffer) != 0) {
			free(buffer);
			FFS_ERR(1, "block read failed");
			return read;
		}

		// Copy appropriate data
		memcpy((uint8_t *) data + data_offset, (uint8_t *) buffer + buffer_offset, chunk_size);
		read += chunk_size;

		address.offset = 0;
		address.block = ffs_block_next(disk, address.block);
		if(!FFS_BLOCK_VALID(address.block)) {
			free(buffer);
			FFS_ERR(1, "failed to get next block");
			return read;
		}
	}

	free(buffer);
	return read;
}

ffs_address ffs_dir_root(ffs_disk disk)
{
	ffs_address address = {ffs_disk_superblock(disk)->root_block, 0};
	return address;
}

ffs_address ffs_dir_seek(ffs_disk disk, ffs_address entry, uint32_t offset)
{
	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	// Read entry block
	uint8_t *buffer = malloc(sb->block_size);
	if(ffs_block_read(disk, entry.block, buffer) != 0) {
		FFS_ERR(1, "failed to read entry");
		return -1;
	}

	struct ffs_directory directory = *(buffer + entry.offset);
	ffs_address address = {directory.start_block, directory.length - offset};
	uint32_t chunk_size = directory.length % sb->block_size;

	free(buffer);

	// Seek chunk by chunk
	while(address.offset >= sb->block_size) {
		address.offset -= chunk_size;
		address.block = ffs_block_next(disk, address.block);
		if(!FFS_BLOCK_VALID(address.block)) {
			FFS_ERR(1, "failed to get next block");
			return FFS_DIR_ADDRESS_INVALID;
		}

		chunk_size = sb->block_size;
	}

	return address;
}

uint32_t ffs_dir_write(ffs_disk disk, ffs_address entry, uint32_t offset, const void *data, uint32_t size)
{
	FFS_LOG(1, "disk=%p offset={block=%u offset=%u} data=%p size=%u", disk, offset.block, offset.offset, data, size);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	if(!FFS_DIR_ADDRESS_VALID(sb, entry)) {
		FFS_ERR(1, "entry address invalid");
		return 0;
	}

	// Seek to end of where data is going to be written
	ffs_address address = ffs_dir_seek(disk, entry, offset + size);
	if(!FFS_DIR_ADDRESS_VALID(sb, address)) {
		FFS_ERR(1, "seeked address invalid");
		return 0;
	}

	void *buffer = malloc(sb->block_size);
	uint32_t written = 0;

	// Write data chunk by chunk in reverse
	while(written < size) {
		const uint32_t chunk_size = min_ui32(sb->block_size - address.offset, size - written);
		const uint32_t data_offset = size - (written + chunk_size);
		const uint32_t buffer_offset = sb->block_size - chunk_size;

		// Read block to copy existing data
		// Optimization: Don't need to do this if we are reading the entire block
		if(chunk_size < sb->block_size) {
			if(ffs_block_read(disk, address.block, buffer) != 0) {
				free(buffer);
				FFS_ERR(1, "block read failed");
				return written;
			}
		}

		// Copy appropriate data
		memcpy((uint8_t *) buffer + buffer_offset, (uint8_t *) data + data_offset, chunk_size);
		written += chunk_size;

		// Write block
		if(ffs_block_write(disk, address.block, buffer) != 0) {
			free(buffer);
			FFS_ERR(1, "block write failed");
			return written;
		}

		address.offset = 0;
		address.block = ffs_block_next(disk, address.block);
		if(!FFS_BLOCK_VALID(address.block)) {
			free(buffer);
			FFS_ERR(1, "failed to get next block");
			return written;
		}
	}

	free(buffer);
	return written;
}

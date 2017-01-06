#include <ffs/ffs_aux.h>
#include <ffs/ffs_block.h>
#include <ffs/ffs_dir.h>
#include <stdlib.h>
#include <string.h>

extern const ffs_address FFS_DIR_ADDRESS_INVALID = {FFS_BLOCK_INVALID, 0};

// Recursively find address in parent directory starting with name
// Returns invalid address on failure
ffs_address ffs_dir_find_impl(ffs_disk disk, ffs_address parent, const char *name);

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

	ffs_address result = ffs_dir_find_impl(disk, &parent, name);

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

int ffs_dir_read(ffs_disk disk, ffs_address offset, void *data, uint32_t size)
{
	FFS_LOG(1, "disk=%p offset={block=%u offset=%u} data=%p size=%u", disk, offset.block, offset.offset, data, size);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	if(!FFS_DIR_ADDRESS_VALID(sb, offset)) {
		FFS_ERR(1, "offset address invalid");
		return -1;
	}

	uint32_t length_read = 0;
	void *buffer = malloc(superblock->block_size);

	// Read data chunk by chunk
	while(length_read < size) {
		const uint32_t chunk_length = min_ui32(sb->block_size - offset.offset, size - length_read);

		// Read block
		if(ffs_block_read(disk, offset.block, buffer) != 0) {
			free(buffer);
			FFS_ERR(1, "block read failed");
			return -1;
		}

		// Copy appropriate data
		memcpy((char *) data + length_read, (char *) buffer + offset.offset, chunk_length);
		length_read += chunk_length;

		// Seek next address to read from
		offset = ffs_dir_seek(disk, offset, chunk_length);
		if(!FFS_DIR_ADDRESS_VALID(offset, FFS_DIR_OFFSET_INVALID)) {
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
	ffs_address address = {ffs_disk_superblock(disk)->root_block, 0};
	return address;
}

ffs_address ffs_dir_seek(ffs_disk disk, ffs_address start, uint32_t offset)
{
	FFS_LOG(1, "disk=%p start={block=%u offset=%u} offset=%u", disk, start.block, start.offset, offset);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	if(!FFS_DIR_ADDRESS_VALID(start, sb->block_size)) {
		FFS_ERR(1, "start address invalid");
		return FFS_DIR_ADDRESS_INVALID;
	}

	// Seek chunk by chunk
	uint32_t length_scanned = 0;
	while(length_scanned < offset) {
		const uint32_t remain_block = sb->block_size - start.offset;
		const uint32_t remain_offset = offset - length_scanned;

		if(remain_block < remain_offset) {
			// Seek to next block
			start.offset = 0;
			start.block = ffs_block_next(disk, start.block);
			if(!FFS_BLOCK_VALID(start.block)) {
				FFS_ERR(1, "failed to get next block");
				return FFS_DIR_ADDRESS_INVALID;
			}

			length_scanned += remain_block;
		} else {
			// Seek to offset
			start.offset = remain_offset;
			length_scanned += remain_offset;
		}
	}

	if(length_scanned != offset) {
		FFS_ERR(1, "failed to seek to correct offset");
		return FFS_DIR_ADDRESS_INVALID;
	}

	return start;
}

int ffs_dir_write(ffs_disk disk, ffs_address offset, const void *data, uint32_t size)
{
	FFS_LOG(1, "disk=%p offset={block=%u offset=%u} data=%p size=%u", disk, offset.block, offset.offset, data, size);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	if(!FFS_DIR_ADDRESS_VALID(sb, offset)) {
		FFS_ERR(1, "offset address invalid");
		return -1;
	}

	uint32_t length_written = 0;
	void *buffer = malloc(sb->block_size);

	// Write data chunk by chunk
	while(length_written < size) {
		const uint32_t chunk_length = min_ui32(sb->block_size - offset.offset, size - length_written);

		// Read block to copy existing data
		// Optimization: Don't need to do this if we are reading the entire block
		if(chunk_length < sb->block_size) {
			if(ffs_block_read(disk, offset.block, buffer) != 0) {
				free(buffer);
				FFS_ERR(1, "block read failed");
				return -1;
			}
		}

		// Copy appropriate data
		memcpy((char *) buffer + offset.offset, (char *) data + length_written, chunk_length);
		length_written += chunk_length;

		// Write block
		if(ffs_block_write(disk, offset.block, buffer) != 0) {
			free(buffer);
			FFS_ERR(1, "block write failed");
			return -1;
		}

		// Seek next address to write from
		offset = ffs_dir_seek(disk, offset, chunk_length);
		if(!FFS_DIR_ADDRESS_VALID(offset, FFS_DIR_OFFSET_INVALID)) {
			free(buffer);
			FFS_ERR(1, "failed to seek to next chunk");
			return -1;
		}
	}

	free(buffer);
	return 0;
}

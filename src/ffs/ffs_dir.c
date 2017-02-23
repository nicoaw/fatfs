#include <ffs/ffs_aux.h>
#include <ffs/ffs_block.h>
#include <ffs/ffs_dir.h>
#include <stdlib.h>
#include <string.h>

const ffs_address FFS_DIR_ADDRESS_INVALID = {FFS_BLOCK_INVALID, 0};

// Recursively find address in parent directory starting with name
// Returns invalid address on failure
ffs_address ffs_dir_find_impl(ffs_disk disk, ffs_address entry, const char *name);

// Seek to offset in directory
// Stops at end of directory
// Returns invalid address on failure
ffs_address ffs_dir_seek(ffs_disk disk, ffs_address entry, uint32_t offset, uint32_t size);

int ffs_dir_alloc(ffs_disk disk, ffs_address entry, uint32_t size)
{
	FFS_LOG(1, "disk=%p entry={block=%u offset=%u} size=%u", disk, entry.block, entry.offset, size);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	// Read directory to allocate space for
	struct ffs_entry directory;
	if(ffs_dir_read(disk, entry, FFS_DIR_ENTRY_OFFSET, &directory, sizeof(struct ffs_entry)) != 0) {
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
	if(ffs_dir_write(disk, entry, FFS_DIR_ENTRY_OFFSET, &directory, sizeof(struct ffs_entry)) != 0) {
		FFS_ERR(1, "directory write failed");
		return -1;
	}

	return 0;
}

int ffs_dir_free(ffs_disk disk, ffs_address entry, uint32_t size)
{
	FFS_LOG(1, "disk=%p entry={block=%u offset=%u} size=%u", disk, entry.block, entry.offset, size);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	// Read directory to free space from
	struct ffs_entry directory;
	if(ffs_dir_read(disk, entry, FFS_DIR_ENTRY_OFFSET, &directory, sizeof(struct ffs_entry)) != 0) {
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
	if(ffs_dir_write(disk, entry, FFS_DIR_ENTRY_OFFSET, &directory, sizeof(struct ffs_entry)) != 0) {
		FFS_ERR(1, "directory write failed");
		return -1;
	}

	return 0;
}

ffs_address ffs_dir_find(ffs_disk disk, const char *path)
{
	FFS_LOG(1, "disk=%p path=%s", disk, path);

	// Need mutable path for strtok
	char *mutable_path = malloc((strlen(path) + 1) * sizeof(char));
	strcpy(mutable_path, path);

	const char *name = strtok(mutable_path, "/");

	ffs_address root = {ffs_disk_superblock(disk)->root_block, 0};
	ffs_address result = ffs_dir_find_impl(disk, root, name);

	free(mutable_path);
	return result;
}

ffs_address ffs_dir_find_impl(ffs_disk disk, ffs_address entry, const char *name)
{
	FFS_LOG(1, "disk=%p entry=%p name=%s", disk, entry, name);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	// Parent address is pointed to by path (ie. done searching)
	if(!name) {
		return entry;
	}

	// Need parent directory to find directory with specified name
	struct ffs_entry parent;
	if(ffs_dir_read(disk, entry, FFS_DIR_ENTRY_OFFSET, &parent, sizeof(struct ffs_entry)) != sizeof(struct ffs_entry)) {
		FFS_ERR(1, "failed to read parent");
		return FFS_DIR_ADDRESS_INVALID;
	}

	ffs_address address = {parent.start_block, 0};
	uint32_t chunk_size = parent.size % sb->block_size;
	for(; address.block != FFS_BLOCK_LAST; address.block = ffs_block_next(disk, address.block)) {
		if(!FFS_BLOCK_VALID(address.block)) {
			FFS_ERR(1, "failed to get next block");
			return FFS_DIR_ADDRESS_INVALID;
		}

		// Check each child entry in block
		for(; address.offset < chunk_size; address.offset + sizeof(struct ffs_entry)) {
			struct ffs_entry child;
			if(ffs_dir_read(disk, address, FFS_DIR_ENTRY_OFFSET, &child, sizeof(struct ffs_entry)) != sizeof(struct ffs_entry)) {
				FFS_ERR(1, "failed to read child");
				return FFS_DIR_ADDRESS_INVALID;
			}

			if(strcmp(child.name, name) == 0) {
				const char *next_name = strtok(NULL, "/");
				return ffs_dir_find_impl(disk, address, next_name);
			}
		}

		chunk_size = sb->block_size;
		address.offset = 0;
	}

	FFS_ERR(1, "no address found");
	return FFS_DIR_ADDRESS_INVALID;
}

uint32_t ffs_dir_read(ffs_disk disk, ffs_address entry, uint32_t offset, void *data, uint32_t size)
{
	FFS_LOG(1, "disk=%p entry={block=%u offset=%u} offset=%u data=%p size=%u", disk, entry.block, entry.offset, offset, data, size);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	if(!FFS_DIR_ADDRESS_VALID(sb, entry)) {
		FFS_ERR(1, "entry address invalid");
		return 0;
	}

	void *buffer = malloc(sb->block_size);

	// Read from entry instead
	if(offset == FFS_DIR_ENTRY_OFFSET) {
		if(size != sizeof(struct ffs_entry)) {
			FFS_ERR(1, "invalid entry read size");
			return 0;
		}

		if(ffs_block_read(disk, entry.block, buffer) != 0) {
			FFS_ERR(1, "failed to read entry block");
			return 0;
		}

		// Copy directory entry
		memcpy(data, buffer + entry.offset, size);

		free(buffer);
		return size;
	}

	// Don't do anything
	if(size == 0) {
		free(buffer);
		return 0;
	}

	// Seek to end of where data is going to be read
	ffs_address address = ffs_dir_seek(disk, entry, offset, size);
	if(!FFS_DIR_ADDRESS_VALID(sb, address)) {
		FFS_ERR(1, "seeked address invalid");
		free(buffer);
		return 0;
	}

	uint32_t read = 0;

	// Read data chunk by chunk in reverse
	while(read < size) {
		const uint32_t chunk_size = ffs_min(sb->block_size - address.offset, size - read);
		const uint32_t data_offset = size - (read + chunk_size);
		const uint32_t buffer_offset = sb->block_size - chunk_size;

		// Read block
		if(ffs_block_read(disk, address.block, buffer) != 0) {
			FFS_ERR(1, "block read failed");
			break;
		}

		// Copy appropriate data
		memcpy((uint8_t *) data + data_offset, (uint8_t *) buffer + buffer_offset, chunk_size);
		read += chunk_size;

		address.offset = 0;
		address.block = ffs_block_next(disk, address.block);
		if(!FFS_BLOCK_VALID(address.block)) {
			FFS_ERR(1, "failed to get next block");
			break;
		}
	}

	free(buffer);
	return read;
}

ffs_address ffs_dir_seek(ffs_disk disk, ffs_address entry, uint32_t offset, uint32_t size)
{
	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	// Read directory entry
	struct ffs_entry directory;
	if(ffs_dir_read(disk, entry, FFS_DIR_ENTRY_OFFSET, &directory, sizeof(struct ffs_entry)) != sizeof(struct ffs_entry)) {
		FFS_ERR(1, "failed to read entry");
		return FFS_DIR_ADDRESS_INVALID;
	}

	// Offset out of range
	if(offset >= directory.size) {
		FFS_ERR(1, "offset out of range");
		return FFS_DIR_ADDRESS_INVALID;
	}

	const uint32_t end = offset + size; // End offset of data access
	const uint32_t roffset = directory.size - ffs_min(directory.size, end); // Start at reverse offset

	ffs_address address = {directory.start_block, roffset};
	uint32_t chunk_size = directory.size % sb->block_size;

	// Seek chunk by chunk
	while(address.offset >= chunk_size) {
		address.offset -= chunk_size;
		address.block = ffs_block_next(disk, address.block);
		if(!FFS_BLOCK_VALID(address.block)) {
			FFS_ERR(1, "failed to get next block");
			return FFS_DIR_ADDRESS_INVALID;
		}

		chunk_size = sb->block_size;
	}
	
	address.offset = chunk_size - address.offset; // Flip offset since its the reverse offset
	return address;
}

uint32_t ffs_dir_write(ffs_disk disk, ffs_address entry, uint32_t offset, const void *data, uint32_t size)
{
	FFS_LOG(1, "disk=%p entry={block=%u offset=%u} offset=%u data=%p size=%u", disk, entry.block, entry.offset, offset, data, size);

	const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	if(!FFS_DIR_ADDRESS_VALID(sb, entry)) {
		FFS_ERR(1, "entry address invalid");
		return 0;
	}

	void *buffer = malloc(sb->block_size);

	// Write to entry instead
	if(offset == FFS_DIR_ENTRY_OFFSET) {
		if(size != sizeof(struct ffs_entry)) {
			FFS_ERR(1, "invalid entry write size");
			return 0;
		}

		// Read entry block
		if(ffs_block_read(disk, entry.block, buffer) != 0) {
			FFS_ERR(1, "failed to read entry block");
			return -1;
		}

		// Copy directory entry
		memcpy(buffer, data, size);

		// Write entry block
		if(ffs_block_write(disk, entry.block, buffer) != 0) {
			FFS_ERR(1, "failed to write entry block");
			return -1;
		}

		free(buffer);
		return size;
	}

	// Don't do anything
	if(size == 0) {
		free(buffer);
		return 0;
	}

	// Seek to end of where data is going to be written
	ffs_address address = ffs_dir_seek(disk, entry, offset, size);
	if(!FFS_DIR_ADDRESS_VALID(sb, address)) {
		FFS_ERR(1, "seeked address invalid");
		free(buffer);
		return 0;
	}

	uint32_t written = 0;

	// Write data chunk by chunk in reverse
	while(written < size) {
		const uint32_t chunk_size = ffs_min(sb->block_size - address.offset, size - written);
		const uint32_t data_offset = size - (written + chunk_size);
		const uint32_t buffer_offset = sb->block_size - chunk_size;

		// Read block to copy existing data
		// Optimization: Don't need to do this if we are reading the entire block
		if(chunk_size < sb->block_size) {
			if(ffs_block_read(disk, address.block, buffer) != 0) {
				FFS_ERR(1, "block read failed");
				break;
			}
		}

		// Copy appropriate data
		memcpy((uint8_t *) buffer + buffer_offset, (uint8_t *) data + data_offset, chunk_size);
		written += chunk_size;

		// Write block
		if(ffs_block_write(disk, address.block, buffer) != 0) {
			FFS_ERR(1, "block write failed");
			break;
		}

		address.offset = 0;
		address.block = ffs_block_next(disk, address.block);
		if(!FFS_BLOCK_VALID(address.block)) {
			FFS_ERR(1, "failed to get next block");
			break;
		}
	}

	free(buffer);
	return written;
}

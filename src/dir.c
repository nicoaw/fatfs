#include "aux.h"
#include "block.h"
#include "dir.h"
#include <stdlib.h>
#include <string.h>

const address DIR_ADDRESS_INVALID = {BLOCK_INVALID, 0};

// Recursively find address in parent directory starting with name
// Returns invalid address on failure
address dir_find_impl(disk disk, address entry, const char *name);

// Read or writes at most size bytes to offset
// Stops at end of directory
// Reads or writes to entry when offset is DIR_ENTRY_OFFSET, size should be size of entry
// Returns count of bytes read or written
uint32_t dir_readwrite(disk disk, address entry, uint32_t offset, void *readdata, const void *writedata, uint32_t size);

int dir_alloc(disk disk, address entry, uint32_t size)
{
	LOG(1, "disk=%p entry={block=%u offset=%u} size=%u", disk, entry.block, entry.offset, size);

	const struct superblock *sb = disk_superblock(disk);

	// Read directory to allocate space for
	struct entry directory;
	if(dir_read(disk, entry, DIR_ENTRY_OFFSET, &directory, sizeof(struct entry)) != sizeof(struct entry)) {
		ERR(1, "failed to read directory");
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
		directory.start_block = block_alloc(disk, directory.start_block);
		if(!BLOCK_VALID(directory.start_block)) {
			// TODO: already allocated blocks are lost because the directory is not updated
			ERR(1, "failed to allocate block");
			return -1;
		}

		allocated += sb->block_size;
	}

	// Write updated directory information
	directory.size += size;
	if(dir_write(disk, entry, DIR_ENTRY_OFFSET, &directory, sizeof(struct entry)) != sizeof(struct entry)) {
		ERR(1, "failed to write directory");
		return -1;
	}

	return 0;
}

int dir_free(disk disk, address entry, uint32_t size)
{
	LOG(1, "disk=%p entry={block=%u offset=%u} size=%u", disk, entry.block, entry.offset, size);

	const struct superblock *sb = disk_superblock(disk);

	// Read directory to free space from
	struct entry directory;
	if(dir_read(disk, entry, DIR_ENTRY_OFFSET, &directory, sizeof(struct entry)) != sizeof(struct entry)) {
		ERR(1, "failed to read directory");
		return -1;
	}

	// Cannot free more space then the directory currently has allocated
	if(size > directory.size) {
		ERR(1, "invalid free range");
		return -1;
	}

	// Psuedo-free allocated space in head block
	const uint32_t allocated = directory.size % sb->block_size;
	uint32_t freed = allocated;

	while(freed <= size) {
		// Get next block before freeing the head
		block next = block_next(disk, directory.start_block);
		if(next != BLOCK_LAST && !BLOCK_VALID(next)) {
			ERR(1, "failed to get next block");
			return -1;
		}

		if(block_free(disk, directory.start_block) != 0) {
			ERR(1, "failed to free block");
			return -1;
		}

		directory.start_block = next;
		freed += sb->block_size;
	}
	
	// Write updated directory information
	directory.size -= size;
	if(dir_write(disk, entry, DIR_ENTRY_OFFSET, &directory, sizeof(struct entry)) != sizeof(struct entry)) {
		ERR(1, "failed to write directory");
		return -1;
	}

	return 0;
}

address dir_find(disk disk, const char *path)
{
	LOG(1, "disk=%p path=%s", disk, path);

	// Need mutable path for strtok
	char *mutable_path = malloc((strlen(path) + 1) * sizeof(char));
	strcpy(mutable_path, path);

	const char *name = strtok(mutable_path, "/");

	address root = {disk_superblock(disk)->root_block, 0};
	address result = dir_find_impl(disk, root, name);

	free(mutable_path);
	return result;
}

address dir_find_impl(disk disk, address entry, const char *name)
{
	LOG(1, "disk=%p entry={block=%u offset=%u} name=%s", disk, entry.block, entry.offset, name);

	const struct superblock *sb = disk_superblock(disk);

	// Parent address is pointed to by path (ie. done searching)
	if(!name) {
		return entry;
	}

	// Need parent directory to find directory with specified name
	struct entry parent;
	if(dir_read(disk, entry, DIR_ENTRY_OFFSET, &parent, sizeof(struct entry)) != sizeof(struct entry)) {
		ERR(1, "failed to read parent");
		return DIR_ADDRESS_INVALID;
	}

	address address = {parent.start_block, 0};
	uint32_t chunk_size = parent.size % sb->block_size;
	for(; address.block != BLOCK_LAST; address.block = block_next(disk, address.block)) {
		if(!BLOCK_VALID(address.block)) {
			ERR(1, "failed to get block");
			return DIR_ADDRESS_INVALID;
		}

		// Check each child entry in block
		for(; address.offset < chunk_size; address.offset += sizeof(struct entry)) {
			struct entry child;
			if(dir_read(disk, address, DIR_ENTRY_OFFSET, &child, sizeof(struct entry)) != sizeof(struct entry)) {
				ERR(1, "failed to read child");
				return DIR_ADDRESS_INVALID;
			}

			if(strcmp(child.name, name) == 0) {
				const char *next_name = strtok(NULL, "/");
				return dir_find_impl(disk, address, next_name);
			}
		}

		chunk_size = sb->block_size;
		address.offset = 0;
	}

	ERR(1, "no address found");
	return DIR_ADDRESS_INVALID;
}

uint32_t dir_read(disk disk, address entry, uint32_t offset, void *data, uint32_t size)
{
	LOG(1, "disk=%p entry={block=%u offset=%u} offset=%u data=%p size=%u", disk, entry.block, entry.offset, offset, data, size);
	return dir_readwrite(disk, entry, offset, data, NULL, size);
}

uint32_t dir_readwrite(disk disk, address entry, uint32_t offset, void *readdata, const void *writedata, uint32_t size)
{
	LOG(1, "disk=%p entry={block=%u offset=%u} offset=%u readdata=%p writedata=%p size=%u", disk, entry.block, entry.offset, offset, readdata, writedata, size);

	const struct superblock *sb = disk_superblock(disk);

	if(!DIR_ADDRESS_VALID(sb, entry)) {
		ERR(1, "invalid entry address");
		return 0;
	}

	void *buffer = malloc(sb->block_size);

	// Read entry block
	if(block_read(disk, entry.block, buffer) != 0) {
		ERR(1, "failed to read entry block");
		return -1;
	}

	// Get directory entry
	struct entry *directory = buffer + entry.offset;

	// Read/Write on entry instead
	if(offset == DIR_ENTRY_OFFSET) {
		if(size != sizeof(struct entry)) {
			ERR(1, "invalid entry size");
			return 0;
		}

		if(readdata) {
			// Get directory entry data
			memcpy(readdata, directory, size);
		}

		if(writedata) {
			// Set directory entry data
			memcpy(directory, writedata, size);

			// Write entry block
			if(block_write(disk, entry.block, buffer) != 0) {
				ERR(1, "failed to write entry block");
				return -1;
			}
		}

		free(buffer);
		return size;
	}

	// Don't do anything
	if(size == 0) {
		free(buffer);
		return 0;
	}

	const uint32_t blocks = directory->size / sb->block_size;
	const uint32_t end = offset + size;

	// Can't read/write outside of directory
	if(end > directory->size) {
		ERR(1, "directory access out of range");
		return 0;
	}

	// Access boundaries
	const uint32_t first_index = blocks - end / sb->block_size;
	uint32_t first_offset = end % sb->block_size;
	const uint32_t last_index = blocks - offset / sb->block_size;
	const uint32_t last_offset = offset % sb->block_size;

	block block = directory->start_block;
	uint32_t count = 0;

	for(uint32_t i = 0; i <= last_index; ++i) {
		if(!BLOCK_VALID(block)) {
			ERR(1, "failed to get block");
			free(buffer);
			return 0;
		}

		// In access boundary
		if(i >= first_index) {
			const uint32_t buffer_offset = i == last_index ? last_offset : 0;
			const uint32_t data_size = first_offset - buffer_offset;
			const uint32_t data_offset = size - (count + data_size);

			// Only read data when needed
			// Data is not needed when writing whole block
			if(readdata || data_size < sb->block_size) {
				if(block_read(disk, block, buffer) != 0) {
					ERR(1, "failed to read block");
					break;
				}
			}

			if(readdata) {
				// Get appropriate data
				memcpy(readdata + data_offset, buffer + buffer_offset, data_size);
			}

			if(writedata) {
				// Set appropriate data
				memcpy(buffer + buffer_offset, writedata + data_offset, data_size);

				if(block_write(disk, block, buffer) != 0) {
					ERR(1, "failed to write block");
					break;
				}
			}

			first_offset = sb->block_size;
			count += data_size;
		}

		block = block_next(disk, block);
	}

	free(buffer);
	return count;
}

uint32_t dir_write(disk disk, address entry, uint32_t offset, const void *data, uint32_t size)
{
	LOG(1, "disk=%p entry={block=%u offset=%u} offset=%u data=%p size=%u", disk, entry.block, entry.offset, offset, data, size);
	return dir_readwrite(disk, entry, offset, NULL, data, size);
}

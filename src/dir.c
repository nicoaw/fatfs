#include "block.h"
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

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
	syslog(LOG_DEBUG, "allocating %u bytes for entry %u:%u", size, entry.block, entry.offset);

	const struct superblock *sb = disk_superblock(disk);

	if(!DIR_ADDRESS_VALID(sb, entry)) {
		syslog(LOG_ERR, "invalid entry %u:%u", entry.block, entry.offset);
		return 0;
	}

	// Read directory to allocate space for
	struct entry directory;
	if(dir_read(disk, entry, DIR_ENTRY_OFFSET, &directory, sizeof(struct entry)) != sizeof(struct entry)) {
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
			return -1;
		}

		allocated += sb->block_size;
	}

	// Write updated directory information
	directory.size += size;
	if(dir_write(disk, entry, DIR_ENTRY_OFFSET, &directory, sizeof(struct entry)) != sizeof(struct entry)) {
		return -1;
	}

	syslog(LOG_DEBUG, "allocated %u bytes for entry %u:%u", size, entry.block, entry.offset);
	return 0;
}

int dir_free(disk disk, address entry, uint32_t size)
{
	syslog(LOG_DEBUG, "freeing %u bytes for entry %u:%u", size, entry.block, entry.offset);

	const struct superblock *sb = disk_superblock(disk);

	if(!DIR_ADDRESS_VALID(sb, entry)) {
		syslog(LOG_ERR, "invalid entry %u:%u", entry.block, entry.offset);
		return 0;
	}

	// Read directory to free space from
	struct entry directory;
	if(dir_read(disk, entry, DIR_ENTRY_OFFSET, &directory, sizeof(struct entry)) != sizeof(struct entry)) {
		return -1;
	}

	// Cannot free more space then the directory currently has allocated
	if(size > directory.size) {
		syslog(LOG_ERR, "cannot free %u bytes for entry of %u bytes", size, directory.size);
		return -1;
	}

	// Psuedo-free allocated space in head block
	const uint32_t allocated = directory.size % sb->block_size;
	uint32_t freed = allocated;

	while(freed <= size) {
		if(!BLOCK_VALID(directory.start_block)) {
			return -1;
		}

		// Get next block before freeing the head
		block next = block_next(disk, directory.start_block);

		if(block_free(disk, directory.start_block) != 0) {
			return -1;
		}

		directory.start_block = next;
		freed += sb->block_size;
	}
	
	// Write updated directory information
	directory.size -= size;
	if(dir_write(disk, entry, DIR_ENTRY_OFFSET, &directory, sizeof(struct entry)) != sizeof(struct entry)) {
		return -1;
	}

	syslog(LOG_DEBUG, "freed %u bytes for entry %u:%u", size, entry.block, entry.offset);
	return 0;
}

address dir_find(disk disk, const char *path)
{
	syslog(LOG_DEBUG, "finding entry for '%s'", path);

	const struct superblock *sb = disk_superblock(disk);

	// Need mutable path for strtok
	char *mutable_path = malloc((strlen(path) + 1) * sizeof(char));
	strcpy(mutable_path, path);

	const char *name = strtok(mutable_path, "/");

	address root = {disk_superblock(disk)->root_block, 0};
	address result = dir_find_impl(disk, root, name);

	free(mutable_path);

	if(DIR_ADDRESS_VALID(sb, result)) {
		syslog(LOG_DEBUG, "found entry %u:%u for '%s'", result.block, result.offset, path);
	} else {
		syslog(LOG_ERR, "no entry found for '%s'", path);
	}
	return result;
}

address dir_find_impl(disk disk, address entry, const char *name)
{
	const struct superblock *sb = disk_superblock(disk);

	// Parent address is pointed to by path (ie. done searching)
	if(!name) {
		return entry;
	}

	// Need parent directory to find directory with specified name
	struct entry parent;
	if(dir_read(disk, entry, DIR_ENTRY_OFFSET, &parent, sizeof(struct entry)) != sizeof(struct entry)) {
		return DIR_ADDRESS_INVALID;
	}

	// Needed to avoid integer underflow when calculating chunk size
	if(parent.size == 0) {
		return DIR_ADDRESS_INVALID;
	}

	address address = {parent.start_block, 0};
	uint32_t chunk_size = (parent.size - 1) % sb->block_size;
	for(; address.block != BLOCK_LAST; address.block = block_next(disk, address.block)) {
		if(!BLOCK_VALID(address.block)) {
			return DIR_ADDRESS_INVALID;
		}

		// Check each child entry in block
		for(; address.offset < chunk_size; address.offset += sizeof(struct entry)) {
			struct entry child;
			if(dir_read(disk, address, DIR_ENTRY_OFFSET, &child, sizeof(struct entry)) != sizeof(struct entry)) {
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

	return DIR_ADDRESS_INVALID;
}

uint32_t dir_read(disk disk, address entry, uint32_t offset, void *data, uint32_t size)
{
	return dir_readwrite(disk, entry, offset, data, NULL, size);
}

uint32_t dir_readwrite(disk disk, address entry, uint32_t offset, void *readdata, const void *writedata, uint32_t size)
{
	if(offset == DIR_ENTRY_OFFSET) {
		syslog(LOG_DEBUG, "%s%s%s entry %u:%u", readdata ? "reading" : "", (readdata && writedata) ? "/" : "", writedata ? "writing" : "", entry.block, entry.offset);
	} else {
		syslog(LOG_DEBUG, "%s%s%s %u bytes at offset %u for entry %u:%u",
				readdata ? "reading" : "", (readdata && writedata) ? "/" : "", writedata ? "writing" : "", size, offset, entry.block, entry.offset);
	}

	const struct superblock *sb = disk_superblock(disk);

	if(!DIR_ADDRESS_VALID(sb, entry)) {
		syslog(LOG_ERR, "invalid entry %u:%u", entry.block, entry.offset);
		return 0;
	}

	void *buffer = malloc(sb->block_size);

	// Read entry block
	if(block_read(disk, entry.block, buffer) != 0) {
		return -1;
	}

	// Get directory entry
	struct entry *directory = buffer + entry.offset;

	// Read/Write on entry instead
	if(offset == DIR_ENTRY_OFFSET) {
		if(size != sizeof(struct entry)) {
			syslog(LOG_ERR, "invalid entry size %u", size);
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
				return -1;
			}
		}

		free(buffer);
		syslog(LOG_DEBUG, "%s%s%s entry %u:%u", readdata ? "read" : "", (readdata && writedata) ? "/" : "", writedata ? "wrote" : "", entry.block, entry.offset);
		return size;
	}

	// Don't do anything
	// Needed to avoid integer underflow when calculating blocks and end
	if(size == 0 || directory->size == 0) {
		free(buffer);
		return 0;
	}

	// Can't read/write outside of directory
	if(offset >= directory->size) {
		syslog(LOG_ERR, "offset %u out of range of entry size %u", offset, size);
		return 0;
	}

	const uint32_t blocks = (directory->size - 1) / sb->block_size;
	const uint32_t end = offset + size - 1;

	syslog(LOG_DEBUG, "blocks=%u end=%u", blocks, end);

	// Access boundaries
	const uint32_t first_index = blocks - end / sb->block_size;
	uint32_t first_offset = end % sb->block_size;
	const uint32_t last_index = blocks - offset / sb->block_size;
	const uint32_t last_offset = offset % sb->block_size;

	block block = directory->start_block;
	uint32_t count = 0;

	for(uint32_t i = 0; i <= last_index; ++i) {
		if(!BLOCK_VALID(block)) {
			break;
		}

		syslog(LOG_DEBUG, "first_index=%u first_offset=%u last_index=%u last_offset=%u", first_index, first_offset, last_index, last_offset);

		// In access boundary
		if(i >= first_index) {
			const uint32_t buffer_offset = i == last_index ? last_offset : 0;
			const uint32_t data_size = first_offset - buffer_offset + 1;
			const uint32_t data_offset = size - (count + data_size);

			syslog(LOG_DEBUG, "buffer_offset=%u data_size=%u data_offset=%u", buffer_offset, data_size, data_offset);

			// Only read data when needed
			// Data is not needed when writing whole block
			if(readdata || data_size < sb->block_size) {
				if(block_read(disk, block, buffer) != 0) {
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
					break;
				}
			}

			first_offset = sb->block_size - 1;
			count += data_size;
		}

		block = block_next(disk, block);
	}

	free(buffer);
	syslog(count == size ? LOG_DEBUG : LOG_ERR, "%s%s%s %u bytes at offset %u for entry %u:%u",
			readdata ? "read" : "", (readdata && writedata) ? "/" : "", writedata ? "wrote" : "", count, offset, entry.block, entry.offset);
	return count;
}

uint32_t dir_write(disk disk, address entry, uint32_t offset, const void *data, uint32_t size)
{
	return dir_readwrite(disk, entry, offset, NULL, data, size);
}

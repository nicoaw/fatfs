#include "entry.h"
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

// Recursively find address from entry
// Returns invalid address on failure
address entry_find(disk d, address entry, const char *name);

address entry_address(disk d, const char *path)
{
	syslog(LOG_DEBUG, "finding entry for '%s'", path);

	const struct superblock *sb = disk_superblock(disk);

	// Need mutable path for strtok
	char *mutable_path = malloc(strlen(path) + 1);
	strcpy(mutable_path, path);

	const char *name = strtok(mutable_path, "/");

	address root = {disk_superblock(disk)->root_block, sizeof(struct entry)};
	address result = entry_find(disk, root, name);

	if(DIR_ADDRESS_VALID(sb, result)) {
		syslog(LOG_DEBUG, "found entry %u:%u for '%s'", result.block, result.offset, path);
	} else {
		syslog(LOG_ERR, "no entry found for '%s'", path);
	}

	free(mutable_path);
	return result;
}

uint32_t entry_alloc(disk d, address entry, uint32_t size)
{
	syslog(LOG_DEBUG, "allocating %u bytes for entry %u:%u", size, entry.end_block, entry.end_offset);

	const struct superblock *sb = disk_superblock(disk);

	struct entry ent;
	if(dir_read(d, entry, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		return 0;
	}

	block next = ent.start_block;
	uint32_t block_unallocated = sb->block_size - ENTRY_FIRST_CHUNK_SIZE(sb, ent);
	uint32_t allocated = 0;

	// Allocate block by block
	while(1) {
		if(!BLOCK_VALID(next)) {
			break;
		}

		ent.start_block = next;

		const uint32_t max_allocation_size = allocated + block_unallocated;
		if(max_allocation_size > size) {
			// Done allocating
			allocated = size;
			break;
		} else {
			// Allocate chunk
			allocated += block_unallocated;

			// Allocate another block
			block_unallocated = sb->block_size;
			next = block_alloc(d, next);
		}
	}

	// Update size and access and modify times
	time_t t = time(NULL);
	ent.access_time = t;
	ent.modify_time = t;
	ent.size += allocated;
	if(dir_write(disk, entry, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		// TODO Blocks were allocated but entry cannot access them
		syslog(LOG_CRIT, "failed to update entry %u:%u", entry.end_block, entry.end_offset);
		return 0;
	}

	syslog(LOG_DEBUG, "allocated %u bytes for entry %u:%u", allocated, entry.end_block, entry.end_offset);
	return allocated;
}

address entry_find(disk d, address entry, const char *name)
{
	const struct superblock *sb = disk_superblock(disk);

	// Found entry address
	if(!name) {
		return entry;
	}

	struct entry ent;
	if(dir_read(disk, entry, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		return DIR_ADDRESS_INVALID;
	}

	// Needed to avoid integer underflow when calculating chunk size
	if(ent.size == 0) {
		return DIR_ADDRESS_INVALID;
	}

	address addr = {ent.start_block, sizeof(struct entry)}
	uint32_t chunk_size = ENTRY_FIRST_CHUNK_SIZE(sb, ent);

	// Scan block by block
	while(1) {
		if(!BLOCK_VALID(addr.end_block)) {
			break;
		}



		addr.end_block = block_next(d, addr.end_block);
		addr.end_offset = sizeof(struct entry);
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

uint32_t entry_free(disk d, address entry, uint32_t size)
{
	syslog(LOG_DEBUG, "freeing %u bytes for entry %u:%u", size, entry.end_block, entry.end_offset);

	const struct superblock *sb = disk_superblock(disk);

	struct entry ent;
	if(dir_read(d, entry, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		return 0;
	}

	// Cannot free more space then the directory currently has allocated
	if(size > ent.size) {
		syslog(LOG_ERR, "cannot free %u bytes for entry of %u bytes", size, ent.size);
		return 0;
	}

	block next = ent.start_block;
	uint32_t block_allocated = ENTRY_FIRST_CHUNK_SIZE(sb, ent);
	uint32_t freed = 0;

	// Free block by block
	while(1) {
		if(!BLOCK_VALID(next)) {
			break;
		}

		const uint32_t max_free_size = freed + block_allocated;
		if(max_free_size > size) {
			// Done freeing
			freed = size;
			break;
		} else {
			// Free block
			if(block_free(d, ent.start_block) != 0) {
				break;
			}

			ent.start_block = next;
			freed += block_allocated;

			// Update next block information
			block_allocated = sb->block_size;
			next = block_next(d, next);
		}
	}
	
	// Update size and access and modify times
	time_t t = time(NULL);
	ent.access_time = t;
	ent.modify_time = t;
	ent.size -= freed;
	if(dir_write(disk, entry, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		// TODO Blocks were freed but entry still tries to use them
		syslog(LOG_CRIT, "failed to update entry %u:%u", entry.end_block, entry.end_offset);
		return 0;
	}

	syslog(LOG_DEBUG, "freed %u bytes for entry %u:%u", freed, entry.end_block, entry.end_offset);
	return freed;
}


uint32_t entry_access(disk d, address entry, uint32_t offset, void *readdata, const void *writedata, uint32_t size)
{
	syslog(LOG_DEBUG, "%s%s%s %u bytes from entry %u:%u at %u",
			readdata ? "reading" : "",
			readdata & writedata ? "/" : "",
			writedata ? "writing" : "",
			size,
			offset.end_block, offset.end_offset,
			offset
			);

	const struct superblock *sb = disk_superblock(disk);

	struct entry ent;
	if(dir_read(d, entry, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		return 0;
	}

	// Offset cannot be past directory end
	if(offset >= ent.size) {
		syslog(LOG_ERR, "offset %u out of directory range %u", offset, ent.size);
		return 0;
	}

	const uint32_t end_offset = ent.size - offset;
	address addr = {ent.start_block, ENTRY_FIRST_CHUNK_SIZE(sb, ent)};
	addr = dir_seek(d, addr, end_offset);
	if(!DIR_ADDRESS_VALID(sb, addr)) {
		return 0;
	}

	// Perform directory access
	uint32_t accessed = dir_access(d, addr, readdata, writedata, size);

	// Update access and modify times
	time_t t = time(NULL);
	ent.access_time = t;
	ent.modify_time = writedata ? t : ent.modify_time;
	if(dir_write(d, entry, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		// Entry has been read/written but entry has not been updated
		syslog(LOG_CRIT, "failed to update entry %u:%u", entry.end_block, entry.end_offset);
		return 0;
	}

	syslog(LOG_DEBUG, "%s%s%s %u bytes from entry %u:%u at %u",
			readdata ? "read" : "",
			readdata & writedata ? "/" : "",
			writedata ? "wrote" : "",
			accessed,
			offset.end_block, offset.end_offset,
			offset
			);

	return accessed;
}

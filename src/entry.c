#include "entry.h"
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

uint32_t entry_alloc(disk d, address entry, uint32_t size)
{
	syslog(LOG_DEBUG, "allocating %u bytes for entry %u:%u", size, entry.end_block, entry.end_offset);

	const struct superblock *sb = disk_superblock(d);

	struct entry ent;
	if(dir_read(d, entry, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		return 0;
	}

	block next = ent.start_block;
	uint32_t block_unallocated = sb->block_size - ENTRY_FIRST_CHUNK_SIZE(sb, ent);
	uint32_t allocated = 0;

	// Completely unallocated blocks don't exist
	if(block_unallocated == sb->block_size) {
		block_unallocated = 0;
	}

	// Allocate block by block
	while(1) {
		ent.start_block = next;

		const uint32_t max_allocation_size = allocated + block_unallocated;
		if(max_allocation_size >= size) {
			// Done allocating
			allocated = size;
			break;
		} else {
			// Allocate chunk
			allocated += block_unallocated;

			// Allocate another block
			block_unallocated = sb->block_size;
			next = block_alloc(d, next);
			if(!BLOCK_VALID(next)) {
				break;
			}
		}
	}

	// Update size and access and modify times
	time_t t = time(NULL);
	ent.access_time = t;
	ent.modify_time = t;
	ent.size += allocated;
	if(dir_write(d, entry, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		// TODO Blocks were allocated but entry cannot access them
		syslog(LOG_CRIT, "failed to update entry %u:%u", entry.end_block, entry.end_offset);
		return 0;
	}

	syslog(LOG_DEBUG, "allocated %u bytes for entry %u:%u", allocated, entry.end_block, entry.end_offset);
	return allocated;
}

address entry_find(disk d, address entry, const char *name)
{
	syslog(LOG_DEBUG, "finding '%s' in entry %u:%u", name, entry.end_block, entry.end_offset);

	const struct superblock *sb = disk_superblock(d);

	struct entry parent;
	if(dir_read(d, entry, &parent, sizeof(struct entry)) != sizeof(struct entry)) {
		return DIR_ADDRESS_INVALID;
	}

	// Read all child entries in parent
	struct entry *children = malloc(parent.size);
	if(entry_read(d, entry, 0, children, parent.size) != parent.size) {
		return DIR_ADDRESS_INVALID;
	}

	// Find child entry with correct name
	for(uint32_t i = 0; i < parent.size / sizeof(struct entry); ++i) {
		if(strcmp(children[i].name, name) == 0) {
			free(children);

			// Get child entry address
			address addr = {parent.start_block, ENTRY_FIRST_CHUNK_SIZE(sb, parent)};
			addr = dir_seek(d, addr, i * sizeof(struct entry));
			if(!DIR_ADDRESS_VALID(sb, addr)) {
				return DIR_ADDRESS_INVALID;
			}

			syslog(LOG_DEBUG, "Found '%s' in entry %u:%u at %u:%u",
					name,
					entry.end_block,
					entry.end_offset,
					addr.end_block,
					addr.end_offset);
			return addr;
		}
	}
	
	free(children);
	syslog(LOG_ERR, "Failed to find '%s' in entry %u:%u", name, entry.end_block, entry.end_offset);
	return DIR_ADDRESS_INVALID;
}

uint32_t entry_free(disk d, address entry, uint32_t size)
{
	syslog(LOG_DEBUG, "freeing %u bytes for entry %u:%u", size, entry.end_block, entry.end_offset);

	const struct superblock *sb = disk_superblock(d);

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
	if(dir_write(d, entry, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
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
			readdata && writedata ? "/" : "",
			writedata ? "writing" : "",
			size,
			entry.end_block, entry.end_offset,
			offset
			);

	const struct superblock *sb = disk_superblock(d);

	struct entry ent;
	if(dir_read(d, entry, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		return 0;
	}

	// Offset cannot be past directory end
	if(offset >= ent.size) {
		syslog(LOG_ERR, "offset %u out of directory range %u", offset, ent.size);
		return 0;
	}

	const uint32_t end_offset = ent.size - (offset + size);
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
			readdata && writedata ? "/" : "",
			writedata ? "wrote" : "",
			accessed,
			entry.end_block, entry.end_offset,
			offset
			);

	return accessed;
}

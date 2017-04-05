#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

const address DIR_ADDRESS_INVALID = {BLOCK_INVALID, -1};

uint32_t dir_access(disk d, address offset, void *readdata, const void *writedata, uint32_t size)
{
	syslog(LOG_DEBUG, "%s%s%s %u bytes reverse from %u:%u",
			readdata ? "reading" : "",
			readdata && writedata ? "/" : "",
			writedata ? "writing" : "",
			size,
			offset.end_block, offset.end_offset
			);

	const struct superblock *sb = disk_superblock(d);

	// Offset cannot be invalid
	if(!DIR_ADDRESS_VALID(sb, offset)) {
		syslog(LOG_ERR, "invalid offset %u:%u", offset.end_block, offset.end_offset);
		return 0;
	}

	// Do not access anything
	if(size == 0) {
		return 0;
	}

	void *buffer = malloc(sb->block_size);
	uint32_t accessed = 0; // Amount of bytes accessed

	// Access data block by block
	while(accessed < size) {
		if(!BLOCK_VALID(offset.end_block)) {
			syslog(LOG_ERR, "invalid block %u", offset.end_block);
			break;
		}

		// Access boundaries
		const uint32_t max_access_size = accessed + offset.end_offset;
		const uint32_t block_offset = max_access_size > size ? offset.end_offset - (size - accessed) : 0;
		const uint32_t data_size = offset.end_offset - block_offset;
		const uint32_t data_offset = size - (accessed + data_size);

		// Read is not needed when writing entire block
		if(readdata || data_size < sb->block_size) {
			if(block_read(d, offset.end_block, buffer) != 0) {
				break;
			}
		}

		if(readdata) {
			// Get appropriate data
			memcpy(readdata + data_offset, buffer + block_offset, data_size);
		}

		if(writedata) {
			// Set appropriate data
			memcpy(buffer + block_offset, writedata + data_offset, data_size);

			if(block_write(d, offset.end_block, buffer) != 0) {
				break;
			}
		}

		accessed += data_size;

		// Seek next address
		offset.end_block = block_next(d, offset.end_block);
		offset.end_offset = sb->block_size;
	}

	syslog(LOG_DEBUG, "%s%s%s %u bytes reverse",
			readdata ? "read" : "",
			readdata && writedata ? "/" : "",
			writedata ? "wrote" : "",
			accessed
			);

	free(buffer);
	return accessed;
}

address dir_seek(disk d, address addr, uint32_t offset)
{
	syslog(LOG_DEBUG, "seeking %u:%u backward by %u", addr.end_block, addr.end_offset, offset);

	const struct superblock *sb = disk_superblock(d);

	uint32_t seeked = 0;

	// Seek data block by block
	while(1) {
		if(!BLOCK_VALID(addr.end_block)) {
			syslog(LOG_ERR, "invalid block %u", addr.end_block);
			break;
		}

		const uint32_t max_seek_size = seeked + addr.end_offset;
		if(max_seek_size >= offset) {
			// Done seeking
			addr.end_offset -= offset - seeked;
			break;
		} else {
			// Seek next address
			addr.end_block = block_next(d, addr.end_block);
			addr.end_offset = sb->block_size;
		}

		seeked += max_seek_size;
	}

	syslog(LOG_DEBUG, "seeked to %u:%u", addr.end_block, addr.end_offset);
	return addr;
}

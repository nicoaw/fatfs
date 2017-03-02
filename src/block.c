#include "block.h"
#include <stdlib.h>
#include <syslog.h>

block block_alloc(disk disk, block next)
{
	syslog(LOG_DEBUG, "allocating block before %u", next);

	// Next must be valid or BLOCK_LAST
	if(next != BLOCK_LAST && !BLOCK_VALID(next)) {
		syslog(LOG_ERR, "invalid block %u", next);
		return BLOCK_INVALID;
	}

    const struct superblock *sb = disk_superblock(disk);
	block *fat_buffer = malloc(sb->block_size);

	// Find a free block using FAT
	for(uint32_t i = 0; i < sb->fat_block_count; ++i) {
		// Read FAT
		const block fat = BLOCK_FAT + i;
		if(block_read(disk, fat, fat_buffer) != 0) {
			free(fat_buffer);
			return BLOCK_INVALID;
		}

		for(uint32_t j = 0; j < BLOCK_FAT_ENTRY_COUNT(sb); ++j) {
			// Found free block
			if(fat_buffer[j] == BLOCK_FREE) {
				const block allocated = j + i * BLOCK_FAT_ENTRY_COUNT(sb);
				fat_buffer[j] = next;

				// Write updated FAT
				if(block_write(disk, fat, fat_buffer) != 0) {
					free(fat_buffer);
					return BLOCK_INVALID;
				}

				free(fat_buffer);
				syslog(LOG_DEBUG, "allocated block before %u", next);
				return allocated;
			}
		}
	}

	free(fat_buffer);
	syslog(LOG_ERR, "failed to allocate block");
	return BLOCK_INVALID;
}

int block_free(disk disk, block head)
{
	syslog(LOG_DEBUG, "freeing block %u", head);

	// Head must be valid
	if(!BLOCK_VALID(head)) {
		syslog(LOG_ERR, "invalid block %u", head);
		return -1;
	}

    const struct superblock *sb = disk_superblock(disk);
	const block fat = BLOCK_FAT_BLOCK(sb, head);
	block *fat_buffer = malloc(sb->block_size);

	// Read FAT
	if(block_read(disk, fat, fat_buffer) != 0) {
		free(fat_buffer);
		return -1;
	}

	fat_buffer[BLOCK_FAT_ENTRY(sb, head)] = BLOCK_FREE;

	// Write updated FAT
	if(block_write(disk, fat, fat_buffer) != 0) {
		free(fat_buffer);
		return -1;
	}

	free(fat_buffer);
	syslog(LOG_DEBUG, "freed block %u", head);
	return 0;
}

block block_next(disk disk, block previous)
{
	syslog(LOG_DEBUG, "retreiving block after %u", previous);

	// Previous must be valid
	if(!BLOCK_VALID(previous)) {
		syslog(LOG_ERR, "invalid block %u", previous);
		return BLOCK_INVALID;
	}

    const struct superblock *sb = disk_superblock(disk);
	const block fat = BLOCK_FAT_BLOCK(sb, previous);
	block *fat_buffer = malloc(sb->block_size);

	// Read FAT
	if(block_read(disk, fat, fat_buffer) != 0) {
		free(fat_buffer);
		return BLOCK_INVALID;
	}

	// Get next block according to FAT
	block next = fat_buffer[BLOCK_FAT_ENTRY(sb, previous)];

	free(fat_buffer);
	syslog(LOG_DEBUG, "retreived block %u after %u", next, previous);
	return next;
}

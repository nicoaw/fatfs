#include "aux.h"
#include "block.h"
#include <stdlib.h>

block block_alloc(disk disk, block next)
{
	LOG(0, "disk=%p next=%u", disk, next);

	// Next must be valid or BLOCK_LAST
	if(next != BLOCK_LAST && !BLOCK_VALID(next)) {
		ERR(0, "next block invalid");
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
			ERR(0, "FAT read failed");
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
					ERR(0, "FAT write failed");
					return BLOCK_INVALID;
				}

				free(fat_buffer);
				return allocated;
			}
		}
	}

	free(fat_buffer);
	ERR(0, "failed to find free block");
	return BLOCK_INVALID;
}

int block_free(disk disk, block head)
{
	LOG(0, "disk=%p head=%u", disk, head);

	// Head must be valid
	if(!BLOCK_VALID(head)) {
		ERR(0, "head block invalid");
		return -1;
	}

    const struct superblock *sb = disk_superblock(disk);
	const block fat = BLOCK_FAT_BLOCK(sb, head);
	block *fat_buffer = malloc(sb->block_size);

	// Read FAT
	if(block_read(disk, fat, fat_buffer) != 0) {
		free(fat_buffer);
		ERR(0, "FAT read failed");
		return -1;
	}

	fat_buffer[BLOCK_FAT_ENTRY(sb, head)] = BLOCK_FREE;

	// Write updated FAT
	if(block_write(disk, fat, fat_buffer) != 0) {
		free(fat_buffer);
		ERR(0, "FAT read failed");
		return -1;
	}

	free(fat_buffer);
	return 0;
}

block block_next(disk disk, block previous)
{
	LOG(0, "disk=%p previous=%u", disk, previous);

	// Previous must be valid
	if(!BLOCK_VALID(previous)) {
		ERR(0, "previous block invalid");
		return BLOCK_INVALID;
	}

    const struct superblock *sb = disk_superblock(disk);
	const block fat = BLOCK_FAT_BLOCK(sb, previous);
	block *fat_buffer = malloc(sb->block_size);

	// Read FAT
	if(block_read(disk, fat, fat_buffer) != 0) {
		free(fat_buffer);
		ERR(0, "FAT read failed");
		return BLOCK_INVALID;
	}

	// Get next block according to FAT
	block next = fat_buffer[BLOCK_FAT_ENTRY(sb, previous)];
	free(fat_buffer);
	return next;
}

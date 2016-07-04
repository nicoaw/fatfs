#include <ffs/ffs_block.h>

int ffs_block_alloc(ffs_disk disk, int previous_block)
{
	if(previous_block == FFS_BLOCK_INVALID) {
		return FFS_BLOCK_INVALID;
	}

    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
        return FFS_BLOCK_INVALID;
    }

	const size_t fat_block_entry_count = superblock->block_size / sizeof(int32_t);

	// Find a free block using FAT
	int32_t *fat = malloc(superblock->block_size);
	for(size_t i = 0; i < superblock->fat_block_count; ++i) {
		const int fat_block = FFS_BLOCK_FAT + i;

		if(ffs_block_read(disk, fat_block, fat) != 0) {
			free(fat);
			return FFS_BLOCK_INVALID;
		}

		for(size_t j = 0; j < fat_block_entry_count; ++j) {
			// Found free block
			if(fat[j] == FFS_BLOCK_FREE) {
				const int block = j + i * fat_block_entry_count;
				fat[j] = FFS_BLOCK_LAST;

				if(ffs_block_write(disk, fat_block, fat) != 0) {
					free(fat);
					return FFS_BLOCK_INVALID;
				}

				// Set previous block's next
				if(previous_block != FFS_BLOCK_LAST) {
					const int previous_fat_block = FFS_BLOCK_FAT + previous_block / fat_block_entry_count;
					const size_t previous_fat_entry = previous_block % fat_block_entry_count;

					if(ffs_block_read(disk, previous_fat_block, fat) != 0) {
						free(fat);
						return FFS_BLOCK_INVALID;
					}

					fat[previous_fat_entry] = block;

					if(ffs_block_write(disk, previous_fat_block, fat) != 0) {
						free(fat);
						return FFS_BLOCK_INVALID;
					}
				}

				free(fat);
				return block;
			}
		}
	}

	free(fat);
	return FFS_BLOCK_INVALID;
}

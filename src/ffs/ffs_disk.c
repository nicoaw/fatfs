#include <ffs/ffs_disk.h>
#include <stdio.h>
#include <stdlib.h>

struct ffs_disk_info {
    FILE *file;
    struct ffs_superblock superblock;
};

int ffs_disk_close(ffs_disk disk)
{
    // Disk must not be NULL
    if(!disk) {
        return -1;
    }

    // Must be able to close disk file
    if(fclose(disk->file) != 0) {
        return -1;
    }

    // Done using disk
    free(disk);

    return 0;
}

ffs_disk ffs_disk_open(const char *path, int mode)
{
    ffs_disk disk = (ffs_disk) malloc(sizeof(ffs_disk_info));

	// Disk information could not be allocated
	if(!disk) {
		return NULL;
	}

	// Mode must be valid
    if(
        mode != FFS_DISK_OPEN_RDONLY &&
        mode != FFS_DISK_OPEN_RDWR
    ) {
        return NULL;
    }

    // Find corresponding mode for fopen
    char fmode[] = "r+";
    fmode[mode] = '\0';

    disk->file = fopen(path, fmode);

	// Disk file could not be opened
	if(!disk->file) {
		return NULL;
	}

	// Cache disk superblock
	fread(&disk->superblock, sizeof(struct ffs_superblock), 1, disk->file);

    return disk;
}

const struct ffs_superblock *ffs_disk_superblock(const ffs_disk disk)
{
    // Disk must not be NULL
    if(!disk) {
        return NULL;
    }

    return &disk->superblock;
}

#include <ffs/ffs_disk.h>
#include <stdio.h>

struct ffs_disk_info {
    FILE *file;
    ffs_superblock superblock;
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

}

const struct ffs_superblock *ffs_disk_superblock(const ffs_disk disk)
{
    if(disk) {
        return &disk->superblock;
    } else {
        return NULL;
    }
}

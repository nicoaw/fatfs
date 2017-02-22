#define FUSE_USE_VERSION 26

#include "fatfs_operations.h"
#include <ffs/ffs.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

// Format a disk
// Returns non-zero on failure
int format(int argc, char **argv);

// Mount a disk
// Returns non-zero on failure
int mount(int argc, char **argv);

// Print program usage
void usage(const char *program);

int main(int argc, char** argv)
{
	if(argc > 1) {
		const char *command = argv[1];
		if(strcmp(command, "format") == 0) {
			return format(argc, argv);
		} else if(strcmp(command, "mount") == 0) {
			return mount(argc, argv);
		}
	}

	usage(argv[0]);
	return -1;
}

int format(int argc, char **argv)
{
	const char *path = argv[argc - 1];
	ffs_disk disk = ffs_disk_open(path);

	if(!disk) {
		return -1;
	}

	// Setup superblock
	struct ffs_superblock sb = {
		.magic = 0x2345beef,
		.block_count = 512,
		.block_size = 1024,
	};

	const uint32_t fat_size = sb.block_count / sizeof(ffs_block);
	sb.fat_block_count = 1 + ((fat_size - 1) / sb.block_size); // ceil(fat_size / block_size)

	// Format a new or existing disk
	if(ffs_disk_format(disk, sb) != 0) {
		return -1;
	} 

	ffs_disk_close(disk);
	return 0;
}

int mount(int argc, char **argv)
{
	const char *path = argv[2];
	ffs_disk disk = ffs_disk_open(path);

	// Implemented fuse operations
	struct fuse_operations operations = {
	};

	// Start fuse with appropriate options
	int status = fuse_main(argc - 2, argv + 2, &operations, disk);

	ffs_disk_close(disk);
	return status;
}

void usage(const char *program)
{
	fprintf(stderr, "%s (format|mount) <disk> [OPTIONS...]\n", program);
}

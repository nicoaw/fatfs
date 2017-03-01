#define FUSE_USE_VERSION 26

#include "aux.h"
#include "block.h"
#include "disk.h"
#include "ops.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

// Format a disk
// Returns non-zero on failure
int format_command(int argc, char **argv);

// Mount a disk
// Returns non-zero on failure
int mount_command(int argc, char **argv);

// Print program usage
void usage(const char *program);

int main(int argc, char** argv)
{
	if(argc > 1) {
		const char *command = argv[1];
		if(strcmp(command, "format") == 0) {
			return format_command(argc, argv);
		} else if(strcmp(command, "mount") == 0) {
			return mount_command(argc, argv);
		}
	}

	usage(argv[0]);
	return -1;
}

int format_command(int argc, char **argv)
{
	const char *path = argv[argc - 1];
	disk disk = disk_open(path);

	if(!disk) {
		return -1;
	}

	// Setup superblock
	struct superblock sb = {
		.magic = 0x2345beef,
		.block_count = 512,
		.block_size = 1024,
	};

	const uint32_t fat_size = sb.block_count / sizeof(block);
	sb.fat_block_count = 1 + ((fat_size - 1) / sb.block_size); // ceil(fat_size / block_size)
	sb.root_block = sb.fat_block_count + 1;

	// Format a new or existing disk
	if(disk_format(disk, sb) != 0) {
		return -1;
	} 

	disk_close(disk);
	return 0;
}

int mount_command(int argc, char **argv)
{
	const char *path = argv[2];
	disk disk = disk_open(path);
	if(!disk) {
		ERR(3, "failed to open disk");
		return -1;
	}

	// Implemented fuse operations
	struct fuse_operations operations = {
		.getattr = fatfs_getattr,
		.mkdir = fatfs_mkdir,
		.mknod = fatfs_mknod,
		.open = fatfs_open,
		.read = fatfs_read,
		.readdir = fatfs_readdir,
		.rmdir = fatfs_rmdir,
		.unlink = fatfs_unlink,
		.utimens = fatfs_utimens,
	};

	// Start fuse with appropriate options
	int status = fuse_main(argc - 2, argv + 2, &operations, disk);
	disk_close(disk);
	return status;
}

void usage(const char *program)
{
	fprintf(stderr, "%s (format|mount) <disk> [OPTIONS...]\n", program);
}
#define FUSE_USE_VERSION 26

#include "ffs_ops.h"
#include <ffs/ffs_disk.h>

int main(int argc, char** argv)
{
	struct fuse_operations operations = {
		.getattr = ffs_getattr,
		.open = ffs_open,
		.read = ffs_read,
		.readdir = ffs_readdir,
	};

	ffs_disk disk = ffs_disk_open(argv[argc - 1]);
	int result = fuse_main(argc - 1, argv, &operations, disk);
	ffs_disk_close(disk);

	return result;
}

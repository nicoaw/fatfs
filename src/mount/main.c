#define FUSE_USE_VERSION 26
#include <ffs/ffs_disk.h>
#include <fuse/fuse.h>

#define MOUNT_DISK ((ffs_disk) fuse_get_context()->private_data)

int main(int argc, char** argv)
{
	struct fuse_operations operations = {

	};

	ffs_disk disk = ffs_disk_open(argv[argc - 1], FFS_DISK_OPEN_RDWR);
	return fuse_main(argc - 1, argv, &operations, disk);
}

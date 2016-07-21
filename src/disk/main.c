#include <ffs/ffs_disk.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	ffs_disk disk = ffs_disk_open(argv[1], FFS_DISK_OPEN_RDWR);
	if(disk == NULL) {
		fprintf(stderr, "%s: %s: disk open failure\n", argv[0], argv[1]);
		return -1;
	}

	if(ffs_disk_init(disk, atoi(argv[2])) != 0) {
		fprintf(stderr, "%s: %s: disk init failure\n", argv[0], argv[1]);
		return -1;
	}

	return 0;
}

#include "block.h"
#include "cmd.h"
#include "disk.h"
#include "ops.h"
#include <stdio.h>
#include <syslog.h>

#define FATFS_VERSION "1.0.0"

#define FATFS_CEIL(a, b) (1 + ((a - 1) / b))

// Print usage
void usage(struct fatfs_params *params);

int cmd_format(struct fatfs_params *params)
{
	// Parameters must contain disk path, valid disk size, and valid block size
	if(!params->disk_path || !params->size || !params->block_size) {
		usage(params);
		return -1;
	}

	// TODO use different method to determine size to avoid overflow

	// Determine input size power
	int power = 0;
	switch(params->unit) {
		case 'K':
		case 'k':
			power = 1;
			break;
		case 'M':
		case 'm':
			power = 2;
			break;
		case 'G':
		case 'g':
			power = 3;
			break;
		case '\0':
			break;
		default:
			usage(params);
			return -1;
	}

	// Actual size in bytes of entire filesystem
	uintmax_t size = 1;
	for(int i = 0; i < power; ++i) {
		size *= 1024;
	}
	size *= params->size;

	// Setup superblock
	struct superblock sb;
	sb.magic = 0x2345beef;
	sb.block_size = params->block_size;
	sb.block_count = FATFS_CEIL(size, sb.block_size);
	const uint32_t fat_size = sb.block_count * sizeof(block);
	sb.fat_block_count = FATFS_CEIL(fat_size, sb.block_size);
	sb.root_block = sb.fat_block_count + 1;

	const uint32_t min_block_count = 2 + sb.fat_block_count;  // Min block count to support filesystem metadata
	if(sb.block_count < min_block_count) {
		fprintf(stderr, "filesystem too small: need at least %u bytes\n", min_block_count * sb.block_size);
		return -1;
	}

	disk d = disk_open(params->disk_path, true);
	if(!d) {
		return -1;
	}

	if(disk_format(d, sb) != 0) {
		return -1;
	}

	disk_close(d);
	return 0;
}

int cmd_help(struct fatfs_params *params)
{
	usage(params);
	return 0;
}

int cmd_mount(struct fatfs_params *params)
{
	// Parameters must contain disk path and mount path but not block size
	if(!params->disk_path || !params->mount_path) {
		usage(params);
		return -1;
	}

	struct fuse_operations operations = {
		.chmod = fatfs_chmod,
		.getattr = fatfs_getattr,
		.mkdir = fatfs_mkdir,
		.mknod = fatfs_mknod,
		.open = fatfs_open,
		.read = fatfs_read,
		.readdir = fatfs_readdir,
		.rename = fatfs_rename,
		.rmdir = fatfs_rmdir,
		.truncate = fatfs_truncate,
		.unlink = fatfs_unlink,
		.utimens = fatfs_utimens,
		.write = fatfs_write,
	};

	disk d = disk_open(params->disk_path, false);
	if(!d) {
		return -1;
	}

	int err = fuse_main(params->args.argc, params->args.argv, &operations, d);
	disk_close(d);
	return err;
}

int cmd_version(struct fatfs_params *params)
{
	fprintf(stderr, "fatfs version %s\n", FATFS_VERSION);
	fuse_opt_add_arg(&params->args, "--version");
	fuse_main(params->args.argc, params->args.argv, NULL, NULL);
	return 0;
}

void usage(struct fatfs_params *params)
{
	const char *program = params->args.argv[0];

	switch(params->base_cmd) {
		case CMD_FORMAT:
			fprintf(stderr,
					"usage: %s format [<options>] <file> <size>\n"
					"\n"
					"    <file> the disk file path\n"
					"    <size> size of disk in bytes, append (K,M,G) for (KiB,MiB,GiB) respectively\n"
					"\n"
					"    -b   --block_size=N set block size in bytes (1024)\n"
					"    -h   --help         print help\n"
					, program);
			break;
		case CMD_MOUNT:
			fprintf(stderr,
					"usage: %s mount [<options>] <file> <mountpoint>\n"
					"\n"
					"    <file>       the disk file path\n"
					"    <mountpoint> the mount point path\n"
					"\n"
					"general options:\n"
					"    -o opt,[opt...]	mount options\n"
					"    -h   --help		print help\n"
					"\n"
					, program);
			fuse_opt_add_arg(&params->args, "-ho");
			fuse_main(params->args.argc, params->args.argv, NULL, NULL);
			break;
		default:
			fprintf(stderr,
					"usage: %s [-V] [--version] [-h] [--help] <command> [<args>]\n"
					"\n"
					"commands:\n"
					"    format initialize a disk with empty fatfs filesystem\n"
					"    mount  mount a disk with a fatfs filesystem\n"
					, program);
			break;
	}
}

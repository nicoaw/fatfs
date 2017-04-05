#ifndef PARAM_H
#define PARAM_H

#define FUSE_USE_VERSION 26
#include <fuse.h>

#define FATFS_PARAMS_INIT(argc, argv) {FUSE_ARGS_INIT(argc, argv), NULL, 0, 0, 0, '\0', 0, NULL}

enum command
{
	CMD_FORMAT = 1,
	CMD_HELP,
	CMD_MOUNT,
	CMD_VERSION,
};

struct fatfs_params
{
	// General parameters
	struct fuse_args args;
	const char *disk_path;
	enum command base_cmd;
	enum command cmd;

	// Format parameters
	uint32_t size;
	char unit;
	uint32_t block_size;

	// Mount parameters
	const char *mount_path;
};

// Parse command-line arguments to setup fatfs parameters
// Returns non-zero on failure
int param_parse(struct fatfs_params *outparams, int argc, char **argv);

#endif

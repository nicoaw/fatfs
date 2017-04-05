#include "param.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FATFS_OPT(t, p, v) {t, offsetof(struct fatfs_params, p), v}

enum
{
	KEY_HELP,
	KEY_VERSION,
};

// Parse a single option
// Returns -1 on error, 0 if arg is to be discarded, 1 if arg should be kept
int opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs);

// Parse non-options
// Returns -1 on error, 0 if arg is to be discarded, 1 if arg should be kept
int parse_nonopt(struct fatfs_params *outparams, const char *arg);

int opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	struct fatfs_params *outparams = data;

	// Ignore the rest of the args when the command is help or version
	switch(outparams->cmd) {
		case CMD_HELP:
		case CMD_VERSION:
			return 0;
	}

	switch(key) {
		case KEY_HELP:
			outparams->cmd = CMD_HELP;
			return 0;
		case KEY_VERSION:
			outparams->cmd = CMD_VERSION;
			return 0;
		case FUSE_OPT_KEY_NONOPT:
			return parse_nonopt(outparams, arg);
		default:
			return 1;
	}
}

int param_parse(struct fatfs_params *outparams, int argc, char **argv)
{
	struct fuse_opt options[] = {
		// Format Options
		FATFS_OPT("-b %u", block_size, 1024),
		FATFS_OPT("--block_size=%u", block_size, 1024),

		// General options
		FUSE_OPT_KEY("-V", KEY_VERSION),
		FUSE_OPT_KEY("--version", KEY_VERSION),
		FUSE_OPT_KEY("-h", KEY_HELP),
		FUSE_OPT_KEY("--help", KEY_HELP),

		FUSE_OPT_END
	};

	struct fatfs_params params = FATFS_PARAMS_INIT(argc, argv);
	params.block_size = 1024; // block size defaults to 1024

	int err = fuse_opt_parse(&params.args, &params, options, &opt_proc);
	*outparams = params;
	return err;
}

int parse_nonopt(struct fatfs_params *outparams, const char *arg)
{
	if(!outparams->base_cmd) {
		if(strcmp(arg, "format") == 0) {
			outparams->base_cmd = CMD_FORMAT;
			outparams->cmd = CMD_FORMAT;
			return 0;
		} else if(strcmp(arg, "mount") == 0) {
			outparams->base_cmd = CMD_MOUNT;
			outparams->cmd = CMD_MOUNT;
			return 0;
		}
	} else if(!outparams->disk_path) {
		outparams->disk_path = arg;
		return 0;
	} else {
		switch(outparams->base_cmd) {
			case CMD_FORMAT:
				if(!outparams->size) {
					sscanf(arg, "%u%c", &outparams->size, &outparams->unit);
					return 0;
				}
				break;
			case CMD_MOUNT:
				if(!outparams->mount_path) {
					outparams->mount_path = arg;
					return 1;
				}
				break;
		}
	}

	return 1;
}

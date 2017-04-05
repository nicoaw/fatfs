#ifndef CMD_H
#define CMD_H

#include "param.h"

struct fatfs_params;

// Format a disk
// Returns non-zero on failure
int cmd_format(struct fatfs_params *params);

// Print help
// Returns zero
int cmd_help(struct fatfs_params *params);

// Mount a disk
// Returns non-zero on failure
int cmd_mount(struct fatfs_params *params);

// Print program version
// Returns zero
int cmd_version(struct fatfs_params *params);

#endif

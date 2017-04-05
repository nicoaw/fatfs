#include "cmd.h"
#include "param.h"
#include <syslog.h>

int main(int argc, char **argv) {
	setlogmask(LOG_UPTO(LOG_INFO));
	openlog("fatfs", LOG_CONS | LOG_PID, LOG_USER);

	struct fatfs_params params;
	param_parse(&params, argc, argv);

	switch(params.cmd) {
		case CMD_FORMAT:
			return cmd_format(&params);
		case CMD_HELP:
			return cmd_help(&params);
		case CMD_MOUNT:
			return cmd_mount(&params);
		case CMD_VERSION:
			return cmd_version(&params);
		default:
			cmd_help(&params);
			return -1;
	}
}

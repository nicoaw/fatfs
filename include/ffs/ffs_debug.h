#ifndef FFS_DEBUG_H
#define FFS_DEBUG_H

#define FFS_ERR_LEVEL 1
#define FFS_LOG_LEVEL 2

#ifdef FFS_DEBUG

#include <stdio.h> 

#define FFS_ERR(level, msg, ...) {																				\
	if(level >= FFS_ERR_LEVEL) {																				\
		fprintf(stdout, "ERROR[%d]: %s:%d %s: " msg "\n", level, __FILE__, __LINE__, __func__, ##__VA_ARGS__);	\
	}																											\
}

#define FFS_LOG(level, msg, ...) {																				\
	if(level >= FFS_LOG_LEVEL) {																				\
		fprintf(stdout, "INFO[%d]: %s:%d %s: " msg "\n", level, __FILE__, __LINE__, __func__, ##__VA_ARGS__);	\
	}																											\
}


#else

#define FFS_ERR(level, msg, ...)
#define FFS_LOG(level, msg, ...)

#endif

#endif

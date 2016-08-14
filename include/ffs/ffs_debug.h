#ifndef FFS_DEBUG_H
#define FFS_DEBUG_H

#ifdef FFS_DEBUG

#include <stdio.h> 

#define FFS_ERR(msg, ...) fprintf(stdout, "ERROR: %s:%d %s: " msg "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define FFS_LOG(msg, ...) fprintf(stdout, "INFO: %s:%d %s: " msg "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)

#else

#define FFS_ERR(msg, ...)
#define FFS_LOG(msg, ...)

#endif

#endif

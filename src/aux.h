#ifndef AUX_H
#define AUX_H

#define ERR_LEVEL 1
#define LOG_LEVEL 2

#include <stdio.h> 
#include <errno.h> 

#define ERR(level, msg, ...) {																				\
	if(level >= ERR_LEVEL) {																				\
		fprintf(stdout, "ERROR[%d]: %s:%d %s: " msg "\n", level, __FILE__, __LINE__, __func__, ##__VA_ARGS__);	\
	}																											\
}

#define LOG(level, msg, ...) {																				\
	if(level >= LOG_LEVEL) {																				\
		fprintf(stdout, "INFO[%d]: %s:%d %s: " msg "\n", level, __FILE__, __LINE__, __func__, ##__VA_ARGS__);	\
	}																											\
}

#include <stdint.h> 

// Returns larger 32-bit unsigned integer of a and b
uint32_t max(uint32_t a, uint32_t b);

// Returns smaller 32-bit unsigned integer of a and b
uint32_t min(uint32_t a, uint32_t b);

#endif

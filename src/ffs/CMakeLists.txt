file(GLOB_RECURSE sources *.c *.h)

add_library(ffs ${sources})
set_target_properties(ffs PROPERTIES
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
	)

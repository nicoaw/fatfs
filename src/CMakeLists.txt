file(GLOB_RECURSE sources *.c *.h)
set(libraries
	fuse
	rt)

add_executable(fatfs ${sources})
target_link_libraries(fatfs ${libraries})
set_target_properties(fatfs PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	COMPILE_FLAGS "-D_FILE_OFFSET_BITS=64 -pthread"
	)

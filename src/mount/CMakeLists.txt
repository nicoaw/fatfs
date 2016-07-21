file(GLOB_RECURSE sources *.c *.h)
set(libraries
	ffs
	fuse
	rt)

add_executable(ffs-mount ${sources})
target_link_libraries(ffs-mount ${libraries})
set_target_properties(ffs-mount PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	COMPILE_FLAGS "-D_FILE_OFFSET_BITS=64 -pthread"
	)

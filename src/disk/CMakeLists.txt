file(GLOB_RECURSE sources *.c *.h)
set(libraries ffs)

add_executable(ffs-disk ${sources})
target_link_libraries(ffs-disk ${libraries})
set_target_properties(ffs-disk PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	)

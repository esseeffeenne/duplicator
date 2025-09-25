cmake_minimum_required(VERSION 4.1.0)
project(duplicator VERSION 0.1.0)

configure_file(src/config.h.in config.h)
add_executable(duplicator src/main.c)

include_directories(${CMAKE_BINARY_DIR})
target_link_libraries(duplicator -lc)

set(CMAKE_INSTALL_PREFIX /usr/local)
install(TARGETS duplicator DESTINATION bin)

if(DEBUG)
	target_compile_options(duplicator PUBLIC -g PUBLIC -Wall PUBLIC -Wextra PUBLIC -O0)
else()
	target_compile_options(duplicator PUBLIC -O2)
endif()

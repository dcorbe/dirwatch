cmake_minimum_required(VERSION 3.15)
project(dirwatch)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)

add_executable(dirwatch main.c timer.c)

# Find POPT
find_package(POPT REQUIRED)
if (POPT_FOUND)
    include_directories(${POPT_INCLUDE_DIRS})
    target_link_libraries (dirwatch ${POPT_LIBRARIES})
endif (POPT_FOUND)
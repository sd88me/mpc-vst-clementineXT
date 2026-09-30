# Dev-only firmware oracle (never shipped). Builds against a gearmulator checkout:
#   cmake -S tools/oracle -B ~/oracle/tool-build -G Ninja -DGEARMULATOR_DIR=~/oracle/gearmulator -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.10)
project(clementine_oracle CXX)
set(GEARMULATOR_DIR "" CACHE PATH "gearmulator checkout")
set(gearmulator_BUILD_JUCEPLUGIN OFF CACHE BOOL "" FORCE)
set(gearmulator_BUILD_JUCEPLUGIN_CLAP OFF CACHE BOOL "" FORCE)
add_subdirectory(${GEARMULATOR_DIR} gearmulator EXCLUDE_FROM_ALL)
add_executable(oracle oracle.cpp)
target_link_libraries(oracle PUBLIC xtLib)

cmake_minimum_required(VERSION 3.20)

project(PulseNet VERSION 0.1.0 LANGUAGES C CXX)

# Must be at the top level. ctest only walks tests registered under the
# build root, and enable_testing() in a subdirectory is not enough.
include(CTest)

add_subdirectory(backend)

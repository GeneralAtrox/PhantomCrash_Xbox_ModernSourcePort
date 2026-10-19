cmake_minimum_required(VERSION 3.25)

project(phantomcrash-port LANGUAGES CXX)

include(CTest)

option(PHANTOMCRASH_BUILD_DESKTOP "Build the SDL3 desktop presentation host" ON)

add_subdirectory(port)

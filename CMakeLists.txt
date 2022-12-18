cmake_minimum_required(VERSION 3.1)

project(splitter)

find_package(PkgConfig)

add_compile_options(-std=c++17)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(splitter STATIC 
  Splitter.cpp
)
target_link_libraries(splitter PUBLIC)

include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)
enable_testing()

add_executable(
  Tests
  ClientsTest.cpp
  SingleClientTests.cpp
  SlowClientTest.cpp
)

target_link_libraries(
  Tests
  GTest::gtest_main
  splitter
)

gtest_discover_tests(Tests)



enable_testing()
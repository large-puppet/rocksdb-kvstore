cmake_minimum_required(VERSION 3.14)
project(rocksdb_kvstore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Find RocksDB
# ---------------------------------------------------------------------------
find_library(ROCKSDB_LIB rocksdb REQUIRED)
find_path(ROCKSDB_INCLUDE_DIR rocksdb/db.h REQUIRED)

# ---------------------------------------------------------------------------
# Library: kvstore
# ---------------------------------------------------------------------------
add_library(kvstore STATIC src/kvstore.cpp)
target_include_directories(kvstore PUBLIC include ${ROCKSDB_INCLUDE_DIR})
target_link_libraries(kvstore PUBLIC ${ROCKSDB_LIB} pthread)

# ---------------------------------------------------------------------------
# Demo executable
# ---------------------------------------------------------------------------
add_executable(kvstore_demo src/main.cpp)
target_link_libraries(kvstore_demo PRIVATE kvstore)

# ---------------------------------------------------------------------------
# Tests (using GTest)
# ---------------------------------------------------------------------------
find_package(GTest QUIET)

if(GTest_FOUND OR TARGET GTest::gtest)
    add_executable(kvstore_test test/test_kvstore.cpp)
    target_link_libraries(kvstore_test PRIVATE kvstore GTest::gtest)
    enable_testing()
    add_test(NAME kvstore_test COMMAND kvstore_test)
else()
    # Fallback: try pkg-config / manual detection
    find_library(GTEST_LIB gtest)
    find_path(GTEST_INCLUDE_DIR gtest/gtest.h)
    if(GTEST_LIB AND GTEST_INCLUDE_DIR)
        add_executable(kvstore_test test/test_kvstore.cpp)
        target_include_directories(kvstore_test PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(kvstore_test PRIVATE kvstore ${GTEST_LIB} pthread)
        enable_testing()
        add_test(NAME kvstore_test COMMAND kvstore_test)
    else()
        message(STATUS "GTest not found – skipping test target")
    endif()
endif()

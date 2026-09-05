cmake_minimum_required(VERSION 3.16)

project(Ncore LANGUAGES C)

find_package(Threads REQUIRED)

add_library(ncore
    src/nfifo.c
    src/nthread_message.c
    src/nlog.c
)
add_library(Ncore::ncore ALIAS ncore)
target_compile_features(ncore PRIVATE c_std_11)

target_include_directories(ncore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(ncore PRIVATE Threads::Threads)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(NCORE_BUILD_TESTS_DEFAULT ON)
else()
    set(NCORE_BUILD_TESTS_DEFAULT OFF)
endif()

option(NCORE_BUILD_TESTS "Build Ncore tests" ${NCORE_BUILD_TESTS_DEFAULT})

if(NCORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

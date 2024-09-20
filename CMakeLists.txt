cmake_minimum_required(VERSION 3.18)

project(httpserver)

set(CMAKE_CXX_STANDARD 23)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

aux_source_directory(./src SRC_LIST)

add_executable(server ${SRC_LIST})

find_package(Threads REQUIRED)
target_link_libraries(main PUBLIC Threads::Threads)

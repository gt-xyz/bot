cmake_minimum_required(VERSION 3.22)
project(bee CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(libbee STATIC
    src/libbee/command.cpp
    src/libbee/config.cpp
    src/libbee/render.cpp
    src/libbee/project.cpp
)
set_target_properties(libbee PROPERTIES OUTPUT_NAME bee)
target_include_directories(libbee PUBLIC src)
target_compile_options(libbee PRIVATE -Wall -Wextra -Wpedantic)

add_executable(bee src/bee/main.cpp)
target_link_libraries(bee PRIVATE libbee)
target_compile_options(bee PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_subdirectory(test)

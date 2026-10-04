cmake_minimum_required(VERSION 3.22)
project(bot CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(libbot STATIC
    src/libbot/command.cpp
    src/libbot/config.cpp
    src/libbot/render.cpp
    src/libbot/project.cpp
    src/libbot/session.cpp
    src/libbot/web.cpp
)
set_target_properties(libbot PROPERTIES OUTPUT_NAME bot)
target_include_directories(libbot PUBLIC src)
target_compile_options(libbot PRIVATE -Wall -Wextra -Wpedantic)
# Where an installed bot finds its templates and image definitions unless the
# config says otherwise.
target_compile_definitions(libbot PRIVATE BOT_SHARE="${CMAKE_INSTALL_FULL_DATADIR}/bot")

add_executable(bot src/bot/main.cpp)
target_link_libraries(bot PRIVATE libbot)
target_compile_options(bot PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS bot)
install(DIRECTORY templates images DESTINATION ${CMAKE_INSTALL_DATADIR}/bot)

enable_testing()
add_subdirectory(test)

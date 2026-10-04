add_executable(unit unit.cpp)
target_link_libraries(unit PRIVATE libbot)
target_compile_options(unit PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME unit COMMAND unit)

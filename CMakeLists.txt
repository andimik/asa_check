cmake_minimum_required(VERSION 3.5)

project(asa_check LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(asa_check src/main.cpp)

install(TARGETS asa_check
    RUNTIME DESTINATION bin
)

target_compile_options(asa_check PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

option(ASA_BUILD_TESTS "Build DAB time regression tests" OFF)
if(ASA_BUILD_TESTS)
    enable_testing()
    add_executable(dab_time_test tests/dab_time.cpp)
    add_test(NAME dab_time COMMAND dab_time_test)
endif()

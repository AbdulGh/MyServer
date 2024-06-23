cmake_minimum_required(VERSION 3.12)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(server)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -Wfatal-errors)

add_library(mainlib src/server/parseHTTP.cpp src/server/server.cpp src/server/dispatch.cpp src/server/client.cpp)
target_include_directories(mainlib PUBLIC include)

add_executable(main main.cpp)
target_link_libraries(main PUBLIC mainlib)

enable_testing()

add_executable(parseHTTP test/parseHTTP.cpp)
target_link_libraries(parseHTTP PUBLIC mainlib)
add_test(NAME parseHTTP COMMAND parseHTTP)

add_executable(json test/json.cpp)
target_link_libraries(json PUBLIC mainlib)
add_test(NAME json COMMAND json)

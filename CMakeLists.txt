cmake_minimum_required(VERSION 3.12)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(server)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_BUILD_TYPE Debug)

add_library(mainlib src/parseHTTP.cpp src/server.cpp src/concurrentQueue.cpp src/dispatch.cpp src/client.cpp)
target_include_directories(mainlib PUBLIC include)

add_executable(main main.cpp)
target_link_libraries(main PUBLIC mainlib)

enable_testing()
add_executable(parseHTTP test/parseHTTP.cpp)
target_link_libraries(parseHTTP PUBLIC mainlib)
add_test(NAME parseHTTP COMMAND parseHTTP)

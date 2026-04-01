cmake_minimum_required(VERSION 3.15)

project(OpenWebUIDashboard C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(OpenWebUIDashboard WIN32 src/main.c)
target_sources(OpenWebUIDashboard PRIVATE app.rc)
target_link_libraries(OpenWebUIDashboard PRIVATE ws2_32 iphlpapi)

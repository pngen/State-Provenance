cmake_minimum_required(VERSION 3.21)
project(stateprovenance_consumer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(StateProvenance CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE StateProvenance::stateprovenance)
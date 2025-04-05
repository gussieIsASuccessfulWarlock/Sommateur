cmake_minimum_required(VERSION 3.10)
project(Sommateur LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(sommateur sommateur.cpp)
target_link_libraries(sommateur PRIVATE curl pthread)

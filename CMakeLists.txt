cmake_minimum_required(VERSION 3.10)
project(Sommateur LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(CURL REQUIRED)

add_executable(sommateur sommateur.cpp)

if(WIN32)
  target_link_libraries(sommateur PRIVATE CURL::libcurl)
else()
  target_link_libraries(sommateur PRIVATE CURL::libcurl pthread)
endif()

cmake_minimum_required(VERSION 4.3)
project(soapp)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_CXX_STANDARD 23)

find_package(LibXml2 REQUIRED)
find_package(fmt)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

add_executable(soapp main.cpp)
target_link_libraries(soapp fmt::fmt OpenSSL::SSL CURL::libcurl LibXml2::LibXml2)
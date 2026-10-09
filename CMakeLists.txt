cmake_minimum_required(VERSION 4.3)
project(soapp)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_CXX_STANDARD 23)

find_package(LibXml2 REQUIRED)
find_package(fmt)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

add_library(soapp
    cpp_generator.cpp
    cpp_names.cpp
    element_table.cpp
    io.cpp
    type_table.cpp
    wsdl.cpp
    xml.cpp
    xsd_types.cpp
)
target_link_libraries(soapp PUBLIC fmt::fmt OpenSSL::SSL CURL::libcurl LibXml2::LibXml2)
target_include_directories(soapp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(wsdl2pp main.cpp)
target_link_libraries(wsdl2pp PRIVATE soapp)
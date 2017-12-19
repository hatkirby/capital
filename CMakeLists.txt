cmake_minimum_required (VERSION 3.1)
project (capital)

add_subdirectory(vendor/libtwittercpp)
add_subdirectory(vendor/verbly)

find_package(PkgConfig)
pkg_check_modules(GraphicsMagick GraphicsMagick++ REQUIRED)
pkg_check_modules(yaml-cpp yaml-cpp REQUIRED)

include_directories(
  ${GraphicsMagick_INCLUDE_DIRS}
  vendor/verbly/lib
  vendor/libtwittercpp/src
  ${yaml-cpp_INCLUDE_DIRS}
  vendor/libtwittercpp/vendor/curlcpp/include)

link_directories(${GraphicsMagick_LIBRARY_DIRS})

add_executable(capital capital.cpp main.cpp)
set_property(TARGET capital PROPERTY CXX_STANDARD 11)
set_property(TARGET capital PROPERTY CXX_STANDARD_REQUIRED ON)
target_link_libraries(capital ${GraphicsMagick_LIBRARIES} verbly ${yaml-cpp_LIBRARIES} twitter++)

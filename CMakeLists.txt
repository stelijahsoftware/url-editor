cmake_minimum_required(VERSION 3.16)

project(urleditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM3 REQUIRED gtkmm-3.0)
pkg_check_modules(CURL REQUIRED libcurl)

add_executable(urleditor url-editor.cpp)

target_include_directories(urleditor PRIVATE ${GTKMM3_INCLUDE_DIRS})
target_compile_options(urleditor PRIVATE ${GTKMM3_CFLAGS_OTHER})
target_link_directories(urleditor PRIVATE ${GTKMM3_LIBRARY_DIRS})
target_link_libraries(urleditor ${GTKMM3_LIBRARIES} ${CURL_LIBRARIES})

cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FindPkgConfig)
set(CMAKE_FIND_FRAMEWORK LAST)

project(InotifyWatcher)

add_executable(inotify_watcher src/inotify_service.cpp src/inotify_path.cpp src/inotify_tools.cpp)

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)

find_package(Boost 1.75 REQUIRED system json REQUIRED)
find_package(spdlog CONFIG REQUIRED)
pkg_check_modules(SYSTEMD libsystemd)

target_include_directories(inotify_watcher PRIVATE ${Boost_INCLUDE_DIRS} src)
target_link_libraries(inotify_watcher ${Boost_LIBRARIES} ${SYSTEMD_LIBRARIES} stdc++fs spdlog::spdlog pthread)

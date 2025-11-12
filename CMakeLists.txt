cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FindPkgConfig)
set(CMAKE_FIND_FRAMEWORK LAST)

project(InotifyWatcher)

add_executable(inotify_watcher json/swe_json.cpp json/jsmn/jsmn.cpp src/inotify_path.cpp src/inotify_tools.cpp src/inotify_service.cpp)

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)

find_package(Boost REQUIRED)
#find_package(spdlog CONFIG REQUIRED)
pkg_check_modules(SYSTEMD libsystemd)

target_compile_definitions(inotify_watcher PUBLIC SPDLOG_ENABLE_SYSLOG SPDLOG_FMT_EXTERNAL)
target_include_directories(inotify_watcher PRIVATE ${Boost_INCLUDE_DIRS} src json)
target_link_libraries(inotify_watcher ${Boost_LIBRARIES} ${SYSTEMD_LIBRARIES} stdc++fs pthread)

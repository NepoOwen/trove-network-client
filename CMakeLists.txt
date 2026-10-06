cmake_minimum_required(VERSION 3.16)
project(network-nepofied CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Binary lands directly in the build dir (e.g. build/network-nepofied), with
# CMake's own object/dependency files in build/CMakeFiles/ alongside it --
# matches the .vcxproj putting everything under build/.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(MSVC)
    add_compile_options(/W3)
endif()

add_executable(network-nepofied
    src/main.cpp
    src/app/session.cpp
    src/app/selftest.cpp
    src/network/connection.cpp
    src/network/log.cpp
    src/login/login.cpp
    src/auth/auth_handler.cpp
    src/auth/auth_messages.cpp
    src/world/world_handler.cpp
    src/world/world_messages.cpp
    src/world/post_login_handler.cpp
    src/api/api_handler.cpp
    src/api/api_messages.cpp
    src/api/hotkeys.cpp
    src/utils/net/net.cpp
    src/utils/crypto/crypto.cpp
    src/utils/frame/frame.cpp
    src/utils/zlib/zlib.cpp
)

target_include_directories(network-nepofied PRIVATE src)

find_package(Threads REQUIRED)

if(WIN32)
    target_link_libraries(network-nepofied PRIVATE ws2_32 winhttp user32)
else()
    find_package(CURL REQUIRED)
    target_link_libraries(network-nepofied PRIVATE CURL::libcurl Threads::Threads)
endif()

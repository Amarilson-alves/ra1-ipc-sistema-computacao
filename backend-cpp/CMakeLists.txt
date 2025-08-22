cmake_minimum_required(VERSION 3.20)
project(ra1_ipc_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.cxx"
)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(ra1_ipc_backend ${SOURCES})

# Windows-specific libs for named pipes / shared memory if needed later
if (WIN32)
    target_link_libraries(ra1_ipc_backend PRIVATE ws2_32)
endif()
cmake_minimum_required(VERSION 3.16)

project(claude-sdbus
    VERSION 1.0.0
    DESCRIPTION "Bluetooth Low Energy Manager using sdbus-c++"
    LANGUAGES CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# suppress policy warnings on newer versions of cmake
cmake_policy(SET CMP0042 NEW)
cmake_policy(SET CMP0053 NEW)
cmake_policy(SET CMP0057 NEW)

# Export compile commands for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Build options
option(BUILD_WITH_WARNINGS "Enable compiler warnings" ON)
option(BUILD_WITH_SANITIZERS "Enable sanitizers (Debug only)" OFF)

# Compiler warnings
if(BUILD_WITH_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wformat=2
        )
    elseif(MSVC)
        add_compile_options(/W4)
    endif()
endif()

# Sanitizers for debug builds
if(BUILD_WITH_SANITIZERS AND CMAKE_BUILD_TYPE MATCHES Debug)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(
            -fsanitize=address
            -fsanitize=undefined
            -fno-omit-frame-pointer
        )
        add_link_options(
            -fsanitize=address
            -fsanitize=undefined
        )
    endif()
endif()

# Find required packages
find_package(PkgConfig REQUIRED)

# Find sdbus-c++
pkg_check_modules(SDBUS_CPP REQUIRED IMPORTED_TARGET sdbus-c++)

# Find threading library
find_package(Threads REQUIRED)

# Create executable
add_executable(${PROJECT_NAME}
    src/main.cpp
)

# Set target properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${PROJECT_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        PkgConfig::SDBUS_CPP
        Threads::Threads
)

# Include directories
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Compiler-specific optimizations
if(CMAKE_BUILD_TYPE MATCHES Release)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${PROJECT_NAME} PRIVATE
            -O3
            -march=native
            -flto
        )
        target_link_options(${PROJECT_NAME} PRIVATE
            -flto
        )
    endif()
endif()

# Installation rules
include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT runtime
)

# Print build information
message(STATUS "")
message(STATUS "==================== Build Configuration ====================")
message(STATUS "Project:              ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Build type:           ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard:         C++${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:             ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Install prefix:       ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Options:")
message(STATUS "  Warnings:           ${BUILD_WITH_WARNINGS}")
message(STATUS "  Sanitizers:         ${BUILD_WITH_SANITIZERS}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  sdbus-c++:          ${SDBUS_CPP_VERSION}")
message(STATUS "  sdbus-c++ libs:     ${SDBUS_CPP_LIBRARIES}")
message(STATUS "  sdbus-c++ include:  ${SDBUS_CPP_INCLUDE_DIRS}")
message(STATUS "=============================================================")
message(STATUS "")
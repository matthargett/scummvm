# CMakeLists.txt for Playdate ScummVM build
cmake_minimum_required(VERSION 3.14)

# Set Playdate SDK path
if (NOT DEFINED ENV{PLAYDATE_SDK_PATH})
    message(FATAL_ERROR "PLAYDATE_SDK_PATH environment variable must be set")
endif()

set(PLAYDATE_SDK_PATH $ENV{PLAYDATE_SDK_PATH})
set(CMAKE_TOOLCHAIN_FILE ${PLAYDATE_SDK_PATH}/C_API/buildsupport/arm.cmake)

# Project name
project(scummvm_playdate C CXX ASM)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Playdate-specific defines
add_definitions(
    -D__PLAYDATE__
    -DREDUCE_MEMORY_USAGE
    -DDISABLE_FANCY_THEMES
    -DDISABLE_COMMAND_LINE
)

# Include Playdate SDK
include(${PLAYDATE_SDK_PATH}/C_API/buildsupport/playdate.cmake)

# ScummVM source files
set(SCUMMVM_PLAYDATE_SOURCES
    main.cpp
    osystem_playdate.cpp
    playdate-graphics.cpp
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/backends/platform/playdate
    ${CMAKE_SOURCE_DIR}/engines
    ${PLAYDATE_SDK_PATH}/C_API
)

# Create executable
add_executable(${PROJECT_NAME} ${SCUMMVM_PLAYDATE_SOURCES})

# Link Playdate SDK
target_link_libraries(${PROJECT_NAME} ${PLAYDATE_SDK_PATH}/C_API/buildsupport/libpd.a)

# Create PDX
create_pdx(${PROJECT_NAME})

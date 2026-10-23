cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME bootloader)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME} VERSION 0.0.2)
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

set(BOARD_VERSION "" CACHE STRING "Board version.")
if(BOARD_VERSION STREQUAL "")
    message(FATAL_ERROR "Provide -DBOARD_VERSION=<version>, for example 0x0200")
endif()
configure_file(Core/Src/header.c.in header.c @ONLY)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/header.c)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE BOARD_VERSION=${BOARD_VERSION})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

set(OPENBL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/stm32-mw-openbl)
target_include_directories(stm32cubemx INTERFACE
    ${OPENBL_DIR}/Core
    ${OPENBL_DIR}/Modules/Mem
    ${OPENBL_DIR}/Modules/USB
)
target_compile_definitions(stm32cubemx INTERFACE
    HAL_TIM_MODULE_ENABLED
    USBD_DFU_VENDOR_EXIT_ENABLED=1
)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/usb_hub.c
    Core/Src/bitbang_i2c.c
    Core/Src/ws2812.c
    Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_tim.c
    Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_tim_ex.c
    Core/Src/bootloader.c
    Core/Src/app_openbootloader.c
    Core/Src/common_interface.c
    Core/Src/flash_interface.c
    Core/Src/usb_interface.c
    Core/Src/usbd_dfu_if.c
    ${OPENBL_DIR}/Core/openbl_core.c
    ${OPENBL_DIR}/Modules/Mem/openbl_mem.c
    ${OPENBL_DIR}/Modules/USB/openbl_usb_cmd.c
)
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/STM32G0B1xx_FLASH.ld
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

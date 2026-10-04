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
set(CMAKE_PROJECT_NAME magi)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

set(ATCA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ATCA_NO_HEAP ON CACHE BOOL "" FORCE)
set(ATCA_HAL_I2C ON CACHE BOOL "" FORCE)
set(ATCA_ATECC608_SUPPORT ON CACHE BOOL "" FORCE)
foreach(device ATSHA204A ATSHA206A ATECC108A ATECC508A ECC204 ECC206 TA010 SHA104 SHA105)
    set(ATCA_${device}_SUPPORT OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(Drivers/cryptoauthlib/lib)
target_include_directories(cryptoauth PUBLIC
    ${PROJECT_SOURCE_DIR}/Drivers/cryptoauthlib/lib
    ${PROJECT_BINARY_DIR}/Drivers/cryptoauthlib/lib
)
target_link_libraries(cryptoauth stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
set(RTL8367C_DRIVER_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/OpenWRT/target/linux/mediatek/files/drivers/net/phy/rtk/rtl8367c
)
set(RTL8367C_DRIVER_SOURCES
    ${RTL8367C_DRIVER_DIR}/port.c
    ${RTL8367C_DRIVER_DIR}/rate.c
    ${RTL8367C_DRIVER_DIR}/rtk_switch.c
    ${RTL8367C_DRIVER_DIR}/rtl8367c_asicdrv.c
    ${RTL8367C_DRIVER_DIR}/rtl8367c_asicdrv_green.c
    ${RTL8367C_DRIVER_DIR}/rtl8367c_asicdrv_inbwctrl.c
    ${RTL8367C_DRIVER_DIR}/rtl8367c_asicdrv_lut.c
    ${RTL8367C_DRIVER_DIR}/rtl8367c_asicdrv_meter.c
    ${RTL8367C_DRIVER_DIR}/rtl8367c_asicdrv_mirror.c
    ${RTL8367C_DRIVER_DIR}/rtl8367c_asicdrv_misc.c
    ${RTL8367C_DRIVER_DIR}/rtl8367c_asicdrv_phy.c
    ${RTL8367C_DRIVER_DIR}/rtl8367c_asicdrv_port.c
    ${RTL8367C_DRIVER_DIR}/rtl8367c_asicdrv_portIsolation.c
    ${RTL8367C_DRIVER_DIR}/rtl8367c_asicdrv_rma.c
    ${RTL8367C_DRIVER_DIR}/rtl8367c_asicdrv_scheduling.c
)
set_source_files_properties(${RTL8367C_DRIVER_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wno-incompatible-pointer-types"
)

set(LWIP_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/lwIP
)

include(${LWIP_DIR}/src/Filelists.cmake)

add_library(lwip STATIC)

target_sources(lwip PRIVATE
        ${lwipcore_SRCS}
        ${lwipcore4_SRCS}
        ${lwipcore6_SRCS}
        ${lwipapi_SRCS}
        ${lwipmdns_SRCS}

        ${LWIP_DIR}/src/netif/ethernet.c
        ${LWIP_DIR}/contrib/ports/freertos/sys_arch.c
)

target_include_directories(lwip PUBLIC
        ${LWIP_DIR}/src/include
        ${LWIP_DIR}/contrib/ports/freertos/include
        ${PROJECT_SOURCE_DIR}/Core/Inc
)
target_link_libraries(lwip PUBLIC stm32cubemx)

set(WOLFSSL_DIR ${PROJECT_SOURCE_DIR}/Middlewares/Third_Party/wolfSSL)
set(WOLFSSH_DIR ${PROJECT_SOURCE_DIR}/Middlewares/Third_Party/wolfSSH)

find_program(MAGI_PATCH patch REQUIRED)
function(magi_patch_source output source patch_file)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}" "${patch_file}")
    configure_file("${source}" "${output}" COPYONLY)
    execute_process(COMMAND "${MAGI_PATCH}" --batch --silent "${output}" "${patch_file}"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot apply ${patch_file}: ${error}")
    endif()
endfunction()

set(MAGI_ATMEL_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/ports/atmel.c)
set(MAGI_ECC_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/ports/ecc.c)
set(MAGI_WOLFSSH_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/ports/internal.c)
magi_patch_source("${MAGI_ATMEL_SOURCE}"
    "${WOLFSSL_DIR}/wolfcrypt/src/port/atmel/atmel.c"
    "${PROJECT_SOURCE_DIR}/cmake/patches/wolfssl-atecc-tempkey.patch")
magi_patch_source("${MAGI_ECC_SOURCE}"
    "${WOLFSSL_DIR}/wolfcrypt/src/ecc.c"
    "${PROJECT_SOURCE_DIR}/cmake/patches/wolfssl-atecc-math-init.patch")
magi_patch_source("${MAGI_WOLFSSH_SOURCE}"
    "${WOLFSSH_DIR}/src/internal.c"
    "${PROJECT_SOURCE_DIR}/cmake/patches/wolfssh-atecc-hostkey.patch")

set(WOLFSSL_USER_SETTINGS ON)
set(WOLFSSL_CRYPT_ONLY ON)
set(WOLFSSL_INLINE ON)
set(WOLFSSL_CRYPTOAUTHLIB ON)
include(${WOLFSSL_DIR}/cmake/functions.cmake)
generate_build_flags()
generate_lib_src_list("")
list(REMOVE_ITEM LIB_SOURCES wolfcrypt/src/ecc.c wolfcrypt/src/port/atmel/atmel.c)
list(TRANSFORM LIB_SOURCES PREPEND "${WOLFSSL_DIR}/")
add_library(wolfssl STATIC ${LIB_SOURCES} ${MAGI_ATMEL_SOURCE} ${MAGI_ECC_SOURCE})
target_include_directories(wolfssl PUBLIC ${WOLFSSL_DIR} ${PROJECT_SOURCE_DIR}/Core/Inc)
target_compile_definitions(wolfssl PUBLIC WOLFSSL_USER_SETTINGS)
target_link_libraries(wolfssl PUBLIC stm32cubemx cryptoauth)

add_library(wolfssh STATIC
    ${WOLFSSH_DIR}/src/ssh.c
    ${MAGI_WOLFSSH_SOURCE}
    ${WOLFSSH_DIR}/src/io.c
    ${WOLFSSH_DIR}/src/log.c
    ${WOLFSSH_DIR}/src/port.c
)
target_include_directories(wolfssh PUBLIC ${WOLFSSH_DIR})
target_link_libraries(wolfssh PUBLIC wolfssl)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Core/Src/adc.c
    Core/Src/bitbang_i2c.c
    Core/Src/cdc.c
    Core/Src/cmd.c
    Core/Src/console.c
    Core/Src/crypto.c
    Core/Src/cryptoauth_hal.c
    Core/Src/log.c
    Core/Src/network.c
    Core/Src/ssh_crypto.c
    Core/Src/ssh_server.c
    Core/Src/switch.c
    Core/Src/tcpm.c
    Core/Src/tiny_printf.c
    Core/Src/unit.c
    Core/Src/usb.c
    Core/Src/usb_hub.c
    Core/Src/ws2812.c
    Core/Src/xpander.c
    Drivers/ch390-lwip/ch390.c
    Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_adc.c
    Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_adc_ex.c
    Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_spi.c
    Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_spi_ex.c
    Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_uart.c
    Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_uart_ex.c
    ${RTL8367C_DRIVER_SOURCES}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${RTL8367C_DRIVER_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/ch390-lwip
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
    lwip
    cryptoauth
    wolfssh

    # Add user defined libraries
)

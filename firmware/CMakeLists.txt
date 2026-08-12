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

set(LWIP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/lwIP)
set(LWIP_SOURCES
    ${LWIP_DIR}/src/core/altcp.c
    ${LWIP_DIR}/src/core/altcp_alloc.c
    ${LWIP_DIR}/src/core/altcp_tcp.c
    ${LWIP_DIR}/src/core/def.c
    ${LWIP_DIR}/src/core/dns.c
    ${LWIP_DIR}/src/core/inet_chksum.c
    ${LWIP_DIR}/src/core/init.c
    ${LWIP_DIR}/src/core/ip.c
    ${LWIP_DIR}/src/core/mem.c
    ${LWIP_DIR}/src/core/memp.c
    ${LWIP_DIR}/src/core/netif.c
    ${LWIP_DIR}/src/core/pbuf.c
    ${LWIP_DIR}/src/core/raw.c
    ${LWIP_DIR}/src/core/stats.c
    ${LWIP_DIR}/src/core/sys.c
    ${LWIP_DIR}/src/core/tcp.c
    ${LWIP_DIR}/src/core/tcp_in.c
    ${LWIP_DIR}/src/core/tcp_out.c
    ${LWIP_DIR}/src/core/timeouts.c
    ${LWIP_DIR}/src/core/udp.c
    ${LWIP_DIR}/src/core/ipv4/acd.c
    ${LWIP_DIR}/src/core/ipv4/autoip.c
    ${LWIP_DIR}/src/core/ipv4/dhcp.c
    ${LWIP_DIR}/src/core/ipv4/etharp.c
    ${LWIP_DIR}/src/core/ipv4/icmp.c
    ${LWIP_DIR}/src/core/ipv4/igmp.c
    ${LWIP_DIR}/src/core/ipv4/ip4.c
    ${LWIP_DIR}/src/core/ipv4/ip4_addr.c
    ${LWIP_DIR}/src/core/ipv4/ip4_frag.c
    ${LWIP_DIR}/src/api/api_lib.c
    ${LWIP_DIR}/src/api/api_msg.c
    ${LWIP_DIR}/src/api/err.c
    ${LWIP_DIR}/src/api/if_api.c
    ${LWIP_DIR}/src/api/netbuf.c
    ${LWIP_DIR}/src/api/netdb.c
    ${LWIP_DIR}/src/api/netifapi.c
    ${LWIP_DIR}/src/api/sockets.c
    ${LWIP_DIR}/src/api/tcpip.c
    ${LWIP_DIR}/src/netif/ethernet.c
    ${LWIP_DIR}/contrib/ports/freertos/sys_arch.c
)

add_library(lwip STATIC ${LWIP_SOURCES})
target_include_directories(lwip PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
    ${LWIP_DIR}/src/include
    ${LWIP_DIR}/contrib/ports/freertos/include
)
target_link_libraries(lwip PUBLIC stm32cubemx)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Core/Src/adc.c
    Core/Src/bitbang_i2c.c
    Core/Src/log.c
    Core/Src/switch.c
    Core/Src/tiny_printf.c
    Core/Src/unit.c
    Core/Src/usb.c
    Core/Src/usb_hub.c
    Core/Src/ws2812.c
    Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_adc.c
    Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_adc_ex.c
    Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_uart.c
    Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_uart_ex.c
    ${RTL8367C_DRIVER_SOURCES}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${RTL8367C_DRIVER_DIR}/include
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

    # Add user defined libraries
)

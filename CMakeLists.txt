# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

include(FetchContent)

FetchContent_Declare(
    no-os-fatfs-sd-spi-rpi-pico
    GIT_REPOSITORY https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico.git
    GIT_TAG        master
)
FetchContent_Populate(no-os-fatfs-sd-spi-rpi-pico)
add_subdirectory(
    ${no-os-fatfs-sd-spi-rpi-pico_SOURCE_DIR}/FatFs_SPI
    ${no-os-fatfs-sd-spi-rpi-pico_BINARY_DIR}
)

project(SID_Display_Dan C CXX ASM)

pico_sdk_init()

add_executable(SID_Display_Dan
        SID_Display_Dan.c
        hw_config.c
)

pico_set_program_name(SID_Display_Dan "SID_Display_Dan")
pico_set_program_version(SID_Display_Dan "0.1")

pico_enable_stdio_uart(SID_Display_Dan 0)
pico_enable_stdio_usb(SID_Display_Dan 0)

target_link_libraries(SID_Display_Dan
        pico_stdlib
        hardware_spi
        hardware_i2c
        hardware_uart
        FatFs_SPI)

target_include_directories(SID_Display_Dan PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(SID_Display_Dan)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

# Build name
set(BUILD_NAME "frank-c64")

# Firmware version string (set by release.sh or manually)
set(FIRMWARE_VERSION "v1.08" CACHE STRING "Firmware version string")

# Set board to Pico 2 (RP2350)
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Board configuration variant (M1, M2, PC or Z0)
set(BOARD_VARIANT "M2" CACHE STRING "Board variant: M1, M2 or PC")

# Video output type (VGA or HDMI)
set(VIDEO_TYPE "HDMI" CACHE STRING "Video output: VGA or HDMI")

# Audio output configuration (I2S or PWM)
set(AUDIO_TYPE "I2S" CACHE STRING "Audio output: I2S or PWM")

# CPU overclock configuration (252, 378, 428(VGA), 504 MHz)
set(CPU_SPEED "378" CACHE STRING "CPU speed: 252, 378, 428 (VGA only), 504 MHz")

# PSRAM speed configuration (84, 100, 133, 166 MHz target)
#set(PSRAM_SPEED "133" CACHE STRING "PSRAM max frequency in MHz: 84, 100, 133, 166")
set(FLASH_MAX_FREQ_MHZ "66")

if(VIDEO_TYPE STREQUAL "HDMI")
    set(VIDEO_HDMI ON)
elseif(VIDEO_TYPE STREQUAL "VGA")
    set(VIDEO_VGA ON)
endif()

if(PICO_BOARD STREQUAL "pico")
    if(BOARD_VARIANT STREQUAL "M1")
        SET(BUILD_NAME "m1p1-${BUILD_NAME}")
    elseif(BOARD_VARIANT STREQUAL "PC")
        SET(BUILD_NAME "PCp1-${BUILD_NAME}")
    elseif(BOARD_VARIANT STREQUAL "Z0")
        SET(BUILD_NAME "z0p1-${BUILD_NAME}")
    else()  # M2
        SET(BUILD_NAME "m2p1-${BUILD_NAME}")
    endif()
else()
    if(BOARD_VARIANT STREQUAL "M1")
        SET(BUILD_NAME "m1p2-${BUILD_NAME}")
    elseif(BOARD_VARIANT STREQUAL "PC")
        SET(BUILD_NAME "PCp2-${BUILD_NAME}")
    elseif(BOARD_VARIANT STREQUAL "Z0")
        SET(BUILD_NAME "z0p2-${BUILD_NAME}")
    else()  # M2
        SET(BUILD_NAME "m2p2-${BUILD_NAME}")
    endif()
endif()

IF(VIDEO_VGA)
    SET(BUILD_NAME "${BUILD_NAME}-VGA")
endif ()
IF(VIDEO_HDMI)
    SET(BUILD_NAME "${BUILD_NAME}-HDMI")
endif ()

if(PICO_BOARD STREQUAL "pico")

    SET(BUILD_NAME "${BUILD_NAME}-${CPU_SPEED}MHz-${AUDIO_TYPE}-${FIRMWARE_VERSION}")
else()
    if (PSRAM_SPEED)
        SET(BUILD_NAME "${BUILD_NAME}-${CPU_SPEED}MHz-F${FLASH_MAX_FREQ_MHZ}-P${PSRAM_SPEED}-${AUDIO_TYPE}-${FIRMWARE_VERSION}")
    else()
        SET(BUILD_NAME "${BUILD_NAME}-${CPU_SPEED}MHz-F${FLASH_MAX_FREQ_MHZ}-${AUDIO_TYPE}-${FIRMWARE_VERSION}")
    endif()
endif()

# CPU voltage selection based on speed
if(CPU_SPEED GREATER_EQUAL 504)
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_65")
elseif(CPU_SPEED GREATER_EQUAL 378)
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_60")
else()
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_50")
endif()

# I2S Audio pin configuration based on board variant
if(BOARD_VARIANT STREQUAL "M1")
    set(I2S_DATA_PIN "26")
    set(I2S_CLOCK_PIN_BASE "27")
    set(PWM_RIGHT_PIN "26")
    set(PWM_LEFT_PIN "27")
elseif(BOARD_VARIANT STREQUAL "PC")
    set(PWM_RIGHT_PIN "27")
    set(PWM_LEFT_PIN "28")
elseif(BOARD_VARIANT STREQUAL "Z0")
    set(I2S_DATA_PIN "10")
    set(I2S_CLOCK_PIN_BASE "11")
    set(PWM_RIGHT_PIN "10")
    set(PWM_LEFT_PIN "11")
else()  # M2
    set(I2S_DATA_PIN "9")
    set(I2S_CLOCK_PIN_BASE "10")
    set(PWM_RIGHT_PIN "10")
    set(PWM_LEFT_PIN "11")
endif()

# USB HID Host support (keyboard and gamepad via USB)
# When enabled, USB CDC stdio is disabled!
option(USB_HID_ENABLED "Enable USB HID Host for keyboard/gamepad" ON)

# PS/2 keyboard support toggle
option(PS2_KEYBOARD_ENABLED "Enable PS/2 keyboard input" ON)

# Verbose debug logging toggle
option(DEBUG_LOGS_ENABLED "Enable verbose debug logging" OFF)
# debug line (to connect other pico for this)
option(UART_ENABLED "Enable USB HID Host for keyboard/gamepad" OFF)

# Use Frodo Lite (line-based) instead of Frodo SC (cycle-accurate) for better performance
option(FRODO_LITE "Use Frodo Lite (line-based emulation)" ON)

message(STATUS "FRANK C64 - Commodore 64 Emulator (Frodo4) for RP2040/RP2350")
if (PSRAM_SPEED)
    message(STATUS "Board: ${BOARD_VARIANT}, CPU: ${CPU_SPEED} MHz, PSRAM: ${PSRAM_SPEED} MHz, Voltage: ${CPU_VOLTAGE}")
else()
    message(STATUS "Board: ${BOARD_VARIANT}, CPU: ${CPU_SPEED} MHz, PSRAM: N/A, Voltage: ${CPU_VOLTAGE}")
endif()
message(STATUS "I2S Audio: DATA=${I2S_DATA_PIN}, CLK_BASE=${I2S_CLOCK_PIN_BASE}")

include(pico_sdk_import.cmake)

set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

project(${BUILD_NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

# Copy driver subdirectories from murmapple structure
add_subdirectory(drivers/ps2kbd)
add_subdirectory(drivers/fatfs)
add_subdirectory(drivers/sdcard)
add_subdirectory(drivers/usbhid)

# Collect driver sources based on configuration
set(DRIVER_SOURCES
    drivers/nespad/nespad.c
    drivers/audio.c
)

if (PSRAM_SPEED)
    list(APPEND DRIVER_SOURCES
        drivers/psram_init.c
        drivers/psram_allocator.c
    )
endif()

if (VIDEO_HDMI)
    list(APPEND DRIVER_SOURCES drivers/HDMI.c)
endif()

if (VIDEO_VGA)
    list(APPEND DRIVER_SOURCES drivers/vga.c)
endif()

# Create a library for drivers
add_library(drivers ${DRIVER_SOURCES})

# Generate PIO header for audio I2S (must be after add_library)
pico_generate_pio_header(drivers ${CMAKE_CURRENT_LIST_DIR}/drivers/audio_i2s.pio)

target_include_directories(drivers PUBLIC drivers drivers/nespad src ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(drivers PRIVATE
    BOARD_${BOARD_VARIANT}
    I2S_DATA_PIN=${I2S_DATA_PIN}
    I2S_CLOCK_PIN_BASE=${I2S_CLOCK_PIN_BASE}
)
if (PSRAM_SPEED)
    target_compile_definitions(drivers PRIVATE PSRAM_MAX_FREQ_MHZ=${PSRAM_SPEED})
endif()
if(DEBUG_LOGS_ENABLED)
    target_compile_definitions(drivers PRIVATE ENABLE_DEBUG_LOGS=1)
else()
    target_compile_definitions(drivers PRIVATE ENABLE_DEBUG_LOGS=0)
endif()
target_link_libraries(drivers
    pico_stdlib
    hardware_dma
    hardware_pio
    hardware_spi
    hardware_clocks
    hardware_irq
    hardware_pwm
)

# Frodo4 C64 Emulator sources
add_executable(${BUILD_NAME}
    # RP2350 Platform layer
    src/rp2350/main_rp2350.c
    src/rp2350/Display_rp2350.cpp
    src/rp2350/C64_rp2350.cpp
    src/rp2350/Prefs_rp2350.cpp
    src/rp2350/ROM_data.cpp
    src/rp2350/sid_i2s.cpp
    src/rp2350/SID_rp2350.cpp
    src/rp2350/input_rp2350.cpp
    src/rp2350/disk_loader.c
    src/rp2350/disk_ui.c
    src/rp2350/startscreen.c
    src/rp2350/fatfs_stdio.c

    # Frodo4 emulator core (C++) - full emulation
    src/CPUC64.cpp
    src/VIC.cpp
    # src/SID.cpp  # Replaced by SID_rp2350.cpp (uses I2S instead of SDL)
    src/CIA.cpp
    src/IEC.cpp
    src/Cartridge.cpp
    src/REU.cpp
    src/rp2350/Tape_stub.cpp  # Stub implementation for RP2350
    src/1541d64.cpp
    # src/1541t64.cpp  # Disabled for RP2350 (T64 tape archives not supported)
    src/1541gcr.cpp
    # src/1541fs.cpp   # Disabled for RP2350 (filesystem drive not supported)
    src/CPU1541.cpp
    src/VIA.cpp
)

if (PICO_BOARD STREQUAL "pico")
    pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
    target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
    pico_set_boot_stage2(${BUILD_NAME} slower_boot2)
endif()

# Frodo Lite (line-based) vs Frodo SC (cycle-accurate) emulation
if(FRODO_LITE)
    message(STATUS "Using Frodo Lite (line-based emulation)")
else()
    message(STATUS "Using Frodo SC (subcycle-accurate emulation)")
    target_compile_definitions(${BUILD_NAME} PRIVATE FRODO_SC=1)
endif()

target_include_directories(${BUILD_NAME} PRIVATE
    src
    src/rp2350
    drivers
)

target_compile_definitions(${BUILD_NAME} PRIVATE
    BOARD_${BOARD_VARIANT}
    CPU_CLOCK_MHZ=${CPU_SPEED}
    CPU_VOLTAGE=${CPU_VOLTAGE}
    FLASH_MAX_FREQ_MHZ=${FLASH_MAX_FREQ_MHZ}
    FIRMWARE_VERSION="${FIRMWARE_VERSION}"
    # RP2350 platform flag
    FRODO_RP2350=1
)
if (PSRAM_SPEED)
    target_compile_definitions(${BUILD_NAME} PRIVATE PSRAM_MAX_FREQ_MHZ=${PSRAM_SPEED})
endif()

if(AUDIO_TYPE STREQUAL "I2S")
    target_compile_definitions(${BUILD_NAME} PRIVATE
        FEATURE_AUDIO_I2S=1
        # Disable SDL and GTK
        # I2S Audio configuration
        # NOTE: HDMI uses PIO1, PS/2 uses PIO0 SM0 (claimed dynamically)
        # So I2S uses PIO0 SM2 (claimed statically before PS/2 init)
        PICO_AUDIO_I2S_PIO=0
        PICO_AUDIO_I2S_STATE_MACHINE=2
        PICO_AUDIO_I2S_DMA_IRQ=1
        PICO_AUDIO_I2S_DMA_CHANNEL=6
        PICO_AUDIO_I2S_DATA_PIN=${I2S_DATA_PIN}
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=${I2S_CLOCK_PIN_BASE}
    )
    target_compile_definitions(drivers PRIVATE FEATURE_AUDIO_I2S=1)
elseif(AUDIO_TYPE STREQUAL "PWM")
    target_compile_definitions(${BUILD_NAME} PRIVATE FEATURE_AUDIO_PWM=1)
    target_compile_definitions(drivers PRIVATE FEATURE_AUDIO_PWM=1)
endif()

if (VIDEO_HDMI)
    target_compile_definitions(${BUILD_NAME} PRIVATE
        VIDEO_HDMI
        VIDEO=HDMI
    )
endif()
if (VIDEO_VGA)
    target_compile_definitions(${BUILD_NAME} PRIVATE
        VIDEO_VGA
        VIDEO=VGA
    )
endif()

if(PS2_KEYBOARD_ENABLED)
    target_compile_definitions(${BUILD_NAME} PRIVATE ENABLE_PS2_KEYBOARD=1)
else()
    target_compile_definitions(${BUILD_NAME} PRIVATE ENABLE_PS2_KEYBOARD=0)
endif()

if(DEBUG_LOGS_ENABLED)
    target_compile_definitions(${BUILD_NAME} PRIVATE ENABLE_DEBUG_LOGS=1)
else()
    target_compile_definitions(${BUILD_NAME} PRIVATE ENABLE_DEBUG_LOGS=0)
endif()

# Optimization for maximum performance on RP2350
# -O3: Maximum optimization including loop vectorization
# -ffunction-sections -fdata-sections: Allow linker to remove unused code
# -fno-strict-aliasing: Allow type punning (common in emulators)
target_compile_options(${BUILD_NAME} PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
    -fno-strict-aliasing
    -fno-exceptions
    -fno-rtti
)

target_link_libraries(${BUILD_NAME}
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_pio
    hardware_clocks
    hardware_vreg
    hardware_spi
    drivers
    ps2kbd
    sdcard
    fatfs
    usbhid
)

# USB CDC stdio: enabled for debugging, but disabled when USB HID is enabled
# (USB port can only be Host OR Device, not both)
if(USB_HID_ENABLED)
    pico_enable_stdio_usb(${BUILD_NAME} 0)
    message(STATUS "USB CDC stdio disabled (USB HID Host is enabled)")
else()
    pico_enable_stdio_usb(${BUILD_NAME} 1)
    # NOTE: Do NOT explicitly link tinyusb_device - pico_enable_stdio_usb handles it
    # Explicit linking causes "CDC not enabled" warning and breaks USB Serial!
    message(STATUS "USB CDC stdio ENABLED (USB HID Host is disabled)")
endif()
if(UART_ENABLED)
    pico_enable_stdio_uart(${BUILD_NAME} 1)
else()
    pico_enable_stdio_uart(${BUILD_NAME} 0)
endif()

# Create UF2 output
pico_add_extra_outputs(${BUILD_NAME})

target_link_options(${BUILD_NAME} PRIVATE -Xlinker --print-memory-usage --data-sections)

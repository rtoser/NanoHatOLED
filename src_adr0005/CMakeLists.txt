cmake_minimum_required(VERSION 3.13)
project(nanohat-oled C)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
add_compile_options(-Wall -Wextra)
add_compile_definitions(_POSIX_C_SOURCE=200809L _GNU_SOURCE)

# Apply optimization and gc-sections for non-Debug builds
string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)
if(BUILD_TYPE_UPPER STREQUAL "DEBUG")
    add_compile_options(-O0 -g)
    add_compile_definitions(DEBUG)
else()
    add_compile_options(-O2 -ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
    if(BUILD_TYPE_UPPER STREQUAL "RELEASE")
        add_compile_definitions(NDEBUG)
    endif()
endif()

# Configurable options
set(GPIOCHIP_PATH "/dev/gpiochip1" CACHE STRING "GPIO chip device path")
set(BTN_OFFSETS "0,2,3" CACHE STRING "Button GPIO offsets")
set(MONITORED_SERVICES "dropbear,xray_core,collectd,uhttpd" CACHE STRING "Services to monitor")

add_compile_definitions(
    GPIOCHIP_PATH="${GPIOCHIP_PATH}"
    BTN_OFFSETS=${BTN_OFFSETS}
    MONITORED_SERVICES="${MONITORED_SERVICES}"
)

# Target sysroot for cross-compilation
set(TARGET_DIR "/opt/target" CACHE PATH "Target sysroot directory")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/u8g2/csrc
    ${TARGET_DIR}/usr/include
)

# Link directories
link_directories(${TARGET_DIR}/usr/lib)

# Application sources
set(APP_SOURCES
    main.c
    event_loop.c
    event_queue.c
    ring_queue.c
    ui_thread.c
    ui_controller.c
    anim.c
    page_controller.c
    pages/page_home.c
    pages/page_network.c
    pages/page_gateway.c
    pages/page_services.c
    task_queue.c
    result_queue.c
    ubus_thread.c
    service_config.c
    sys_status.c
    fonts.c
    hal/gpio_hal_libgpiod.c
    hal/time_hal_real.c
    hal/display_hal_ssd1306.c
    hal/ubus_hal_real.c
)

# U8g2 sources - only what we need for SSD1306 128x64
set(U8G2_DIR ${CMAKE_CURRENT_SOURCE_DIR}/u8g2/csrc)

# Core u8g2 files
set(U8G2_CORE
    ${U8G2_DIR}/u8g2_buffer.c
    ${U8G2_DIR}/u8g2_box.c
    ${U8G2_DIR}/u8g2_cleardisplay.c
    ${U8G2_DIR}/u8g2_d_memory.c
    ${U8G2_DIR}/u8g2_d_setup.c
    ${U8G2_DIR}/u8g2_font.c
    ${U8G2_DIR}/u8g2_fonts.c
    ${U8G2_DIR}/u8g2_hvline.c
    ${U8G2_DIR}/u8g2_intersection.c
    ${U8G2_DIR}/u8g2_ll_hvline.c
    ${U8G2_DIR}/u8g2_setup.c
)

# Core u8x8 files
set(U8X8_CORE
    ${U8G2_DIR}/u8x8_8x8.c
    ${U8G2_DIR}/u8x8_byte.c
    ${U8G2_DIR}/u8x8_cad.c
    ${U8G2_DIR}/u8x8_display.c
    ${U8G2_DIR}/u8x8_gpio.c
    ${U8G2_DIR}/u8x8_setup.c
    ${U8G2_DIR}/u8x8_string.c
)

# SSD1306 128x64 driver only
set(U8X8_DRIVER
    ${U8G2_DIR}/u8x8_d_ssd1306_128x64_noname.c
)

set(U8G2_SOURCES ${U8G2_CORE} ${U8X8_CORE} ${U8X8_DRIVER})

# Create executable
add_executable(nanohat-oled ${APP_SOURCES} ${U8G2_SOURCES})

# Link libraries
target_link_libraries(nanohat-oled
    pthread
    gpiod
    ubus
    ubox
)

# Install
install(TARGETS nanohat-oled RUNTIME DESTINATION bin)

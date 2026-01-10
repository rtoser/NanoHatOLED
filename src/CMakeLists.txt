cmake_minimum_required(VERSION 3.10)
project(nanohat-oled C)

# C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Build mode: HOST (mock GPIO for testing) or TARGET (real hardware)
set(BUILD_MODE "TARGET" CACHE STRING "Build mode: HOST or TARGET")
set_property(CACHE BUILD_MODE PROPERTY STRINGS "HOST" "TARGET")

# Debug options
option(GPIO_DEBUG "Enable GPIO debug logging" OFF)

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

# Target sysroot for cross-compilation
set(TARGET_DIR "/opt/target" CACHE PATH "Target sysroot directory")

# u8g2 path (Phase 3: real display driver on TARGET)
set(U8G2_DIR ${CMAKE_CURRENT_SOURCE_DIR}/u8g2/csrc)

# u8g2 sources (TARGET only)
set(U8G2_CORE
    ${U8G2_DIR}/u8g2_buffer.c
    ${U8G2_DIR}/u8g2_box.c
    ${U8G2_DIR}/u8g2_cleardisplay.c
    ${U8G2_DIR}/u8g2_d_memory.c
    ${U8G2_DIR}/u8g2_d_setup.c
    ${U8G2_DIR}/u8g2_font.c
    ${U8G2_DIR}/u8g2_fonts.c
    ${U8G2_DIR}/u8g2_hvline.c
    ${U8G2_DIR}/u8g2_ll_hvline.c
    ${U8G2_DIR}/u8g2_setup.c
    ${U8G2_DIR}/u8g2_intersection.c
)

set(U8X8_CORE
    ${U8G2_DIR}/u8x8_8x8.c
    ${U8G2_DIR}/u8x8_byte.c
    ${U8G2_DIR}/u8x8_cad.c
    ${U8G2_DIR}/u8x8_display.c
    ${U8G2_DIR}/u8x8_gpio.c
    ${U8G2_DIR}/u8x8_setup.c
)

set(U8X8_DRIVER
    ${U8G2_DIR}/u8x8_d_ssd1306_128x64_noname.c
)

# Silence upstream warning in u8g2_ll_hvline.c without editing submodule.
set_source_files_properties(
    ${U8G2_DIR}/u8g2_ll_hvline.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-variable"
)


# Application sources
set(APP_SOURCES
    main.c
    ui_controller.c
    page_controller.c
    anim.c
    sys_status.c
    service_config.c
    pages/page_home.c
    pages/page_gateway.c
    pages/page_network.c
    pages/page_services.c
    hal/time_hal_real.c
)

# GPIO + display HAL selection
set(U8G2_SOURCES "")
if(BUILD_MODE STREQUAL "HOST")
    list(APPEND APP_SOURCES
        hal/gpio_hal_mock.c
        hal/display_hal_null.c
        hal/u8g2_stub.c
    )
    message(STATUS "Using GPIO mock + null display (HOST mode)")
else()
    list(APPEND APP_SOURCES
        hal/gpio_hal_libgpiod.c
        hal/display_hal_ssd1306.c
        fonts.c
    )
    list(APPEND U8G2_SOURCES ${U8G2_CORE} ${U8X8_CORE} ${U8X8_DRIVER})
    message(STATUS "Using GPIO libgpiod + SSD1306 display (TARGET mode)")
endif()

# GPIO debug logging
if(GPIO_DEBUG)
    add_compile_definitions(GPIO_DEBUG)
endif()

# Create executable
add_executable(nanohat-oled ${APP_SOURCES} ${U8G2_SOURCES})

# Include directories (modern CMake)
target_include_directories(nanohat-oled PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/hal
    ${U8G2_DIR}
    ${TARGET_DIR}/usr/include
)

# Link libraries (with explicit path for cross-compilation)
target_link_directories(nanohat-oled PRIVATE
    ${TARGET_DIR}/usr/lib
)

# Link libraries
set(LINK_LIBS ubox pthread)

# Add libgpiod for TARGET mode
if(NOT BUILD_MODE STREQUAL "HOST")
    list(APPEND LINK_LIBS gpiod)
endif()

target_link_libraries(nanohat-oled ${LINK_LIBS})

# Print build info
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Build mode: ${BUILD_MODE}")
message(STATUS "Target sysroot: ${TARGET_DIR}")

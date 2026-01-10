cmake_minimum_required(VERSION 3.10)
project(nanohat-oled-tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -g)
add_compile_definitions(_POSIX_C_SOURCE=200809L _GNU_SOURCE)

# Option to fail if libubox is missing (for CI)
option(REQUIRE_LIBUBOX "Fail if libubox-dev is not found" OFF)

# Check for libubox (required for host tests)
find_path(LIBUBOX_INCLUDE_DIR libubox/uloop.h
    PATHS /usr/include /usr/local/include
)
find_library(LIBUBOX_LIBRARY ubox
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
)

if(NOT LIBUBOX_INCLUDE_DIR OR NOT LIBUBOX_LIBRARY)
    set(_msg "libubox-dev not found. Install with:\n")
    set(_msg "${_msg}  Ubuntu/Debian: sudo apt install libubox-dev\n")
    set(_msg "${_msg}  Alpine: apk add libubox-dev\n")
    set(_msg "${_msg}  macOS: brew install libubox (or build from source)")

    if(REQUIRE_LIBUBOX)
        message(FATAL_ERROR "${_msg}")
    else()
        message(WARNING "\n=== ${_msg} ===\nTests will be skipped.\n")
    endif()
else()
    message(STATUS "Found libubox: ${LIBUBOX_LIBRARY}")

    # Source directory
    set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)

    # Common UI sources (host uses stub display)
    set(UI_SOURCES
        ${SRC_DIR}/ui_controller.c
        ${SRC_DIR}/ui_draw.c
        ${SRC_DIR}/page_controller.c
        ${SRC_DIR}/anim.c
        ${SRC_DIR}/sys_status.c
        ${SRC_DIR}/service_config.c
        ${SRC_DIR}/pages/page_home.c
        ${SRC_DIR}/pages/page_gateway.c
        ${SRC_DIR}/pages/page_network.c
        ${SRC_DIR}/pages/page_services.c
        ${SRC_DIR}/hal/display_hal_null.c
        ${SRC_DIR}/hal/u8g2_stub.c
        ${SRC_DIR}/hal/time_hal_real.c
        ${SRC_DIR}/hal/ubus_hal_mock.c
    )

    # Test: uloop smoke test
    add_executable(test_uloop_smoke
        test_uloop_smoke.c
    )
    target_include_directories(test_uloop_smoke PRIVATE
        ${SRC_DIR}
        ${SRC_DIR}/hal
        ${LIBUBOX_INCLUDE_DIR}
    )
    target_link_libraries(test_uloop_smoke
        ${LIBUBOX_LIBRARY}
    )

    # Test: timer basic test
    add_executable(test_timer_basic
        test_timer_basic.c
    )
    target_include_directories(test_timer_basic PRIVATE
        ${SRC_DIR}
        ${SRC_DIR}/hal
        ${LIBUBOX_INCLUDE_DIR}
    )
    target_link_libraries(test_timer_basic
        ${LIBUBOX_LIBRARY}
    )

    # Test: GPIO event with uloop
    add_executable(test_gpio_event_uloop
        test_gpio_event_uloop.c
        ${SRC_DIR}/hal/gpio_hal_mock.c
        ${SRC_DIR}/hal/time_hal_real.c
    )
    target_include_directories(test_gpio_event_uloop PRIVATE
        ${SRC_DIR}
        ${SRC_DIR}/hal
        ${LIBUBOX_INCLUDE_DIR}
    )
    target_link_libraries(test_gpio_event_uloop
        ${LIBUBOX_LIBRARY}
        pthread
    )

    # Test: UI controller
    add_executable(test_ui_controller
        test_ui_controller.c
        ${UI_SOURCES}
    )
    target_include_directories(test_ui_controller PRIVATE
        ${SRC_DIR}
        ${SRC_DIR}/hal
        ${LIBUBOX_INCLUDE_DIR}
    )
    target_link_libraries(test_ui_controller
        ${LIBUBOX_LIBRARY}
        pthread
    )

    # Test: UI refresh policy
    add_executable(test_ui_refresh_policy
        test_ui_refresh_policy.c
        ${UI_SOURCES}
    )
    target_include_directories(test_ui_refresh_policy PRIVATE
        ${SRC_DIR}
        ${SRC_DIR}/hal
        ${LIBUBOX_INCLUDE_DIR}
    )
    target_link_libraries(test_ui_refresh_policy
        ${LIBUBOX_LIBRARY}
        pthread
    )

    # Test: ubus async with uloop
    add_executable(test_ubus_async_uloop
        test_ubus_async_uloop.c
        ${SRC_DIR}/hal/ubus_hal_mock.c
        ${SRC_DIR}/hal/time_hal_real.c
        ${SRC_DIR}/sys_status.c
        ${SRC_DIR}/service_config.c
    )
    target_include_directories(test_ubus_async_uloop PRIVATE
        ${SRC_DIR}
        ${SRC_DIR}/hal
        ${LIBUBOX_INCLUDE_DIR}
    )
    target_link_libraries(test_ubus_async_uloop
        ${LIBUBOX_LIBRARY}
    )

    # Custom test target
    enable_testing()
    add_test(NAME uloop_smoke COMMAND test_uloop_smoke)
    add_test(NAME timer_basic COMMAND test_timer_basic)
    add_test(NAME gpio_event_uloop COMMAND test_gpio_event_uloop)
    add_test(NAME ui_controller COMMAND test_ui_controller)
    add_test(NAME ui_refresh_policy COMMAND test_ui_refresh_policy)
    add_test(NAME ubus_async_uloop COMMAND test_ubus_async_uloop)

    message(STATUS "Tests configured successfully")
endif()

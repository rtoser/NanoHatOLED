# CMake generated Testfile for 
# Source directory: /repo/tests
# Build directory: /repo/tests/build/target
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(uloop_smoke "/repo/tests/build/target/test_uloop_smoke")
set_tests_properties(uloop_smoke PROPERTIES  _BACKTRACE_TRIPLES "/repo/tests/CMakeLists.txt;147;add_test;/repo/tests/CMakeLists.txt;0;")
add_test(timer_basic "/repo/tests/build/target/test_timer_basic")
set_tests_properties(timer_basic PROPERTIES  _BACKTRACE_TRIPLES "/repo/tests/CMakeLists.txt;148;add_test;/repo/tests/CMakeLists.txt;0;")
add_test(gpio_event_uloop "/repo/tests/build/target/test_gpio_event_uloop")
set_tests_properties(gpio_event_uloop PROPERTIES  _BACKTRACE_TRIPLES "/repo/tests/CMakeLists.txt;149;add_test;/repo/tests/CMakeLists.txt;0;")
add_test(ui_controller "/repo/tests/build/target/test_ui_controller")
set_tests_properties(ui_controller PROPERTIES  _BACKTRACE_TRIPLES "/repo/tests/CMakeLists.txt;150;add_test;/repo/tests/CMakeLists.txt;0;")
add_test(ui_refresh_policy "/repo/tests/build/target/test_ui_refresh_policy")
set_tests_properties(ui_refresh_policy PROPERTIES  _BACKTRACE_TRIPLES "/repo/tests/CMakeLists.txt;151;add_test;/repo/tests/CMakeLists.txt;0;")
add_test(ubus_async_uloop "/repo/tests/build/target/test_ubus_async_uloop")
set_tests_properties(ubus_async_uloop PROPERTIES  _BACKTRACE_TRIPLES "/repo/tests/CMakeLists.txt;152;add_test;/repo/tests/CMakeLists.txt;0;")

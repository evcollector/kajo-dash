cmake_minimum_required(VERSION 3.20)
project(cyd_lvgl_preview LANGUAGES C CXX)

option(CYD_BUILD_SIMULATOR "Build the interactive SDL2 CYD simulator" OFF)
option(CYD_BUILD_TESTS "Build the native test targets (implied by CYD_BUILD_SIMULATOR)" OFF)
option(CYD_BUILD_WEB_SIMULATOR "Build the interactive browser CYD simulator" OFF)

if(NOT DEFINED LVGL_DIR OR NOT EXISTS "${LVGL_DIR}/lvgl.h")
  message(FATAL_ERROR "Set LVGL_DIR to the PlatformIO LVGL library directory")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LV_CONF_PATH "${PROJECT_ROOT}/include/lv_conf.h" CACHE FILEPATH "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)

add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl" EXCLUDE_FROM_ALL)
target_include_directories(lvgl PUBLIC "${CMAKE_CURRENT_LIST_DIR}" "${PROJECT_ROOT}/include")

set(FIRMWARE_SOURCES
  "${PROJECT_ROOT}/src/lvgl_app/ride_replay_core.cpp"
  "${PROJECT_ROOT}/src/lvgl_app/ride_replay.cpp"
  "${PROJECT_ROOT}/src/lvgl_app/app_logic.cpp"
  "${PROJECT_ROOT}/src/lvgl_app/controller_manager.cpp"
  "${PROJECT_ROOT}/src/lvgl_app/cyd_icons_1bit.cpp"
  "${PROJECT_ROOT}/src/lvgl_app/dashboards.cpp"
  "${PROJECT_ROOT}/src/lvgl_app/screens.cpp"
  "${PROJECT_ROOT}/src/lvgl_app/ui_common.cpp"
  "${PROJECT_ROOT}/src/lvgl_app/vehicle_fields.cpp"
  "${PROJECT_ROOT}/src/lvgl_app/pixel_font.c"
  "${PROJECT_ROOT}/src/lvgl_app/lv_font_rajdhani_10.c"
  "${PROJECT_ROOT}/src/lvgl_app/lv_font_rajdhani_12.c"
  "${PROJECT_ROOT}/src/lvgl_app/lv_font_rajdhani_14.c"
  "${PROJECT_ROOT}/src/lvgl_app/lv_font_rajdhani_16.c"
  "${PROJECT_ROOT}/src/lvgl_app/lv_font_rajdhani_20.c"
  "${PROJECT_ROOT}/src/lvgl_app/lv_font_rajdhani_24.c"
  "${PROJECT_ROOT}/src/lvgl_app/lv_font_rajdhani_36.c"
  "${PROJECT_ROOT}/src/lvgl_app/lv_font_speed48.c"
  "${PROJECT_ROOT}/src/lvgl_app/lv_font_speed72.c"
  "${PROJECT_ROOT}/src/lvgl_app/lv_font_speed96.c"
)

set(HOST_SUPPORT_SOURCES
  preview_stubs.cpp
  host_runtime.cpp
  Preferences.cpp
)

function(configure_cyd_host_target target)
  target_compile_definitions(${target} PRIVATE CYD_LVGL_PREVIEW=1 LV_CONF_INCLUDE_SIMPLE=1)
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${PROJECT_ROOT}/include"
    "${PROJECT_ROOT}/src/lvgl_app"
    "${LVGL_DIR}"
  )
  target_link_libraries(${target} PRIVATE lvgl)
  if(MSVC)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(${target} PRIVATE /W3 /utf-8 /wd4244 /wd4267 /wd4996)
  endif()
endfunction()

add_executable(cyd_lvgl_preview preview_main.cpp ${HOST_SUPPORT_SOURCES} ${FIRMWARE_SOURCES})
set_target_properties(cyd_lvgl_preview PROPERTIES OUTPUT_NAME cyd_lvgl_renderer)
configure_cyd_host_target(cyd_lvgl_preview)

if(CYD_BUILD_WEB_SIMULATOR)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "CYD_BUILD_WEB_SIMULATOR requires the Emscripten toolchain")
  endif()
  add_executable(cyd_web_simulator browser_simulator_main.cpp ${HOST_SUPPORT_SOURCES} ${FIRMWARE_SOURCES})
  set_target_properties(cyd_web_simulator PROPERTIES OUTPUT_NAME index SUFFIX ".html")
  set_property(TARGET cyd_web_simulator APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/browser_shell.html")
  configure_cyd_host_target(cyd_web_simulator)
  target_compile_options(cyd_web_simulator PRIVATE -O2)
  target_link_options(cyd_web_simulator PRIVATE
    -O2
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=67108864"
    "-sENVIRONMENT=web"
    "-sSINGLE_FILE=1"
    "-sEXIT_RUNTIME=0"
    "-sASSERTIONS=1"
    # copyRequestedHostInfo() writes the host string through stringToUTF8; without
    # this export the helper is an aborting stub and main() dies before it can
    # start the render loop.
    "-sEXPORTED_RUNTIME_METHODS=['stringToUTF8']"
    "--shell-file=${CMAKE_CURRENT_LIST_DIR}/browser_shell.html"
  )
endif()

if(CYD_BUILD_SIMULATOR)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    SDL2
    URL "https://www.libsdl.org/release/SDL2-2.32.10.tar.gz"
    URL_HASH "SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(SDL2)

  add_executable(cyd_simulator
    simulator_main.cpp
    windows_simulator_menu.cpp
    ${HOST_SUPPORT_SOURCES}
    ${FIRMWARE_SOURCES}
  )
  set_target_properties(cyd_simulator PROPERTIES OUTPUT_NAME cyd_simulator)
  configure_cyd_host_target(cyd_simulator)
  target_link_libraries(cyd_simulator PRIVATE SDL2::SDL2-static)
  if(WIN32)
    enable_language(RC)
    configure_file(simulator_icon.rc.in "${CMAKE_CURRENT_BINARY_DIR}/simulator_icon.rc" @ONLY)
    target_sources(cyd_simulator PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/simulator_icon.rc")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/simulator_icon.rc" PROPERTIES
      OBJECT_DEPENDS "${PROJECT_ROOT}/tools/logo/kajo_app_icon.ico")
    target_link_libraries(cyd_simulator PRIVATE comdlg32)
  endif()
endif()

# None of the tests drive a window, so they are kept out of the block above:
# CYD_BUILD_TESTS builds and runs them without fetching or compiling SDL2,
# which is all CI needs. Building the simulator still brings them along.
if(CYD_BUILD_TESTS OR CYD_BUILD_SIMULATOR)
  enable_testing()
  add_executable(cyd_ride_replay_test ride_replay_test.cpp "${PROJECT_ROOT}/src/lvgl_app/ride_replay_core.cpp")
  target_include_directories(cyd_ride_replay_test PRIVATE "${PROJECT_ROOT}/src/lvgl_app")
  add_test(NAME cyd_ride_replay COMMAND cyd_ride_replay_test)
  add_executable(cyd_ride_log_policy_test ride_log_policy_test.cpp)
  target_include_directories(cyd_ride_log_policy_test PRIVATE "${PROJECT_ROOT}/src/lvgl_app")
  add_test(NAME cyd_ride_log_policy COMMAND cyd_ride_log_policy_test)
  # Like the VESC sender below, checked against the firmware's own stream and
  # decoder rather than against a second copy of the frame layout.
  add_executable(cyd_fardriver_sender_test fardriver_sender_test.cpp
                 "${PROJECT_ROOT}/src/lvgl_app/fardriver_protocol.cpp")
  add_test(NAME cyd_fardriver_sender COMMAND cyd_fardriver_sender_test)
  # The VESC sender is checked against the firmware's own parsers, so it links
  # the real protocol module rather than restating the layout a second time.
  add_executable(cyd_vesc_sender_test vesc_sender_test.cpp
                 "${CMAKE_CURRENT_LIST_DIR}/../../src/lvgl_app/vesc_protocol.cpp")
  add_test(NAME cyd_vesc_sender COMMAND cyd_vesc_sender_test)
  add_executable(cyd_preferences_test preferences_test.cpp Preferences.cpp)
  target_include_directories(cyd_preferences_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
  add_test(
    NAME cyd_preferences
    COMMAND cyd_preferences_test "${CMAKE_BINARY_DIR}/preferences_test.bin"
  )

  add_executable(cyd_gauge_ranges_test gauge_ranges_test.cpp ${HOST_SUPPORT_SOURCES} ${FIRMWARE_SOURCES})
  configure_cyd_host_target(cyd_gauge_ranges_test)
  add_test(NAME cyd_gauge_ranges COMMAND cyd_gauge_ranges_test)

  add_executable(cyd_demo_ride_test demo_ride_test.cpp ${HOST_SUPPORT_SOURCES} ${FIRMWARE_SOURCES})
  configure_cyd_host_target(cyd_demo_ride_test)
  add_test(NAME cyd_demo_ride COMMAND cyd_demo_ride_test)

  add_executable(cyd_runtime_input_test runtime_input_test.cpp ${HOST_SUPPORT_SOURCES} ${FIRMWARE_SOURCES})
  configure_cyd_host_target(cyd_runtime_input_test)
  add_test(NAME cyd_runtime_input COMMAND cyd_runtime_input_test)

  add_executable(cyd_dashboard_redraw_test dashboard_redraw_test.cpp ${HOST_SUPPORT_SOURCES} ${FIRMWARE_SOURCES})
  configure_cyd_host_target(cyd_dashboard_redraw_test)
  add_test(NAME cyd_dashboard_redraw COMMAND cyd_dashboard_redraw_test)
  add_executable(cyd_efficiency_redraw_test efficiency_redraw_test.cpp ${HOST_SUPPORT_SOURCES} ${FIRMWARE_SOURCES})
  configure_cyd_host_target(cyd_efficiency_redraw_test)
  add_test(NAME cyd_efficiency_redraw COMMAND cyd_efficiency_redraw_test)

  # The vehicle field model builds no widgets of its own, so it is tested
  # against the settings logic and the host stubs rather than through the
  # screen stack: no screens.cpp, no dashboards, no fonts.
  add_executable(cyd_vehicle_fields_test
    vehicle_fields_test.cpp
    "${PROJECT_ROOT}/src/lvgl_app/vehicle_fields.cpp"
    "${PROJECT_ROOT}/src/lvgl_app/app_logic.cpp"
    "${PROJECT_ROOT}/src/lvgl_app/controller_manager.cpp"
    # The host stubs build their fake ride log with the replay CRC.
    "${PROJECT_ROOT}/src/lvgl_app/ride_replay_core.cpp"
    # LV_FONT_DEFAULT in lv_conf.h, which lvgl.lib references even though this
    # target never draws anything.
    "${PROJECT_ROOT}/src/lvgl_app/lv_font_rajdhani_14.c"
    ${HOST_SUPPORT_SOURCES})
  configure_cyd_host_target(cyd_vehicle_fields_test)
  add_test(NAME cyd_vehicle_fields COMMAND cyd_vehicle_fields_test)

  # The VESC wire protocol has no Arduino or LVGL dependency, so it is tested on
  # its own rather than through the firmware sources.
  add_executable(cyd_vesc_protocol_test
    vesc_protocol_test.cpp
    "${PROJECT_ROOT}/src/lvgl_app/vesc_protocol.cpp")
  target_include_directories(cyd_vesc_protocol_test PRIVATE "${PROJECT_ROOT}/src/lvgl_app")
  add_test(NAME cyd_vesc_protocol COMMAND cyd_vesc_protocol_test)
endif()

if(MSVC)
  target_compile_options(lvgl PRIVATE /W1)
endif()

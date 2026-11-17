# -*- cmake -*-
#
# sw67core: 6|7 World Studio's viewer-independent core (accessibility
# framework, RLV gate). Pure C++20 with no third-party dependencies, so it
# builds and tests on its own as well as inside the viewer tree:
#
#   cmake -S indra/sw67core -B build-sw67core -DSW67_BUILD_TESTS=ON
#   cmake --build build-sw67core && ctest --test-dir build-sw67core
#
# Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.

cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(sw67core LANGUAGES CXX)
  set(SW67_STANDALONE ON)
endif()

add_library(
  sw67core
  STATIC
  src/color.cpp
  src/profile.cpp
  src/gate.cpp
  src/json.cpp
  src/settings_policy.cpp
  src/workspace_profile.cpp
  src/perf_assistant.cpp
  src/creator_project.cpp
  src/photo_product.cpp
  src/entitlements.cpp
  src/assistant_chat.cpp
)
add_library(sw67::core ALIAS sw67core)
target_include_directories(sw67core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(sw67core PUBLIC cxx_std_20)
if(TARGET al::flags)
  # Inside the viewer tree every target carries the shared flags
  # (al_check_flags_target enforces it).
  target_link_libraries(sw67core PRIVATE al::flags)
elseif(MSVC)
  target_compile_options(sw67core PRIVATE /W4 /permissive-)
else()
  target_compile_options(sw67core PRIVATE -Wall -Wextra -Wpedantic)
endif()
set_target_properties(sw67core PROPERTIES FOLDER "Libraries")

option(SW67_BUILD_TESTS "Build sw67core's tests" OFF)
if(SW67_BUILD_TESTS OR (DEFINED AL_BUILD_TESTS AND AL_BUILD_TESTS))
  find_package(Catch2 3 CONFIG REQUIRED)
  add_executable(
    sw67core_tests
    tests/color_test.cpp
    tests/profile_test.cpp
    tests/gate_test.cpp
    tests/json_test.cpp
    tests/settings_policy_test.cpp
    tests/workspace_test.cpp
    tests/perf_test.cpp
    tests/creator_test.cpp
    tests/photo_test.cpp
    tests/entitlements_test.cpp
    tests/assistant_test.cpp
  )
  target_link_libraries(sw67core_tests PRIVATE sw67core Catch2::Catch2WithMain)
  if(TARGET al::flags)
    target_link_libraries(sw67core_tests PRIVATE al::flags)
  endif()
  # The shipped plan catalogue is tested as it ships.
  target_compile_definitions(
    sw67core_tests
    PRIVATE
      SW67_PLANS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../newview/app_settings/sw67_plans.json"
  )
  set_target_properties(sw67core_tests PROPERTIES FOLDER "Tests/sw67core")
  if(SW67_STANDALONE)
    enable_testing()
  endif()
  add_test(NAME sw67core_tests COMMAND sw67core_tests)
endif()

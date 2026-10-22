cmake_minimum_required(VERSION 3.16)
project(fortress C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3)

set(GAME_SOURCES
  src/game.c
  src/enemy.c
  src/level.c
  src/gfx.c
  src/audio.c
  src/common.c
)

add_executable(fortress
  src/main.c
  src/render.c
  src/assets.c
  src/weapons.c
  src/font.c
  ${GAME_SOURCES}
)

target_link_libraries(fortress PRIVATE SDL3::SDL3)

if(MSVC)
  target_compile_options(fortress PRIVATE /W3)
else()
  target_compile_options(fortress PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_link_libraries(fortress PRIVATE m)
endif()

# the levels ship next to the executable
add_custom_command(TARGET fortress POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/levels $<TARGET_FILE_DIR:fortress>/levels)

include(CTest)
if(BUILD_TESTING)
  add_executable(barrel_tests tests/barrels.c ${GAME_SOURCES})
  target_include_directories(barrel_tests PRIVATE src)
  target_link_libraries(barrel_tests PRIVATE SDL3::SDL3)
  if(MSVC)
    target_compile_options(barrel_tests PRIVATE /W3)
  else()
    target_compile_options(barrel_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(barrel_tests PRIVATE m)
  endif()
  add_test(NAME barrel_combat COMMAND barrel_tests)
  set_tests_properties(barrel_combat PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

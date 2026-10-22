cmake_minimum_required(VERSION 3.16)
project(fortress C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(FORTRESS_DEBUG_TOOLS "Enable development shortcuts such as level selection" ON)

find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3)

set(GAME_SOURCES
  src/game.c
  src/enemy.c
  src/level.c
  src/gfx.c
  src/audio.c
  src/common.c
  src/savegame.c
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
target_compile_definitions(fortress PRIVATE FORTRESS_DEBUG_TOOLS=$<BOOL:${FORTRESS_DEBUG_TOOLS}>)

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
  add_executable(difficulty_tests tests/difficulty.c ${GAME_SOURCES})
  add_executable(savegame_tests tests/savegame.c ${GAME_SOURCES})
  add_executable(savegame_ui_tests tests/savegame_ui.c src/render.c src/assets.c src/weapons.c src/font.c ${GAME_SOURCES})
  add_executable(wall_tests tests/walls.c src/render.c src/assets.c src/weapons.c src/font.c ${GAME_SOURCES})
  add_executable(pit_tests tests/pits.c src/render.c src/assets.c src/weapons.c src/font.c ${GAME_SOURCES})
  add_executable(pendulum_tests tests/pendulums.c src/render.c src/assets.c src/weapons.c src/font.c ${GAME_SOURCES})
  add_executable(water_tests tests/water.c src/render.c src/assets.c src/weapons.c src/font.c ${GAME_SOURCES})
  foreach(test_target barrel_tests difficulty_tests savegame_tests savegame_ui_tests wall_tests pit_tests pendulum_tests water_tests)
    target_include_directories(${test_target} PRIVATE src)
    target_link_libraries(${test_target} PRIVATE SDL3::SDL3)
    target_compile_definitions(${test_target} PRIVATE FORTRESS_DEBUG_TOOLS=$<BOOL:${FORTRESS_DEBUG_TOOLS}>)
    if(MSVC)
      target_compile_options(${test_target} PRIVATE /W3)
    else()
      target_compile_options(${test_target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
      target_link_libraries(${test_target} PRIVATE m)
    endif()
  endforeach()
  add_test(NAME barrel_combat COMMAND barrel_tests)
  add_test(NAME difficulty COMMAND difficulty_tests)
  add_test(NAME savegame COMMAND savegame_tests)
  add_test(NAME savegame_ui COMMAND savegame_ui_tests)
  add_test(NAME destructible_walls COMMAND wall_tests)
  add_test(NAME pit_traps COMMAND pit_tests)
  add_test(NAME pendulum_traps COMMAND pendulum_tests)
  add_test(NAME shallow_water COMMAND water_tests)
  set_tests_properties(barrel_combat PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(destructible_walls PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(pit_traps PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(pendulum_traps PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(shallow_water PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

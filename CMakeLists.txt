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

set(VISUAL_SOURCES
  src/render.c
  src/assets.c
  src/weapons.c
  src/font.c
  src/comic.c
)

add_executable(fortress src/main.c ${VISUAL_SOURCES} ${GAME_SOURCES})

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
  add_executable(savegame_ui_tests tests/savegame_ui.c ${VISUAL_SOURCES} ${GAME_SOURCES})
  add_executable(gamepad_tests tests/gamepad.c ${VISUAL_SOURCES} ${GAME_SOURCES})
  add_executable(comic_tests tests/comic.c ${VISUAL_SOURCES} ${GAME_SOURCES})
  add_executable(wall_tests tests/walls.c ${VISUAL_SOURCES} ${GAME_SOURCES})
  add_executable(pit_tests tests/pits.c ${VISUAL_SOURCES} ${GAME_SOURCES})
  add_executable(pendulum_tests tests/pendulums.c ${VISUAL_SOURCES} ${GAME_SOURCES})
  add_executable(water_tests tests/water.c ${VISUAL_SOURCES} ${GAME_SOURCES})
  add_executable(stair_tests tests/stairs.c ${VISUAL_SOURCES} ${GAME_SOURCES})
  add_executable(surface_tests tests/surfaces.c ${VISUAL_SOURCES} ${GAME_SOURCES})
  foreach(test_target barrel_tests difficulty_tests savegame_tests savegame_ui_tests gamepad_tests comic_tests wall_tests pit_tests pendulum_tests water_tests surface_tests stair_tests)
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
  add_test(NAME gamepad COMMAND gamepad_tests)
  add_test(NAME comic_story COMMAND comic_tests)
  add_test(NAME destructible_walls COMMAND wall_tests)
  add_test(NAME pit_traps COMMAND pit_tests)
  add_test(NAME pendulum_traps COMMAND pendulum_tests)
  add_test(NAME shallow_water COMMAND water_tests)
  add_test(NAME vertical_levels COMMAND stair_tests)
  set_tests_properties(vertical_levels PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME floor_and_ceiling COMMAND surface_tests)
  set_tests_properties(savegame_ui gamepad comic_story PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(barrel_combat PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(destructible_walls PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(pit_traps PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(pendulum_traps PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(shallow_water PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(floor_and_ceiling PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

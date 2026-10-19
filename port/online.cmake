# Online simulation inputs and transport, shared by the desktop and headless tests.
target_sources(phantomcrash_core PRIVATE src/battle_checkpoint.cpp src/robot_checkpoint.cpp
  src/rumbling_checkpoint.cpp)
add_library(phantomcrash_online STATIC src/online/input.cpp src/online/socket.cpp
  src/online/discovery.cpp src/online/interfaces.cpp
  src/online/prediction.cpp src/online/peer.cpp src/online/room.cpp src/online/room_leave.cpp
  src/online/input_channel.cpp
  src/online/lobby.cpp src/online/content.cpp src/online/records.cpp src/online/agreement.cpp
  src/online/battle_setup.cpp src/online/state_channel.cpp
  src/online/state_battle.cpp src/online/state_robot.cpp src/online/state_arena.cpp
  src/online/state_scene.cpp src/online/state_world.cpp src/online/state_sections.cpp
  src/extract/sha256.cpp src/extract/sha256_x86.cpp)
target_link_libraries(phantomcrash_online PUBLIC phantomcrash_core)
target_include_directories(phantomcrash_online PRIVATE ${CGLTF_INCLUDE_DIRS})
if(WIN32)
  target_link_libraries(phantomcrash_online PRIVATE ws2_32 iphlpapi)
endif()
if(MSVC)
  target_compile_options(phantomcrash_online PRIVATE /W4 /permissive-)
else()
  target_compile_options(phantomcrash_online PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  add_executable(phantomcrash_online_asset_check tests/online_asset_check.cpp)
  target_link_libraries(phantomcrash_online_asset_check PRIVATE phantomcrash_online)
  add_executable(phantomcrash_online_tests tests/core_tests.cpp tests/online_input_tests.cpp
    tests/online_socket_tests.cpp tests/online_discovery_tests.cpp
    tests/online_prediction_tests.cpp tests/online_peer_tests.cpp
    tests/online_battle_tests.cpp tests/online_room_tests.cpp tests/online_wreck_tests.cpp
    tests/online_lobby_tests.cpp tests/online_content_tests.cpp tests/online_startup_tests.cpp
    tests/online_state_tests.cpp tests/online_state_channel_tests.cpp
    tests/online_input_loss_tests.cpp)
  target_link_libraries(phantomcrash_online_tests PRIVATE phantomcrash_online)
  add_test(NAME phantomcrash_online_tests COMMAND phantomcrash_online_tests)
  set_tests_properties(phantomcrash_online_tests PROPERTIES TIMEOUT 60)
endif()

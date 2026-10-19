if(MSVC)
  add_compile_options(/MP /utf-8)
endif()
if(WIN32)
  add_compile_definitions(NOMINMAX)
endif()

# Platform-independent core: data loading, input model, timing, canvas maths.
add_library(phantomcrash_core
  src/result_presentation.cpp
  src/match_result.cpp
  src/location_menu.cpp
  src/location_menu_script.cpp
  src/data_roots.cpp
  src/added_chips.cpp
  src/slai_content.cpp
  src/event_player.cpp
  src/frame_rate.cpp
  src/front_end.cpp
  src/front_end_script.cpp
  src/glyph_font.cpp
  src/input.cpp
  src/intro_sequence.cpp
  src/game_data.cpp
  src/game_text.cpp
  src/json.cpp
  src/json_access.cpp
  src/json_classes.cpp
  src/jukebox.cpp
  src/location_frame.cpp
  src/music_spectrum.cpp
  src/battle.cpp
  src/battle_pose.cpp
  src/robot_combat.cpp
  src/weapon_mounts.cpp
  src/pose_transform.cpp
  src/battle_damage.cpp
  src/battle_camouflage.cpp
  src/shown_armour.cpp
  src/battle_death.cpp
  src/battle_wrecks.cpp
  src/robot_wrecks.cpp
  src/entry_shield.cpp
  src/effect_mixer.cpp
  src/source_sound.cpp
  src/world_sound.cpp
  src/frame_stats.cpp
  src/battle_sounds.cpp
  src/battle_sounds_weapons.cpp
  src/battle_sounds_pairs.cpp
  src/battle_sounds_sustained.cpp
  src/battle_sounds_pilot.cpp
  src/battle_chatter.cpp
  src/battle_scene.cpp
  src/battle_scene_robots.cpp
  src/battle_scene_defeat.cpp
  src/battle_scene_tables.cpp
  src/robot_load.cpp
  src/robot_ai.cpp
  src/robot_ai_data.cpp
  src/battle_ai_pickups.cpp
  src/battle_ai_navigation.cpp
  src/battle_ai_steering.cpp
  src/battle_ai_vantage.cpp
  src/battle_ai_reach.cpp
  src/battle_ai_jump.cpp
  src/battle_ai_weapons.cpp
  src/battle_ai_reactions.cpp
  src/battle_ai.cpp
  src/battle_fire.cpp
  src/battle_targeting.cpp
  src/battle_roster.cpp
  src/battle_weapon_tuning.cpp
  src/battle_beams.cpp
  src/battle_beam_trace.cpp
  src/battle_weapons.cpp
  src/battle_weapon_targets.cpp
  src/fence_impacts.cpp
  src/collision_geometry.cpp
  src/nav_graph_build.cpp
  src/nav_tuning.cpp
  src/nav_graph_search.cpp
  src/stage_collision.cpp
  src/stage_fixes.cpp
    src/stage_play_area.cpp
  src/stage_doors.cpp
  src/stage_objects.cpp
  src/battle_objects.cpp
  src/battle_hud_style.cpp
  src/battle_hud_state.cpp
  src/battle_panels_style.cpp
  src/button_config.cpp
  src/battle_camera.cpp
  src/battle_monitor.cpp
  src/chase_camera.cpp
  src/cockpit.cpp
  src/optical_camouflage.cpp
  src/robot_shadow.cpp
  src/effect_bank.cpp
  src/effect_pictures.cpp
  src/effect_flashes.cpp
  src/effect_particles.cpp
  src/effect_secondary.cpp
  src/effect_system.cpp
  src/effect_glow.cpp
  src/shadow_volume.cpp
  src/supply_drop.cpp
  src/supply_special.cpp
  src/supply_rescue.cpp
  src/supply_settings.cpp
  src/battle_supply.cpp
  src/battle_watch.cpp
  src/split_screen.cpp
  src/sleep.cpp
  src/cheats.cpp
  src/contests.cpp
  src/schedule.cpp
  src/schedule_script.cpp
  src/schedule_skip.cpp
  src/day_skip.cpp
  src/contest_info.cpp
  src/rumbling_rules.cpp
  src/rumbling_script.cpp
  src/robot_preview.cpp
  src/rumble.cpp
  src/rumbling.cpp
  src/rumbling_gates.cpp
  src/rumbling_map.cpp
  src/rumbling_robot.cpp
  src/rumbling_world.cpp
  src/rumbling_progression.cpp
  src/rumbling_world_coop.cpp
  src/rumbling_world_result.cpp
  src/rumbling_roster.cpp
  src/rumbling_battle.cpp
  src/vs_scoobee.cpp
  src/saved_time.cpp
  src/keep_sv_script.cpp
  src/keep_sv.cpp
  src/card_row.cpp
  src/vs_mode_script.cpp
  src/vs_mode.cpp
  src/vs_mode_input.cpp
  src/vs_mode_area.cpp
  src/vs_battle.cpp
  src/vs_rules.cpp
  src/finale.cpp
  src/finale_data.cpp
  src/movie_subtitles.cpp
  src/town_screen.cpp
  src/records.cpp
  src/records_script.cpp
  src/battle_music.cpp
  src/battle_music_script.cpp
  src/shop_dialog.cpp
  src/sonic_amp.cpp
  src/sonic_amp_pointer.cpp
  src/sonic_amp_script.cpp
  src/garage.cpp
  src/garage_tables.cpp
  src/dealer_script.cpp
  src/dealer.cpp
  src/dealer_pointer.cpp
  src/sv_purchase.cpp
  src/sv_purchase_pointer.cpp
  src/used_sv_script.cpp
  src/used_sv.cpp
  src/used_sv_pointer.cpp
  src/chip_shop_script.cpp
  src/chip_spec_rows.cpp
  src/chip_shop.cpp
  src/chip_shop_pointer.cpp
  src/chip_uplift.cpp
  src/chip_shop_browse.cpp
  src/chip_shop_bosses.cpp
  src/part_stats.cpp
  src/extra_stats.cpp
  src/shop_goods.cpp
  src/sort_hints.cpp
  src/module_shop_script.cpp
  src/shop_list_style.cpp
  src/tune_shop_script.cpp
  src/tune_shop.cpp
  src/tune_shop_pointer.cpp
  src/hangar_menu_script.cpp
  src/hangar_menu.cpp
  src/hangar_menu_pointer.cpp
  src/assemble_script.cpp
  src/assemble_fitting.cpp
  src/assemble.cpp
  src/assemble_list.cpp
  src/assemble_details.cpp
  src/assemble_pointer.cpp
  src/sv_spec.cpp
  src/sv_spec_script.cpp
  src/test_drive.cpp
  src/robot_totals.cpp
  src/module_shop.cpp
  src/module_shop_details.cpp
  src/module_shop_pointer.cpp
  src/music_library.cpp
  src/music_program.cpp
  src/enemy_path.cpp
  src/menu_cursor.cpp
  src/level_control.cpp
  src/menu_screen.cpp
  src/message_box.cpp
  src/motion.cpp
  src/name_entry.cpp
  src/name_entry_keyboard.cpp
  src/name_entry_script.cpp
  src/option_menu.cpp
  src/option_script.cpp
  src/pepper_guide.cpp
  src/pepper_guide_script.cpp
  src/load_game.cpp
  src/load_game_script.cpp
  src/town_map.cpp
  src/town_map_script.cpp
  src/robot.cpp
  src/robot_skin.cpp
  src/robot_body_models.cpp
  src/robot_library.cpp
  src/robot_mover.cpp
  src/robot_root_motion.cpp
  src/movement_tuning.cpp
  src/robot_wheels.cpp
  src/robot_air.cpp
  src/robot_strike.cpp
  src/robot_dodge.cpp
  src/robot_dash_turn.cpp
  src/robot_twist.cpp
  src/movement_mounts.cpp
  src/pose_contacts.cpp
  src/robot_parts.cpp
  src/robot_armour.cpp
  src/weapon_reach.cpp
  src/screen_adjust.cpp
  src/post_process.cpp
  src/settings.cpp
  src/host_controls.cpp
  src/quest_json.cpp
  src/skeleton.cpp
  src/math_rotations.cpp
  src/motion_source.cpp
  src/sound_config.cpp
  src/sprite_layer.cpp
  src/sprite_lines.cpp
  src/sprite_shapes.cpp
  src/sprite_text.cpp
  src/story_event_script.cpp
  src/story_progression.cpp
  src/training_battle.cpp
  src/training_ground.cpp
  src/tutorial.cpp
  src/version.cpp
  src/world_model.cpp
  src/world_model_stages.cpp
  src/rigid_animation.cpp
  src/rigid_playback.cpp
  src/rigid_source_playback.cpp
  src/source_leg_playback.cpp
  src/source_pmt.cpp
  src/source_suspension.cpp
  src/source_roll.cpp
  src/source_lighting.cpp
  src/source_particles.cpp
  src/source_impact_attachment.cpp
  src/source_effect_slots.cpp
  src/source_weapon_actions.cpp
  src/source_motion_actions.cpp
  src/source_particle_birth.cpp
)

# cgltf (header only) reads the converted glTF models.
find_path(CGLTF_INCLUDE_DIRS cgltf.h REQUIRED)

target_include_directories(phantomcrash_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CGLTF_INCLUDE_DIRS}
)

target_compile_features(phantomcrash_core PUBLIC cxx_std_20)

# The port's version is the manifest's, so the window title matches the release.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/vcpkg.json")
file(READ "${PROJECT_SOURCE_DIR}/vcpkg.json" PHANTOMCRASH_MANIFEST)
string(JSON PHANTOMCRASH_VERSION GET "${PHANTOMCRASH_MANIFEST}" version-string)
set_source_files_properties(src/version.cpp PROPERTIES
  COMPILE_DEFINITIONS "PHANTOMCRASH_VERSION=\"${PHANTOMCRASH_VERSION}\"")

if(MSVC)
  target_compile_options(phantomcrash_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(phantomcrash_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(online.cmake)

if(BUILD_TESTING)
  add_executable(phantomcrash_core_tests
    tests/match_result_tests.cpp
    tests/club_tests.cpp
    tests/core_tests.cpp
    tests/data_tests.cpp
    tests/event_player_tests.cpp
    tests/guide_tests.cpp
    tests/front_end_tests.cpp
    tests/host_tests.cpp
    tests/host_controls_tests.cpp
    tests/player_locomotion_tests.cpp
    tests/robot_preview_tests.cpp
    tests/rumble_tests.cpp
    tests/intro_tests.cpp
    tests/load_game_tests.cpp
    tests/music_spectrum_tests.cpp
    tests/town_map_tests.cpp
    tests/name_entry_tests.cpp
    tests/option_screen_tests.cpp
    tests/sprite_lines_tests.cpp
    tests/option_tests.cpp
    tests/battle_tests.cpp
    tests/battle_ai_tests.cpp
    tests/armory_tests.cpp
    tests/battle_damage_tests.cpp
    tests/beam_charge_tests.cpp
    tests/battle_wreck_tests.cpp
    tests/battle_chatter_tests.cpp
    tests/battle_scene_tests.cpp
    tests/battle_scene_defeat_tests.cpp
    tests/battle_scene_robots_tests.cpp
    tests/world_sound_tests.cpp
    tests/battle_sounds_tests.cpp
    tests/robot_load_tests.cpp
    tests/robot_wheels_tests.cpp
    tests/stage_collision_tests.cpp
    tests/nav_graph_tests.cpp
    tests/nav_recovery_tests.cpp
    tests/stage_doors_tests.cpp
    tests/stage_objects_tests.cpp
    tests/fence_impacts_tests.cpp
    tests/battle_monitor_tests.cpp
    tests/battle_camera_tests.cpp
    tests/cockpit_tests.cpp
    tests/optical_camouflage_tests.cpp
    tests/supply_drop_tests.cpp
    tests/supply_special_tests.cpp
    tests/supply_rescue_tests.cpp
    tests/supply_destruction_tests.cpp
    tests/split_screen_tests.cpp
    tests/garage_tests.cpp
    tests/town_screen_tests.cpp
    tests/day_skip_tests.cpp
    tests/json_classes_tests.cpp
    tests/records_tests.cpp
    tests/battle_music_tests.cpp
    tests/sonic_amp_tests.cpp
    tests/sonic_amp_import_tests.cpp
    tests/garage_stock_tests.cpp
    tests/cheats_tests.cpp
    tests/dealer_tests.cpp
    tests/used_sv_tests.cpp
    tests/chip_shop_tests.cpp
    tests/chip_spec_rows_tests.cpp
    tests/chip_uplift_tests.cpp
    tests/tune_shop_tests.cpp
    tests/hangar_menu_tests.cpp
    tests/assemble_tests.cpp
    tests/sv_spec_tests.cpp
    tests/test_drive_tests.cpp
    tests/rumbling_tests.cpp
    tests/rumbling_live_tests.cpp
    tests/rumbling_progression_tests.cpp
    tests/rumbling_roster_tests.cpp
    tests/keep_sv_tests.cpp
    tests/vs_mode_tests.cpp
    tests/vs_rules_tests.cpp
    tests/finale_tests.cpp
    tests/stock_module_tests.cpp
    tests/module_shop_tests.cpp
    tests/system_menu_tests.cpp
    tests/battle_hud_style_tests.cpp
    tests/robot_tests.cpp
    tests/robot_combat_tests.cpp
    tests/robot_parts_tests.cpp
    tests/added_chips_tests.cpp
    tests/weapon_reach_tests.cpp
    tests/tutorial_tests.cpp
    tests/post_process_tests.cpp
    tests/world_tests.cpp
    tests/shadow_volume_tests.cpp
    tests/effect_tests.cpp
    tests/effect_cluster_tests.cpp
  )
  target_link_libraries(phantomcrash_core_tests PRIVATE phantomcrash_core)
  # The shipped data/ documents are part of the source tree and tested too.
  target_compile_definitions(phantomcrash_core_tests
    PRIVATE PHANTOMCRASH_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
  if(MSVC)
    target_compile_options(phantomcrash_core_tests PRIVATE /W4 /permissive-)
  endif()
  add_test(NAME phantomcrash_core_tests COMMAND phantomcrash_core_tests)

  add_executable(phantomcrash_slai_tests tests/core_tests.cpp tests/slai_native_tests.cpp tests/slai_playback_tests.cpp
    tests/robot_wheels_tests.cpp tests/sonic_amp_import_tests.cpp tests/slai_assembly_tests.cpp
    tests/slai_lighting_tests.cpp tests/slai_body_bank_tests.cpp tests/source_sound_tests.cpp
    tests/source_particle_atlas_tests.cpp tests/source_particle_motion_tests.cpp tests/source_particle_birth_tests.cpp tests/source_particle_parent_tests.cpp tests/source_fire_pair_tests.cpp tests/source_sustained_sound_tests.cpp tests/source_melee_tests.cpp
    tests/source_weapon_actions_tests.cpp
    tests/source_motion_actions_tests.cpp
    tests/source_rigid_transition_tests.cpp
    tests/source_leg_playback_tests.cpp
    tests/source_pmt_tests.cpp
    tests/rigid_source_playback_tests.cpp
    tests/source_effect_slots_tests.cpp
    tests/slai_supply_tests.cpp
    tests/slai_cheat_tests.cpp
    tests/slai_leg_mount_tests.cpp
    tests/source_suspension_tests.cpp
    tests/source_roll_tests.cpp
    tests/world_sound_tests.cpp tests/battle_sounds_tests.cpp tests/armory_tests.cpp tests/shop_label_tests.cpp)
  target_link_libraries(phantomcrash_slai_tests PRIVATE phantomcrash_core)
  target_compile_definitions(phantomcrash_slai_tests
    PRIVATE PHANTOMCRASH_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
  add_test(NAME phantomcrash_slai_tests COMMAND phantomcrash_slai_tests)
endif()

# Headless AI battle simulator: a research tool that runs the live RUMBLING's
# computer robots without a window and records what their AI does.
add_executable(phantomcrash_ai_sim
  src/ai_sim/ai_sim_main.cpp
  src/ai_sim/ai_sim_navdump.cpp
  src/ai_sim/ai_sim_traps.cpp
  src/ai_sim/ai_sim_output.cpp
  src/ai_sim/ai_sim_probe.cpp
  src/ai_sim/ai_sim_probe_combat.cpp
  src/ai_sim/ai_sim_probe_behaviour.cpp
  src/ai_sim/ai_sim_watch.cpp
  src/ai_sim/ai_sim_world.cpp)
target_link_libraries(phantomcrash_ai_sim PRIVATE phantomcrash_core)
if(MSVC)
  target_compile_options(phantomcrash_ai_sim PRIVATE /W4 /permissive-)
endif()

if(PHANTOMCRASH_BUILD_DESKTOP)
  find_package(SDL3 CONFIG REQUIRED)
  find_package(SDL3_image CONFIG REQUIRED)
  find_package(ZLIB REQUIRED)
  find_package(libdeflate CONFIG REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswresample
    libswscale
  )

  # FFmpeg movie decoding without SDL, shared by the port and the media test.
  add_library(phantomcrash_media STATIC src/movie_decoder.cpp src/music_decoder.cpp)
  target_include_directories(phantomcrash_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(phantomcrash_media PUBLIC phantomcrash_core PkgConfig::FFMPEG)
  if(MSVC)
    target_compile_options(phantomcrash_media PRIVATE /W4 /permissive-)
  endif()

  add_executable(phantomcrash_slai_audio src/slai_audio_convert.cpp)
  target_link_libraries(phantomcrash_slai_audio PRIVATE PkgConfig::FFMPEG)
  target_compile_features(phantomcrash_slai_audio PRIVATE cxx_std_20)

  if(BUILD_TESTING)
    add_executable(phantomcrash_music_tests tests/core_tests.cpp tests/music_decoder_tests.cpp)
    target_link_libraries(phantomcrash_music_tests PRIVATE phantomcrash_media)
    add_test(NAME phantomcrash_music_tests COMMAND phantomcrash_music_tests)
  endif()

  # Disc extraction and asset conversion (the C++ form of the Python
  # research tools), shared by the command-line extractor and the port.
  add_library(phantomcrash_extractor STATIC
    src/extract/audio.cpp
    src/extract/battle_data.cpp
    src/extract/battle_scenes.cpp
    src/extract/binary.cpp
    src/extract/binlink_packs.cpp
    src/extract/convert.cpp
    src/extract/convert_tables.cpp
    src/extract/effect_bank.cpp
    src/extract/effect_convert.cpp
    src/extract/effect_pictures.cpp
    src/extract/event_code.cpp
    src/extract/events.cpp
    src/extract/extract.cpp
    src/extract/finale.cpp
    src/extract/game_tables.cpp
    src/extract/game_text_table.cpp
    src/extract/garage_tables.cpp
    src/extract/gltf.cpp
    src/extract/gltf_writer.cpp
    src/extract/install.cpp
    src/extract/model.cpp
    src/extract/motion.cpp
    src/extract/parts.cpp
    src/extract/png_write.cpp
    src/extract/py_json.cpp
    src/extract/robot_convert.cpp
    src/extract/robot_mounts.cpp
    src/extract/robots.cpp
    src/extract/sha256.cpp
    src/extract/sha256_x86.cpp
    src/extract/stage_convert.cpp
    src/extract/stage_objects.cpp
    src/extract/text.cpp
    src/extract/town_tables.cpp
    src/extract/xbe.cpp
    src/extract/xbox_texture.cpp
    src/extract/xdvdfs.cpp
  )
  target_include_directories(phantomcrash_extractor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/extract)
  target_link_libraries(phantomcrash_extractor PUBLIC phantomcrash_core ZLIB::ZLIB
                        libdeflate::libdeflate_shared)
  if(WIN32)
    target_link_libraries(phantomcrash_extractor PUBLIC Normaliz)
  endif()
  if(MSVC)
    target_compile_options(phantomcrash_extractor PRIVATE /W4 /permissive-)
  endif()

  add_executable(phantomcrash_extract src/extract/extract_main.cpp)
  target_link_libraries(phantomcrash_extract PRIVATE phantomcrash_extractor)
  if(MSVC)
    target_compile_options(phantomcrash_extract PRIVATE /W4 /permissive-)
  endif()

  if(BUILD_TESTING)
    add_executable(phantomcrash_extract_tests tests/core_tests.cpp tests/extract_tests.cpp)
    target_link_libraries(phantomcrash_extract_tests PRIVATE phantomcrash_extractor)
    target_include_directories(phantomcrash_extract_tests PRIVATE tests)
    if(MSVC)
      target_compile_options(phantomcrash_extract_tests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME phantomcrash_extract_tests COMMAND phantomcrash_extract_tests)
  endif()

  add_executable(phantomcrash_port WIN32
    src/desktop_audio.cpp
    src/desktop_audio_effects.cpp
    src/desktop_source_audio.cpp
    src/desktop_battle_hud.cpp
    src/desktop_battle_defeats.cpp
    src/desktop_battle_sights.cpp
    src/desktop_battle_panels.cpp
    src/desktop_battle_panel_text.cpp
    src/desktop_sprite_lines.cpp
    src/desktop_sprite_shapes.cpp
    src/desktop_sprite_text.cpp
    src/desktop_display_text.cpp
    src/desktop_textures_erase.cpp
    src/desktop_battle_chatter.cpp
    src/desktop_battle_scene.cpp
    src/desktop_battle_view.cpp
    src/desktop_battle_wrecks.cpp
    src/desktop_wreck_collision.cpp
    src/desktop_location_menu.cpp
    src/desktop_location_pointer.cpp
    src/desktop_texture_hit.cpp
    src/desktop_draw_list.cpp
    src/desktop_event_text.cpp
    src/desktop_frame.cpp
    src/desktop_frame_counter.cpp
    src/desktop_frame_rate_dialog.cpp
    src/desktop_front_end.cpp
    src/desktop_game_data.cpp
    src/desktop_slai.cpp
    src/desktop_menu_buttons.cpp
    src/desktop_host.cpp
    src/desktop_input.cpp
    src/desktop_input_bindings.cpp
    src/desktop_audio_menu.cpp
    src/desktop_cheats_menu.cpp
    src/desktop_controls_dialog.cpp
    src/desktop_intro.cpp
    src/desktop_main.cpp
    src/desktop_scene_dump.cpp
    src/desktop_name_entry.cpp
    src/desktop_option.cpp
    src/desktop_option_screens.cpp
    src/desktop_pepper_guide.cpp
    src/desktop_load_game.cpp
    src/desktop_town_map.cpp
    src/desktop_options.cpp
    src/desktop_screens.cpp
    src/desktop_screens_dump.cpp
    src/desktop_screens_coop.cpp
    src/desktop_screens_coop_battle.cpp
    src/desktop_screens_coop_save.cpp
    src/desktop_coop.cpp
    src/desktop_coop_storage.cpp
    src/desktop_coop_online.cpp
    src/desktop_coop_discovery.cpp
    src/desktop_coop_address.cpp
    src/desktop_debug_log.cpp
    src/desktop_live_resize.cpp
    src/desktop_coop_draw.cpp
    src/desktop_coop_lobby.cpp
    src/desktop_coop_art.cpp
    src/desktop_coop_cursors.cpp
    src/desktop_coop_matches.cpp
    src/desktop_coop_checkpoint.cpp
    src/desktop_online_match.cpp
    src/desktop_online_session.cpp
    src/desktop_online_presentation.cpp src/desktop_training_presentation.cpp
    src/desktop_online_change.cpp src/desktop_online_exit.cpp
    src/desktop_online_resources.cpp
    src/desktop_online_factory.cpp
    src/desktop_online_join.cpp
    src/desktop_online_game.cpp
    src/desktop_online_barrier.cpp
    src/desktop_online_history.cpp src/desktop_online_recovery.cpp
    src/desktop_online_factory_check.cpp
    src/desktop_online_game_check.cpp
    src/desktop_online_recovery_check.cpp
    src/desktop_online_state.cpp
    src/desktop_online_player.cpp
    src/desktop_training_online.cpp
    src/desktop_online_input_check.cpp
    src/desktop_training_tick.cpp
    src/desktop_online_assets.cpp
    src/desktop_training_coop.cpp
    src/desktop_training_backdrop.cpp
    src/desktop_training_build.cpp
    src/desktop_training_dump.cpp
    src/desktop_screens_rumbling.cpp
    src/desktop_screens_music.cpp
    src/desktop_screens_finale.cpp
    src/desktop_screens_overlay.cpp
    src/desktop_screens_menus.cpp
    src/desktop_screens_battle.cpp
    src/desktop_settings_menu.cpp
    src/desktop_copy_button.cpp
    src/desktop_window_title.cpp
    src/desktop_talk.cpp
    src/desktop_text.cpp
    src/desktop_textures.cpp
    src/desktop_ui_edges.cpp
    src/desktop_robot.cpp
    src/desktop_training.cpp
    src/desktop_training_pause.cpp
    src/desktop_training_result.cpp
    src/desktop_training_replay.cpp
    src/desktop_replay.cpp
    src/desktop_replay_archive.cpp
    src/desktop_result_backdrop.cpp
    src/desktop_result_trophy.cpp
    src/desktop_replay_overlay.cpp
    src/desktop_match_result.cpp
    src/desktop_screens_result.cpp
    src/desktop_screens_attract.cpp
    src/desktop_attract_demo.cpp
    src/desktop_screens_story.cpp
    src/desktop_screens_cheats.cpp
    src/desktop_training_sound.cpp
    src/desktop_training_camera.cpp
    src/desktop_training_defeat.cpp
    src/desktop_training_live.cpp
    src/desktop_training_split.cpp
    src/desktop_training_finale.cpp
    src/desktop_training_vs.cpp
    src/desktop_monitor_ship.cpp
    src/desktop_stage_objects.cpp
    src/desktop_game_screens.cpp
    src/desktop_records.cpp
    src/desktop_battle_music.cpp
    src/desktop_shop_dialog.cpp
    src/desktop_sonic_amp.cpp
    src/desktop_dealer.cpp
    src/desktop_sv_purchase.cpp
    src/desktop_used_sv.cpp
    src/desktop_chip_shop.cpp
    src/desktop_module_shop.cpp
    src/desktop_shop_list.cpp
    src/desktop_tune_shop.cpp
    src/desktop_hangar_menu.cpp
    src/desktop_assemble.cpp
    src/desktop_assemble_list.cpp
    src/desktop_robot_preview.cpp
    src/desktop_robot_shot.cpp
    src/desktop_tools.cpp
    src/desktop_online_resource_check.cpp
    src/desktop_online_stress_check.cpp
    src/desktop_sv_spec.cpp
    src/desktop_part_stats.cpp
    src/desktop_extra_stats.cpp
    src/desktop_chip_dialog.cpp
    src/desktop_chip_spec_rows.cpp
    src/desktop_chip_compare.cpp
    src/desktop_records_convention.cpp
    src/desktop_records_trophies.cpp
    src/desktop_schedule.cpp
    src/desktop_contest_info.cpp
    src/desktop_rumbling.cpp
    src/desktop_rumbling_robots.cpp
    src/desktop_keep_sv.cpp
    src/desktop_vs_mode.cpp
    src/desktop_vs_mode_area.cpp
    src/desktop_game_screens_vs.cpp
    src/desktop_town_screen.cpp
    src/desktop_cockpit.cpp
    src/desktop_supply.cpp
    src/desktop_supply_kinds.cpp
    src/desktop_effects.cpp
    src/desktop_source_particles.cpp
    src/desktop_source_projectiles.cpp
    src/desktop_source_weapon_actions.cpp
    src/desktop_source_model_actions.cpp
    src/desktop_shots.cpp
    src/desktop_mech_vfx.cpp
    src/mech_vfx_data.cpp
    src/desktop_movement_effects.cpp
    src/desktop_mount_effects.cpp
    src/desktop_foot_effects.cpp
    src/desktop_shot_residue.cpp
    src/desktop_training_robots.cpp
    src/desktop_world_gpu.cpp
    src/desktop_world_passes.cpp
    src/desktop_world_monitor.cpp
    src/desktop_dynamic_lights.cpp
    src/desktop_online_diagnostics.cpp
    src/desktop_sun_shadows.cpp
    src/desktop_volumetric_lights.cpp
    src/desktop_light_sources.cpp
    src/desktop_point_lights.cpp
    src/desktop_world_lights.cpp
    src/desktop_visuals_dialog.cpp
    src/desktop_coop_dialog.cpp
    src/desktop_coop_rewards.cpp
    src/desktop_world_shadows.cpp
    src/desktop_world_screen.cpp
    src/desktop_pause_menu.cpp
    src/desktop_pause_roster.cpp
    src/desktop_sharp_text.cpp
    src/desktop_smaa.cpp
    src/desktop_world_sprites.cpp
    src/desktop_sprite_vertices.cpp
    src/desktop_world_renderer.cpp
    src/desktop_world_shaders.cpp
    src/movie_audio_output.cpp
    src/movie_player.cpp
  )
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl" SMAA_SOURCE)
  # Keep each literal below MSVC's per-token limit without editing upstream HLSL.
  string(REPLACE "\n" "\n)smaa\"\nR\"smaa(" SMAA_SOURCE "${SMAA_SOURCE}")
  configure_file(src/desktop_smaa_shaders.hpp.in generated/desktop_smaa_shaders.hpp @ONLY)
  target_include_directories(phantomcrash_port PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(phantomcrash_port PRIVATE
    phantomcrash_online phantomcrash_media SDL3::SDL3 SDL3_image::SDL3_image ZLIB::ZLIB)
  # The port runs the extractor beside it to install or update the game data.
  add_dependencies(phantomcrash_port phantomcrash_extract)
  if(WIN32)
    target_link_libraries(phantomcrash_port PRIVATE comctl32 d3d11 dxgi d3dcompiler gdiplus)

    # The executable icon (port/res, built by tools/make_icon.py).
    enable_language(RC)
    target_sources(phantomcrash_port PRIVATE res/phantomcrash_port.rc)
  endif()
  if(MSVC)
    target_compile_options(phantomcrash_port PRIVATE /W4 /permissive-)
  endif()

  if(BUILD_TESTING)
    add_executable(phantomcrash_coop_tests tests/core_tests.cpp tests/coop_settings_tests.cpp
      tests/online_local_input_tests.cpp tests/online_result_tests.cpp
      tests/coop_checkpoint_tests.cpp tests/coop_storage_tests.cpp src/desktop_coop_storage.cpp
      src/desktop_coop_matches.cpp src/desktop_coop_rewards.cpp src/desktop_coop_checkpoint.cpp
      src/desktop_online_match.cpp src/desktop_online_session.cpp src/desktop_online_state.cpp
      src/desktop_online_presentation.cpp)
    target_link_libraries(phantomcrash_coop_tests PRIVATE phantomcrash_online SDL3::SDL3)
    target_compile_definitions(phantomcrash_coop_tests PRIVATE
      PHANTOMCRASH_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
    add_test(NAME phantomcrash_coop_tests COMMAND phantomcrash_coop_tests)
    add_executable(phantomcrash_input_tests tests/core_tests.cpp tests/desktop_input_tests.cpp
      tests/desktop_replay_archive_tests.cpp tests/desktop_sprite_vertices_tests.cpp
      tests/scene_dump_text_tests.cpp
      src/desktop_replay_archive.cpp src/desktop_sprite_vertices.cpp
      src/desktop_input.cpp src/desktop_input_bindings.cpp)
    target_link_libraries(phantomcrash_input_tests PRIVATE phantomcrash_core SDL3::SDL3 ZLIB::ZLIB)
    add_test(NAME phantomcrash_input_tests COMMAND phantomcrash_input_tests)
    add_test(NAME phantomcrash_port_smoke COMMAND phantomcrash_port --smoke-test 60
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # Title, menu, submenu and a placeholder screen, driven by injected presses.
    add_test(NAME phantomcrash_port_menu_smoke
             COMMAND phantomcrash_port --start title --smoke-test 500 --press 150:start
                     --press 200:down --press 230:a --press 260:up --press 290:a
                     --press 320:a --press 450:b
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # The tutorial battle: Pepper's first pages, then walking, firing and back to Pepper.
    add_test(NAME phantomcrash_port_training_smoke
             COMMAND phantomcrash_port --start training_attack --smoke-test 400 --press 60:a
                     --press 120:a --press 180:a --press 200-300:up --press 250-320:right_trigger
                     --press 380:back
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # The option screen: the vibration box answered NO, cancel to EXIT, back to the menu.
    add_test(NAME phantomcrash_port_option_smoke
             COMMAND phantomcrash_port --start option --smoke-test 300 --press 60:a
                     --press 90:right --press 120:a --press 150:b --press 180:a
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # The option sub-screens: SOUND CONFIG (a volume step, the SE test, EXIT),
    # BUTTON CONFIG (a choice, EXIT) and SCREEN ADJUST (a step, answered NO).
    add_test(NAME phantomcrash_port_option_screens_smoke
             COMMAND phantomcrash_port --start option --smoke-test 700 --press 50:down
                     --press 60:a --press 140:left --press 150:down --press 160:down
                     --press 170:a --press 180:down --press 190:a --press 260:down
                     --press 270:a --press 340:right --press 350:up --press 360:a
                     --press 430:down --press 440:a --press 510:right --press 520:a
                     --press 530:a
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # NEW GAME: the opening movie skipped, event 48 fast-forwarded, a name
    # typed on NAME ENTRY, End, event 49 fast-forwarded, Pepper's guide
    # placeholder, and on to Club Wired.
    add_test(NAME phantomcrash_port_new_game_smoke
             COMMAND phantomcrash_port --start new_game --smoke-test 660 --press 20:a
                     --press 40-180:left_trigger --press 300:a --press 320:right
                     --press 330:a --press 350:b --press 370:a --press 460-620:left_trigger
                     --press 640:a
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # Club Wired: the wheel turned both ways, cancel to Exit, back to
    # Training, into Pepper's guide (event 129 first) and back.
    add_test(NAME phantomcrash_port_club_smoke
             COMMAND phantomcrash_port --start club_wired --smoke-test 700 --press 60:right
                     --press 90:left --press 120:b --press 150:right --press 180:right
                     --press 200:a --press 260-620:left_trigger --press 660:a
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # Pepper's guide on a cleared save riding a Scoobee (the RIDING SV plate):
    # into a topic, the cleared answer to "What is a Ranker?" (event 157)
    # fast-forwarded and back, the topic closed, cancel to End and out.
    add_test(NAME phantomcrash_port_guide_smoke
             COMMAND phantomcrash_port --start pepper_guide
                     --settings ${CMAKE_CURRENT_SOURCE_DIR}/tests/cleared_save.json
                     --smoke-test 720 --press 60:down --press 80:down --press 100:a
                     --press 120:down --press 130:down --press 140:a
                     --press 200-520:left_trigger --press 560:b --press 580:b --press 600:a
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # LOAD GAME with the cleared save in the first slot: the slots and EXIT
    # walked, cancel to EXIT, back up, the save chosen, its notice, the
    # garage placeholder. With no save, only the notice, and confirm leaves.
    add_test(NAME phantomcrash_port_load_smoke
             COMMAND phantomcrash_port --start load_game
                     --settings ${CMAKE_CURRENT_SOURCE_DIR}/tests/cleared_save.json
                     --smoke-test 400 --press 60:right --press 80:left --press 100:down
                     --press 120:up --press 140:b --press 160:up --press 180:a --press 260:a
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    add_test(NAME phantomcrash_port_load_empty_smoke
             COMMAND phantomcrash_port --start load_game
                     --settings ${CMAKE_CURRENT_BINARY_DIR}/no_such_settings.json
                     --smoke-test 200 --press 90:a
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # The garage with the cleared save: the wheel turned, the Y and X
    # shortcuts, cancel to the way out, then the hangar: its menu and back.
    add_test(NAME phantomcrash_port_garage_smoke
             COMMAND phantomcrash_port --start garage
                     --settings ${CMAKE_CURRENT_SOURCE_DIR}/tests/cleared_save.json
                     --smoke-test 320 --press 60:right --press 90:y --press 130:b
                     --press 170:x --press 210:a --press 280:a
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # The town map with the cleared save: round the places both ways, past
    # the ends, then into the garage.
    add_test(NAME phantomcrash_port_town_map_smoke
             COMMAND phantomcrash_port --start town_map
                     --settings ${CMAKE_CURRENT_SOURCE_DIR}/tests/cleared_save.json
                     --smoke-test 260 --press 60:left --press 80:left --press 100:right
                     --press 120:right --press 140:right --press 160:left --press 190:a
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # The town map by mouse: onto Wild Machines' number, a turn round the
    # rider, then a click on Sonic Amp's name.
    add_test(NAME phantomcrash_port_town_map_mouse_smoke
             COMMAND phantomcrash_port --start town_map
                     --settings ${CMAKE_CURRENT_SOURCE_DIR}/tests/cleared_save.json
                     --smoke-test 240 --mouse 60:120,130 --mouse 100:122,137 --mouse 104:142,157
                     --mouse 108:122,177 --mouse 112:102,157 --mouse 116:122,138
                     --mouse 180:420,340:click
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # The first talk scene on its own, its pages turned by presses.
    add_test(NAME phantomcrash_port_talk_smoke
             COMMAND phantomcrash_port --start event_48 --smoke-test 400 --press 60:a
                     --press 80:a --press 100:a --press 120:a --press 140:a --press 160:a
                     --press 180:a --press 200:a --press 220:a --press 240:a --press 260:a
                     --press 280:a --press 300:a
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # Silent audio, so test runs do not play sound.
    set_tests_properties(phantomcrash_port_smoke phantomcrash_port_menu_smoke
      phantomcrash_port_option_smoke phantomcrash_port_option_screens_smoke
      phantomcrash_port_new_game_smoke phantomcrash_port_club_smoke phantomcrash_port_talk_smoke
      phantomcrash_port_guide_smoke phantomcrash_port_load_smoke phantomcrash_port_load_empty_smoke
      phantomcrash_port_garage_smoke phantomcrash_port_town_map_smoke
      phantomcrash_port_town_map_mouse_smoke
      PROPERTIES
      TIMEOUT 60 ENVIRONMENT SDL_AUDIO_DRIVER=dummy)

    # Needs the movies extracted from the disc; reports a skip without them.
    add_executable(phantomcrash_media_tests tests/media_tests.cpp)
    target_link_libraries(phantomcrash_media_tests PRIVATE phantomcrash_media)
    target_compile_definitions(phantomcrash_media_tests
      PRIVATE PHANTOMCRASH_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
    if(MSVC)
      target_compile_options(phantomcrash_media_tests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME phantomcrash_media_tests COMMAND phantomcrash_media_tests)
    set_tests_properties(phantomcrash_media_tests PROPERTIES
      TIMEOUT 300 SKIP_RETURN_CODE 77 LABELS private-input)
  endif()
endif()

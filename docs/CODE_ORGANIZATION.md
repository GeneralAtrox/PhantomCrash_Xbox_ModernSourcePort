# Code organization

New and refactored handwritten source/header files have a 400-physical-line
limit. Format C++ using the repository `.clang-format` before counting.
Generated tables and map/content data are excluded. Split responsibilities
and ownership; do not compress statements or split a function across included
fragments.

The formatter inserts braces for control flow, keeps nonempty functions and
branches on multiple lines, and uses two-space indentation with a 100-column
target. Apply it before checking the 400-line limit:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe' -i port\src\*.cpp port\include\phantomcrash\*.hpp
```

## Layout

| Path | Contents |
|---|---|
| `src/phantomcrash_research/` | Python research package: XDVDFS reader, XBE parser, extraction, CLI |
| `src/phantomcrash_research/{xbox_texture,texture_pack,png,audio,game_tables,events,model,gltf,convert}.py` | Asset conversion: Xbox textures and packs, PNG, Xbox ADPCM and SE BIN banks, XBE tables, event scripts and talk characters, `_mdl.bin` models to glTF |
| `src/phantomcrash_research/{robots,motion,battle_data,collision,environment,parts,finale,robot_convert}.py` | Robot conversion (`convert-robots`): part tables and the catalogue, the AI chip and weapon records (`tables/parts.json`), leg and arm skeletons, motion banks, part models and paints from the binlink packs, Pepper's lesson messages, the enemy path, the stages' collision (read and written), their fog and light presets and the finale's phases (`tables/finale.json`) |
| `data/` | Editable port data (intro, title, menu, music, effects); paths resolve through `mods/` first |
| `tools/phantomcrash.py` | Source-checkout CLI entry point |
| `tools/sprite_{lettering,preview,ink}.py` | Sharp-UI authoring for `data/ui/sprite_text.json` and `sprite_shapes.json`: `add` merges a batch of classes and entries, `measure` fills regions and erase boxes from the art (`sprite_ink.py` finds the ink, rules and labels), `preview` draws check sheets of art beside replacement, `adopt` moves repeated keys onto classes |
| `port/src/{json_classes,sprite_text,sprite_shapes,sprite_lines}.cpp` | Sharp-UI data (no SDL): `json_classes` resolves an entry's `"class"` chain against the file's `classes`; the lettering, outline-shape and ruled-line tables parsed from `data/ui/` |
| `scripts/` | PowerShell entry points (extract, build); Ghidra scripts are in the shared `C:\Projects\Xbox\xbox-re-tools` |
| `port/include/phantomcrash/`, `port/src/` | C++20 modern port |
| `port/src/{json,json_access,data_roots,input,frame_rate,settings,intro_sequence,sprite_layer,menu_cursor,front_end,front_end_script,glyph_font,message_box,menu_screen,option_menu,option_script,sound_config,button_config,screen_adjust,name_entry,name_entry_script,story_event_script,event_player,location_frame,music_spectrum,jukebox,location_menu,location_menu_script,pepper_guide,pepper_guide_script,load_game,load_game_script,town_map,town_map_script,training_ground,world_model}.cpp` | Core library (no SDL): strict JSON, data roots and mod overlay, Xbox button model, frame pacer, saved settings and game options, intro step machine, sprite layers and draw-flag animations, cursor repeat, title/menu state machine, system font and tickers, message box, shared menu-screen parts (script basics, fades, help clocks), option screen and SOUND CONFIG / BUTTON CONFIG / SCREEN ADJUST state machines, NAME ENTRY and the quest save, story events (converted scripts, talk characters) and the event script player, the town frame (day, money, music meter, RIDING SV plate; the base layout Club Wired shares with the garage and Wild Machines), the meter's hearing (per-band FFT levels, each band against its own running average), the radio, the location menus (the wheel Club Wired and the garage share), Pepper's question list, LOAD GAME's slots, the town map (places, cursor, signs and the rider), the training ground's stages and lessons, and glTF model loading (cgltf) into the game's left-handed space; `math3d.hpp` has the row-vector matrices |
| `port/src/{robot,robot_library,skeleton,motion,robot_mover,enemy_path,stage_collision,battle,battle_fire,battle_weapons,robot_parts,battle_hud_style,battle_hud_state,battle_panels_style,tutorial,training_battle,chase_camera}.cpp` | The battle core (no SDL): robot parts and assemblies, skeleton poses with the leg IK and root motion, motion banks and animators, stride speeds, robot movement, the enemy path and its followers, the stage's collision, combatants, locks, firing, hits and the HUD's events, weapons, ammunition and projectiles, the original's AI chip and weapon records with their level curves, the battle HUD's pieces and layout, its flash, lock boxes, reticle tracking and message log, drones, the lesson steps and goals, the tutorial battle's data and controls, the chase camera |
| `port/src/desktop_world_{renderer,passes,gpu,shaders}.cpp`, `port/src/desktop_{robot,battle_view,battle_hud,battle_sights,battle_panels,event_text,training}.cpp` | The 3D world: Direct3D 11 on SDL's renderer device in its own device-context state, multisampled targets resolved into an SDL texture under the 2D, model and texture upload, sky / opaque / blended passes with per-item alpha and hit flash, run-time compiled HLSL; robots drawn from their poses, shot effects, the battle HUD with the original's sight, reticle, lock boxes, markers and panels, event text over the world, and the tutorial battle screen |
| `port/src/movie_decoder.cpp`, `port/include/phantomcrash/movie_clock.hpp` | Media library: FFmpeg movie decoding (Sofdec `.sfd` and anything FFmpeg reads) and the audio-clock timing helpers |
| `port/src/movie_{player,audio_output}.cpp` | SDL movie playback: YUV textures, audio stream, audio-clocked frame timing |
| `port/src/desktop_*.cpp` | SDL3 desktop host: window and D3D11 renderer, input mapping, Settings menu and frame-rate dialog (from Lodoss), frame counter, texture cache, glyph drawing, layered draw list, audio mixer (music, effects, volumes), screen runner (with the screen offset), intro / front-end / option / option sub-screen / name entry / talk / location menu (Club Wired, the garage) / Pepper's guide / LOAD GAME / town map / placeholder screens, the town frame drawing, main loop |
| `port/tests/` | CTest: core unit tests (self-registering harness), a movie decode test (private input, skips without the disc) and a hidden smoke run of the port |
| `tests/` | pytest suite; private-disc tests use the `private_inputs` marker |
| `docs/` | Format notes, research conclusions, tooling |

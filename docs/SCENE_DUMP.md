# Scene dump (copy game context)

Click the **copy icon** on the menu bar, next to Settings (tooltip "Copy game
context"), to describe what is on screen for an agent. In fullscreen, where
the menu bar is hidden, **F9** does the same. Nothing is shown when it copies.
The dump is written to `scene_dumps/` next to the settings file
(`%APPDATA%/PhantomCrash/PhantomCrash Port/scene_dumps/`), and the JSON is also
copied to the clipboard so it can be pasted straight into a chat. When Phangrade
started the game, the clipboard copy (not the file) ends with Phangrade's context
for this run: where its full logs, crash dumps and co-op partners' logs are.

Each dump writes three files named by the local date and time:

- `<stamp>.json`: the description below.
- `<stamp>.bmp`: the frame as drawn, without the frame counter.
- `<stamp>.settings.json`: the settings with the running game's save, so the
  port can start again from the same save with `--settings`.

## The window title

The title shows the port's name, the frame rate and, while a movie or a
scripted scene plays out, its name and frame of the total, for example
`Phantom Crash Port 1.0 | 144 FPS | Entrance (shot 2): frame 210 / 540`.

- Movies (intro, opening, ending): video frames.
- Battle scenes: the entrance (shots 1-4), a boss's entrance, the exit, a VS
  rider's walk out of its gate and the final battle's scenes, in the scene
  tables' 60 Hz frames, so they match the original's scene frame numbers.
- Story events: the event's command reached of its commands.

The dump's `scene.playing` and its `task` carry the same.

## The description

```json
{
  "task": "Screen rumbling_battle: battle on <stage>: RUMBLING contest, ... (entered from rumbling)",
  "problem": "",
  "how_to_reach": ["\"...phantomcrash_port.exe\" --settings \"...settings.json\" --start rumbling",
                   "In RUMBLING enter area 2, contest 5, gate 1",
                   "Screens shown, oldest first: garage > town_map > rumbling > rumbling_battle"],
  "kinds": ["rumbling", "free_play"],
  "scene": {"screen": "...", "trail": [], "quest": {}, "details": {}},
  "files": {"read": ["AGENTS.md", "..."], "edit": ["port/src/..."]},
  "capture": {"screenshot": "...", "settings": "...", "dump": "..."}
}
```

- **task**: a one-line summary built from the screen and its details.
- **problem**: left blank for you to describe what looks or behaves wrong.
- **how_to_reach**: the command that starts the port on the dumped save, then
  the steps to the scene. A battle starts from the screen that enters it
  (RUMBLING, VS MODE, the co-op lobby).
- **kinds**: what the battle screen is (`lesson`, `free_play`, `test_drive`,
  `rumbling`, `coop_match`, `vs`, `finale`, `scripted_scene`, `paused`).
- **scene**: the screen, the screens shown before it, the story event and
  RUMBLING entry, the save's player, day and money, and the screen's own
  details. Battles list the stage, clock, camera view, the entrance or exit
  scene's state and every robot (name, team, HP, movement mode, position,
  heading, scene state). Co-op lists each joined player's screen and save.
- **files**: what to read and which files the fix most likely belongs in.

## Choosing the files

`data/debug/scene_dump.json` maps screens to files. `read_always` comes first;
then every rule whose `screens` (exact names, or a prefix ending in `*`) or
`kinds` match adds its `read` and `edit` lists. A mod can replace it through
`mods/data/debug/scene_dump.json` (an overlay replaces arrays whole).

A screen adds more of its own state by overriding `Screen::scene_details()`
(`port/src/desktop_screen.hpp`); the battle's lives in
`port/src/desktop_training_dump.cpp`.

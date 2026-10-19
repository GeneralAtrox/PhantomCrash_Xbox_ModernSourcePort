#pragma once
namespace phantomcrash {
class DesktopHost;
class DataRoots;
class WorldRenderer;
class RobotLibrary;
struct CoopMatch;
struct TrainingScript;
void check_desktop_online_input(DesktopHost &host, const DataRoots &roots, WorldRenderer &world,
                                RobotLibrary &robots, CoopMatch &match,
                                const TrainingScript &training);
} // namespace phantomcrash

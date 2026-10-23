#ifndef FALLOUT_GAME_HOME_H_
#define FALLOUT_GAME_HOME_H_

namespace fallout {

// Makes the game run from a folder that has the game data, and returns `true`
// when it does. `bundleDirectory` is where an app bundle keeps its resources.
//
// The player's own copy of the game is never changed or copied. On first run
// the game looks for an installed copy, or asks for the folder, then builds a
// small home folder of links to those files with its own saves and settings.
bool game_home_enter(const char* bundleDirectory);

} // namespace fallout

#endif /* FALLOUT_GAME_HOME_H_ */

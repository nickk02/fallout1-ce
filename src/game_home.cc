#include "game_home.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL.h>

namespace fallout {

#define GAME_HOME_PATH_SIZE 1024

// Finds an entry in `directory` whose name matches `name` ignoring case. This
// copes with MASTER.DAT and master.dat. Writes the real name to `found`.
static bool find_entry(const char* directory, const char* name, char* found, size_t foundSize)
{
    DIR* dir = opendir(directory);
    if (dir == nullptr) {
        return false;
    }

    bool result = false;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcasecmp(entry->d_name, name) == 0) {
            snprintf(found, foundSize, "%s", entry->d_name);
            result = true;
            break;
        }
    }

    closedir(dir);
    return result;
}

// `true` if the folder has both main data files. A broken link does not count.
static bool has_game(const char* directory)
{
    const char* names[] = { "master.dat", "critter.dat" };
    for (const char* name : names) {
        char found[256];
        if (!find_entry(directory, name, found, sizeof(found))) {
            return false;
        }

        char path[GAME_HOME_PATH_SIZE];
        snprintf(path, sizeof(path), "%s/%s", directory, found);

        struct stat info;
        if (stat(path, &info) != 0) {
            return false;
        }
    }

    return true;
}

static bool make_directory(const char* path)
{
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

// Replaces whatever is at `link` with a link to `target`.
static void relink(const char* target, const char* link)
{
    struct stat info;
    if (lstat(link, &info) == 0 && S_ISLNK(info.st_mode)) {
        unlink(link);
    }

    symlink(target, link);
}

static void copy_file_if_missing(const char* from, const char* to)
{
    struct stat info;
    if (stat(to, &info) == 0) {
        return;
    }

    FILE* in = fopen(from, "rb");
    if (in == nullptr) {
        return;
    }

    FILE* out = fopen(to, "wb");
    if (out != nullptr) {
        char buffer[4096];
        size_t count;
        while ((count = fread(buffer, 1, sizeof(buffer), in)) > 0) {
            fwrite(buffer, 1, count, out);
        }
        fclose(out);
    }

    fclose(in);
}

// Builds the home folder. The data files and art are links into the player's
// copy. Saves and settings are kept in the home folder.
static bool link_install(const char* install, const char* home)
{
    if (!make_directory(home)) {
        return false;
    }

    const char* files[] = { "master.dat", "critter.dat" };
    for (const char* file : files) {
        char name[256];
        if (!find_entry(install, file, name, sizeof(name))) {
            return false;
        }

        char target[GAME_HOME_PATH_SIZE];
        char link[GAME_HOME_PATH_SIZE];
        snprintf(target, sizeof(target), "%s/%s", install, name);
        snprintf(link, sizeof(link), "%s/%s", home, name);
        relink(target, link);
    }

    char dataName[256];
    char dataDirectory[GAME_HOME_PATH_SIZE];
    char homeData[GAME_HOME_PATH_SIZE];
    snprintf(homeData, sizeof(homeData), "%s/DATA", home);
    make_directory(homeData);

    if (find_entry(install, "data", dataName, sizeof(dataName))) {
        snprintf(dataDirectory, sizeof(dataDirectory), "%s/%s", install, dataName);

        DIR* dir = opendir(dataDirectory);
        if (dir != nullptr) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue;
                }

                // Saves stay in the home folder.
                if (strcasecmp(entry->d_name, "savegame") == 0) {
                    continue;
                }

                char target[GAME_HOME_PATH_SIZE];
                char link[GAME_HOME_PATH_SIZE];
                snprintf(target, sizeof(target), "%s/%s", dataDirectory, entry->d_name);
                snprintf(link, sizeof(link), "%s/%s", homeData, entry->d_name);
                relink(target, link);
            }
            closedir(dir);
        }
    }

    char saves[GAME_HOME_PATH_SIZE];
    snprintf(saves, sizeof(saves), "%s/SAVEGAME", homeData);
    make_directory(saves);

    char configName[256];
    if (find_entry(install, "fallout.cfg", configName, sizeof(configName))) {
        char from[GAME_HOME_PATH_SIZE];
        char to[GAME_HOME_PATH_SIZE];
        snprintf(from, sizeof(from), "%s/%s", install, configName);
        snprintf(to, sizeof(to), "%s/fallout.cfg", home);
        copy_file_if_missing(from, to);
    }

    char remembered[GAME_HOME_PATH_SIZE];
    snprintf(remembered, sizeof(remembered), "%s/install_path.txt", home);
    FILE* stream = fopen(remembered, "w");
    if (stream != nullptr) {
        fprintf(stream, "%s\n", install);
        fclose(stream);
    }

    return true;
}

static bool read_remembered(const char* home, char* install, size_t size)
{
    char path[GAME_HOME_PATH_SIZE];
    snprintf(path, sizeof(path), "%s/install_path.txt", home);

    FILE* stream = fopen(path, "r");
    if (stream == nullptr) {
        return false;
    }

    bool result = fgets(install, (int)size, stream) != nullptr;
    fclose(stream);

    size_t length = strlen(install);
    while (length > 0 && (install[length - 1] == '\n' || install[length - 1] == '\r')) {
        install[--length] = '\0';
    }

    return result && length > 0;
}

// Looks in the places where Fallout is usually installed.
static bool detect_install(char* install, size_t size)
{
    const char* home = getenv("HOME");
    if (home == nullptr) {
        return false;
    }

    const char* relative[] = {
        "Library/Application Support/Steam/steamapps/common/Fallout",
        "Library/Application Support/Steam/steamapps/common/Fallout 1",
        "GOG Games/Fallout",
        "Games/Fallout",
        "Applications/Fallout",
        "Downloads/Fallout",
        "Documents/Fallout",
    };

    for (const char* path : relative) {
        char candidate[GAME_HOME_PATH_SIZE];
        snprintf(candidate, sizeof(candidate), "%s/%s", home, path);
        if (has_game(candidate)) {
            snprintf(install, size, "%s", candidate);
            return true;
        }
    }

    // CrossOver keeps each game in a bottle.
    char bottles[GAME_HOME_PATH_SIZE];
    snprintf(bottles, sizeof(bottles), "%s/Library/Application Support/CrossOver/Bottles", home);
    DIR* dir = opendir(bottles);
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') {
                continue;
            }

            const char* inside[] = {
                "drive_c/Program Files (x86)/Steam/steamapps/common/Fallout",
                "drive_c/GOG Games/Fallout",
            };
            for (const char* path : inside) {
                char candidate[GAME_HOME_PATH_SIZE];
                snprintf(candidate, sizeof(candidate), "%s/%s/%s", bottles, entry->d_name, path);
                if (has_game(candidate)) {
                    snprintf(install, size, "%s", candidate);
                    closedir(dir);
                    return true;
                }
            }
        }
        closedir(dir);
    }

    return false;
}

#ifdef __APPLE__
// Asks for a folder with the system's own folder chooser.
static bool ask_for_folder(char* install, size_t size)
{
    FILE* pipe = popen("osascript -e 'POSIX path of (choose folder with prompt \"Select the folder with your Fallout files (it has MASTER.DAT and CRITTER.DAT)\")' 2>/dev/null", "r");
    if (pipe == nullptr) {
        return false;
    }

    bool result = fgets(install, (int)size, pipe) != nullptr;
    pclose(pipe);

    size_t length = strlen(install);
    while (length > 1 && (install[length - 1] == '\n' || install[length - 1] == '/')) {
        install[--length] = '\0';
    }

    return result && length > 0;
}
#else
static bool ask_for_folder(char*, size_t)
{
    return false;
}
#endif

bool game_home_enter(const char* bundleDirectory)
{
    // A bundle that carries its own data runs as it always did.
    if (bundleDirectory != nullptr && has_game(bundleDirectory)) {
        return chdir(bundleDirectory) == 0;
    }

    const char* user = getenv("HOME");
    if (user == nullptr) {
        return false;
    }

    char support[GAME_HOME_PATH_SIZE];
    char home[GAME_HOME_PATH_SIZE];
    snprintf(support, sizeof(support), "%s/Library/Application Support", user);
    snprintf(home, sizeof(home), "%s/Fallout ME", support);

    make_directory(support);
    make_directory(home);

    if (has_game(home)) {
        return chdir(home) == 0;
    }

    char install[GAME_HOME_PATH_SIZE];
    bool found = (read_remembered(home, install, sizeof(install)) && has_game(install))
        || detect_install(install, sizeof(install));

    for (int attempt = 0; !found && attempt < 3; attempt++) {
        if (!ask_for_folder(install, sizeof(install))) {
            break;
        }

        found = has_game(install);
        if (!found) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Fallout ME", "That folder does not have MASTER.DAT and CRITTER.DAT. Choose the folder that does.", nullptr);
        }
    }

    if (!found || !link_install(install, home)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fallout ME", "The game files were not found. Fallout ME needs your own copy of Fallout. Start it again and choose the folder that has MASTER.DAT and CRITTER.DAT.", nullptr);
        return false;
    }

    return chdir(home) == 0;
}

} // namespace fallout

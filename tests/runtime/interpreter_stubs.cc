// Stands in for everything the interpreter reaches outside itself, so that the
// real src/int/intrpret.cc can be linked into an asset-free test. Nothing here
// changes how waits, bursts, critical sections or the program list behave. The
// only stub with a bearing on timing is get_time(), which is the interpreter's
// clock (interpretSetTimeFunc() is a no-op in the engine, so the interpreter
// always reads get_time()).

#include "interpreter_stubs.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "int/export.h"
#include "int/intlib.h"
#include "int/memdbg.h"
#include "plib/color/color.h"
#include "plib/db/db.h"
#include "plib/gnw/debug.h"
#include "plib/gnw/input.h"
#include "plib/gnw/mouse.h"

namespace fallout_test {

InterpreterStubState interpreterStub;

namespace {

    std::map<std::string, std::vector<unsigned char>>& images()
    {
        static std::map<std::string, std::vector<unsigned char>> value;
        return value;
    }

} // namespace

void interpreter_stub_reset()
{
    interpreterStub = InterpreterStubState();
    images().clear();
}

void interpreter_stub_set_image(const char* path, const std::vector<unsigned char>& image)
{
    images()[path] = image;
}

} // namespace fallout_test

namespace fallout {

unsigned char cmap[768];

// The interpreter's clock.
unsigned int get_time()
{
    fallout_test::InterpreterStubState& state = fallout_test::interpreterStub;
    state.clockReads++;
    unsigned int value = state.now;
    state.now += state.advancePerRead;
    if (state.onRead != nullptr) {
        state.onRead(state.clockReads);
    }
    return value;
}

// An in-memory file.
struct DB_FILE {
    std::vector<unsigned char> bytes;
    size_t position;
};

DB_FILE* db_fopen(const char* filename, const char* mode)
{
    auto it = fallout_test::images().find(filename);
    if (it == fallout_test::images().end()) {
        return nullptr;
    }
    return new DB_FILE { it->second, 0 };
}

int db_fclose(DB_FILE* stream)
{
    delete stream;
    return 0;
}

size_t db_fread(void* buf, size_t size, size_t count, DB_FILE* stream)
{
    size_t bytes = size * count;
    if (stream->position + bytes > stream->bytes.size()) {
        bytes = stream->bytes.size() - stream->position;
    }
    memcpy(buf, stream->bytes.data() + stream->position, bytes);
    stream->position += bytes;
    return size != 0 ? bytes / size : 0;
}

long db_filelength(DB_FILE* stream)
{
    return static_cast<long>(stream->bytes.size());
}

// Allocation without the debug tracking.
void* mymalloc(size_t size, const char*, int) { return malloc(size); }
void* myrealloc(void* ptr, size_t size, const char*, int) { return realloc(ptr, size); }
void myfree(void* ptr, const char*, int) { free(ptr); }

char* mystrdup(const char* string, const char*, int)
{
    char* copy = static_cast<char*>(malloc(strlen(string) + 1));
    strcpy(copy, string);
    return copy;
}

// ASCII case-insensitive comparison. Written out so that it builds with every
// compiler the project uses.
int compat_stricmp(const char* a, const char* b)
{
    for (;; a++, b++) {
        int x = tolower(static_cast<unsigned char>(*a));
        int y = tolower(static_cast<unsigned char>(*b));
        if (x != y) {
            return x - y;
        }
        if (x == 0) {
            return 0;
        }
    }
}

int debug_printf(const char*, ...) { return 0; }
void mouse_show() { }
void fadeSystemPalette(unsigned char*, unsigned char*, int) { }

// The script libraries are not part of this test. updateIntLib() counts its
// calls so that a test can tell whether a pass reached it.
void initIntlib() { }
void intlibClose() { }
void updateIntLib() { fallout_test::interpreterStub.updateIntLibCalls++; }

void removeProgramReferences(Program*)
{
    fallout_test::interpreterStub.removedPrograms++;
}

void initExport() { }
void exportClose() { }
int exportStoreVariable(Program*, const char*, ProgramValue&) { return -1; }
int exportFetchVariable(Program*, const char*, ProgramValue&) { return -1; }
int exportExportVariable(Program*, const char*) { return -1; }
Program* exportFindProcedure(const char*, int*, int*) { return nullptr; }
int exportExportProcedure(Program*, const char*, int, int) { return -1; }

} // namespace fallout

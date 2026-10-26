// The engine's rect list allocates through the plib memory manager, which
// pulls in the window system. Tests use plain malloc instead.

#include <stdlib.h>

namespace fallout {

void* mem_malloc(size_t size)
{
    return malloc(size);
}

void mem_free(void* ptr)
{
    free(ptr);
}

} // namespace fallout

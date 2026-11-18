#include "test_harness.h"

#include "object_handle.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <new>

using namespace fallout;

// Allocation failure injection. While g_failAllocations is set, every operator
// new in this test binary throws std::bad_alloc.
static bool g_failAllocations = false;

void* operator new(size_t size)
{
    if (g_failAllocations) {
        throw std::bad_alloc();
    }
    void* p = malloc(size != 0 ? size : 1);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void operator delete(void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }

static void* fake(uintptr_t address)
{
    return reinterpret_cast<void*>(address);
}

TEST(object_handle_round_trip)
{
    ObjectRegistry r;
    void* p = fake(0x1000);
    ObjectHandle h = r.create(p);
    CHECK(h.valid());
    CHECK(r.resolve(h) == p);
    CHECK(r.handleFor(p) == h);
    CHECK_EQ(r.liveCount(), 1u);
}

TEST(object_handle_null_and_unknown_are_invalid)
{
    ObjectRegistry r;
    CHECK(!r.create(nullptr).valid());
    CHECK_EQ(r.liveCount(), 0u);
    CHECK(!r.handleFor(nullptr).valid());
    CHECK(!r.handleFor(fake(0x2000)).valid());
    CHECK(r.resolve(ObjectHandle()) == nullptr);
    CHECK(r.resolve(ObjectHandle { 0, 1 }) == nullptr);
    CHECK(r.resolve(ObjectHandle { 99, 1 }) == nullptr);
}

TEST(object_handle_destroy_makes_handle_stale)
{
    ObjectRegistry r;
    void* p = fake(0x1000);
    ObjectHandle h = r.create(p);
    CHECK(r.destroy(p));
    CHECK(r.resolve(h) == nullptr);
    CHECK(!r.handleFor(p).valid());
    CHECK_EQ(r.liveCount(), 0u);
    CHECK(!r.destroy(p));
    CHECK(!r.destroy(fake(0x3000)));
    CHECK(!r.destroy(nullptr));
    CHECK_EQ(r.liveCount(), 0u);
}

TEST(object_handle_address_reuse)
{
    ObjectRegistry r;
    void* p = fake(0x1000);
    ObjectHandle oldHandle = r.create(p);
    r.destroy(p);
    ObjectHandle newHandle = r.create(p);
    CHECK(r.resolve(oldHandle) == nullptr);
    CHECK(r.resolve(newHandle) == p);
    CHECK(r.handleFor(p) == newHandle);
    CHECK(oldHandle != newHandle);
}

TEST(object_handle_slot_reuse_raises_generation)
{
    ObjectRegistry r;
    ObjectHandle a = r.create(fake(0x1000));
    r.destroy(fake(0x1000));
    ObjectHandle b = r.create(fake(0x2000));
    CHECK_EQ(b.slot, a.slot);
    CHECK(b.generation > a.generation);
    CHECK(r.resolve(a) == nullptr);
    CHECK(r.resolve(b) == fake(0x2000));
}

TEST(object_handle_many_cycles)
{
    ObjectRegistry r;
    const int cycles = 100000;
    ObjectHandle* old = new ObjectHandle[cycles];
    ObjectHandle keep = r.create(fake(0x10));
    for (int i = 0; i < cycles; i++) {
        void* p = fake(0x1000 + static_cast<uintptr_t>(i % 7) * 16);
        old[i] = r.create(p);
        CHECK(old[i].valid());
        CHECK_EQ(r.liveCount(), 2u);
        CHECK(r.destroy(p));
        CHECK_EQ(r.liveCount(), 1u);
    }
    bool allStale = true;
    for (int i = 0; i < cycles; i++) {
        if (r.resolve(old[i]) != nullptr) {
            allStale = false;
        }
    }
    CHECK(allStale);
    CHECK(r.resolve(keep) == fake(0x10));
    CHECK_EQ(r.retiredSlots(), 0u);
    delete[] old;
}

TEST(object_handle_duplicate_create)
{
    ObjectRegistry r;
    void* p = fake(0x1000);
    ObjectHandle a = r.create(p);
    ObjectHandle b = r.create(p);
    CHECK(a == b);
    CHECK_EQ(r.duplicateCreates(), 1u);
    CHECK_EQ(r.liveCount(), 1u);
    r.create(p);
    CHECK_EQ(r.duplicateCreates(), 2u);
}

struct FakePayload {
    int id;
    int tile;
    char data[32];
};

TEST(object_handle_identical_payloads_get_distinct_handles)
{
    ObjectRegistry r;
    FakePayload original;
    memset(&original, 0, sizeof(original));
    original.id = 4242;
    FakePayload copy;
    memcpy(&copy, &original, sizeof(copy));
    ObjectHandle a = r.create(&original);
    ObjectHandle b = r.create(&copy);
    CHECK(a != b);
    CHECK(r.resolve(a) == &original);
    CHECK(r.resolve(b) == &copy);
}

TEST(object_handle_generation_exhaustion_retires_slot)
{
    ObjectRegistry r(UINT64_MAX - 2);
    void* pa = fake(0x1000);
    ObjectHandle a = r.create(pa);
    CHECK_EQ(a.generation, UINT64_MAX - 2);
    CHECK(r.destroy(pa));
    CHECK_EQ(r.retiredSlots(), 0u);

    void* pb = fake(0x2000);
    ObjectHandle b = r.create(pb);
    CHECK_EQ(b.slot, a.slot);
    CHECK_EQ(b.generation, UINT64_MAX - 1);
    CHECK(r.destroy(pb));
    CHECK_EQ(r.retiredSlots(), 1u);

    ObjectHandle c = r.create(fake(0x3000));
    CHECK(c.valid());
    CHECK(c.slot != a.slot);
    CHECK(r.resolve(a) == nullptr);
    CHECK(r.resolve(b) == nullptr);
    CHECK(r.resolve(ObjectHandle { a.slot, UINT64_MAX }) == nullptr);
    CHECK(r.resolve(c) == fake(0x3000));
}

TEST(object_handle_allocation_failure_leaves_registry_unchanged)
{
    ObjectRegistry r;
    ObjectHandle first = r.create(fake(0x1000));

    g_failAllocations = true;
    ObjectHandle failed = r.create(fake(0x2000));
    g_failAllocations = false;

    CHECK(!failed.valid());
    CHECK_EQ(r.liveCount(), 1u);
    CHECK(!r.handleFor(fake(0x2000)).valid());
    CHECK(r.resolve(first) == fake(0x1000));
    CHECK_EQ(r.duplicateCreates(), 0u);

    // The registry still works, and the failed pointer can register later.
    ObjectHandle second = r.create(fake(0x2000));
    CHECK(second.valid());
    CHECK(second != first);
    CHECK_EQ(r.liveCount(), 2u);
    CHECK(r.destroy(fake(0x2000)));
    CHECK(r.destroy(fake(0x1000)));
}

TEST(object_handle_allocation_failure_on_recycled_slot)
{
    ObjectRegistry r;
    ObjectHandle a = r.create(fake(0x1000));
    r.destroy(fake(0x1000));

    g_failAllocations = true;
    ObjectHandle failed = r.create(fake(0x2000));
    g_failAllocations = false;

    CHECK(!failed.valid());
    CHECK_EQ(r.liveCount(), 0u);
    ObjectHandle b = r.create(fake(0x2000));
    CHECK_EQ(b.slot, a.slot);
    CHECK(b.generation > a.generation);
}

TEST(object_handle_ref_epoch)
{
    ObjectRegistry r;
    void* p = fake(0x1000);
    r.create(p);
    ObjectRef ref = object_ref_make(r, p, 5);
    CHECK(ref.handle.valid());
    CHECK(object_ref_resolve(r, ref, 5) == p);
    CHECK(object_ref_resolve(r, ref, 6) == nullptr);
    CHECK(object_ref_resolve(r, ref, 4) == nullptr);
    CHECK(object_ref_resolve(r, ref, 5) == p);

    r.destroy(p);
    CHECK(object_ref_resolve(r, ref, 5) == nullptr);
    CHECK(object_ref_resolve(r, ref, 6) == nullptr);

    ObjectRef none = object_ref_make(r, fake(0x9000), 5);
    CHECK(!none.handle.valid());
    CHECK(object_ref_resolve(r, none, 5) == nullptr);
}

TEST(object_handle_global_registry_is_one_instance)
{
    CHECK(&object_registry() == &object_registry());
}

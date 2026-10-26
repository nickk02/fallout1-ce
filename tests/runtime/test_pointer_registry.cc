#include "test_harness.h"

#include "pointer_registry.h"

using namespace fallout;

TEST(pointer_registry_null_is_zero)
{
    PointerRegistry registry;
    CHECK_EQ(registry.store(nullptr), 0);
    CHECK(registry.fetch(0) == nullptr);
}

TEST(pointer_registry_round_trips_pointer)
{
    PointerRegistry registry;
    int value = 0;
    int ref = registry.store(&value);
    CHECK(ref != 0);
    CHECK(registry.fetch(ref) == &value);
    // A plain fetch keeps the entry.
    CHECK(registry.fetch(ref) == &value);
}

TEST(pointer_registry_refs_are_not_reused)
{
    PointerRegistry registry;
    int a = 0;
    int b = 0;
    int refA = registry.store(&a);
    registry.fetch(refA, true);
    int refB = registry.store(&b);
    CHECK(refA != refB);
}

TEST(pointer_registry_removed_ref_is_invalid)
{
    PointerRegistry registry;
    int value = 0;
    int ref = registry.store(&value);
    CHECK(registry.fetch(ref, true) == &value);
    CHECK(registry.fetch(ref) == nullptr);
}

TEST(pointer_registry_unknown_ref_is_null)
{
    PointerRegistry registry;
    CHECK(registry.fetch(12345) == nullptr);
}

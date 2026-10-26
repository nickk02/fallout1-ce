#include "test_harness.h"

#include "plib/gnw/rect.h"

using namespace fallout;

static Rect make_rect(int ulx, int uly, int lrx, int lry)
{
    Rect rect = { ulx, uly, lrx, lry };
    return rect;
}

static int count_nodes(RectPtr list)
{
    int count = 0;
    for (; list != nullptr; list = list->next) {
        count++;
    }
    return count;
}

TEST(rect_size_is_inclusive)
{
    Rect rect = make_rect(10, 20, 19, 39);
    CHECK_EQ(rectGetWidth(&rect), 10);
    CHECK_EQ(rectGetHeight(&rect), 20);
}

TEST(rect_min_bound_is_union)
{
    Rect a = make_rect(0, 0, 9, 9);
    Rect b = make_rect(5, -3, 20, 4);
    Rect result;
    rect_min_bound(&a, &b, &result);
    CHECK_EQ(result.ulx, 0);
    CHECK_EQ(result.uly, -3);
    CHECK_EQ(result.lrx, 20);
    CHECK_EQ(result.lry, 9);
}

TEST(rect_inside_bound_intersects)
{
    Rect r1 = make_rect(0, 0, 9, 9);
    Rect bound = make_rect(5, 5, 20, 20);
    Rect result;
    CHECK_EQ(rect_inside_bound(&r1, &bound, &result), 0);
    CHECK_EQ(result.ulx, 5);
    CHECK_EQ(result.uly, 5);
    CHECK_EQ(result.lrx, 9);
    CHECK_EQ(result.lry, 9);
}

TEST(rect_inside_bound_disjoint_returns_minus_one_and_copies_r1)
{
    Rect r1 = make_rect(0, 0, 4, 4);
    Rect bound = make_rect(10, 10, 20, 20);
    Rect result;
    CHECK_EQ(rect_inside_bound(&r1, &bound, &result), -1);
    CHECK_EQ(result.ulx, 0);
    CHECK_EQ(result.uly, 0);
    CHECK_EQ(result.lrx, 4);
    CHECK_EQ(result.lry, 4);
}

TEST(rect_offset_moves_all_edges)
{
    Rect rect = make_rect(1, 2, 3, 4);
    rectOffset(&rect, 10, -2);
    CHECK_EQ(rect.ulx, 11);
    CHECK_EQ(rect.uly, 0);
    CHECK_EQ(rect.lrx, 13);
    CHECK_EQ(rect.lry, 2);
}

TEST(rect_clip_disjoint_returns_whole_bound)
{
    Rect bound = make_rect(0, 0, 9, 9);
    Rect other = make_rect(100, 100, 109, 109);
    RectPtr list = rect_clip(&bound, &other);
    CHECK(list != nullptr);
    CHECK_EQ(count_nodes(list), 1);
    if (list != nullptr) {
        CHECK_EQ(list->rect.ulx, 0);
        CHECK_EQ(list->rect.lrx, 9);
    }
    // The caller owns the returned nodes. Give them back before shutdown.
    while (list != nullptr) {
        RectPtr next = list->next;
        rect_free(list);
        list = next;
    }
    GNW_rect_exit();
}

TEST(rect_list_recycles_freed_nodes)
{
    RectPtr first = rect_malloc();
    CHECK(first != nullptr);
    rect_free(first);
    RectPtr second = rect_malloc();
    CHECK(second == first);
    rect_free(second);
    GNW_rect_exit();
}

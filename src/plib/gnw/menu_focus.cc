#include "plib/gnw/menu_focus.h"

#include <stdlib.h>

#include "plib/gnw/button.h"
#include "plib/gnw/dxinput.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/svga.h"

namespace fallout {

#define MENU_FOCUS_MAX_BUTTONS 256

typedef struct FocusCandidate {
    int buttonId;
    Rect rect;
    int centerX;
    int centerY;
} FocusCandidate;

static bool focusActive = false;
static int focusWindowId = -1;
static int focusButtonId = -1;

static int menu_focus_collect(Window* window, FocusCandidate* candidates);
static void menu_focus_select(const FocusCandidate* candidate, int windowId);

void menu_focus_move(int dx, int dy)
{
    Window* window = GNW_top_button_window();
    if (window == nullptr) {
        menu_focus_clear();
        return;
    }

    FocusCandidate candidates[MENU_FOCUS_MAX_BUTTONS];
    int count = menu_focus_collect(window, candidates);
    if (count == 0) {
        menu_focus_clear();
        return;
    }

    int current = -1;
    if (focusActive && focusWindowId == window->id) {
        for (int index = 0; index < count; index++) {
            if (candidates[index].buttonId == focusButtonId) {
                current = index;
                break;
            }
        }
    }

    if (current == -1) {
        // Start at the button closest to the cursor.
        int mouseX;
        int mouseY;
        mouse_get_position(&mouseX, &mouseY);

        int best = 0;
        int bestDistance = -1;
        for (int index = 0; index < count; index++) {
            int distance = abs(candidates[index].centerX - mouseX) + abs(candidates[index].centerY - mouseY);
            if (bestDistance == -1 || distance < bestDistance) {
                bestDistance = distance;
                best = index;
            }
        }

        menu_focus_select(&(candidates[best]), window->id);
        return;
    }

    if (dx == 0 && dy == 0) {
        return;
    }

    int best = -1;
    int bestScore = 0;
    for (int index = 0; index < count; index++) {
        if (index == current) {
            continue;
        }

        int vx = candidates[index].centerX - candidates[current].centerX;
        int vy = candidates[index].centerY - candidates[current].centerY;

        int along = vx * dx + vy * dy;
        int across = abs(vx * dy - vy * dx);
        if (along <= 0 || across > along * 2) {
            continue;
        }

        int score = along + across * 2;
        if (best == -1 || score < bestScore) {
            best = index;
            bestScore = score;
        }
    }

    if (best != -1) {
        menu_focus_select(&(candidates[best]), window->id);
    } else if (dy != 0) {
        // Nothing further in that direction: scroll the list under the focus.
        dxinput_add_wheel(0, -dy);
    }
}

void menu_focus_update()
{
    if (!focusActive) {
        return;
    }

    Window* window = GNW_top_button_window();
    if (window == nullptr) {
        menu_focus_clear();
        return;
    }

    if (window->id != focusWindowId) {
        // A new screen opened. Focus its first button.
        focusActive = false;
        menu_focus_move(0, 0);
        return;
    }

    FocusCandidate candidates[MENU_FOCUS_MAX_BUTTONS];
    int count = menu_focus_collect(window, candidates);
    for (int index = 0; index < count; index++) {
        if (candidates[index].buttonId == focusButtonId) {
            svga_set_focus_rect(&(candidates[index].rect));
            return;
        }
    }

    focusActive = false;
    menu_focus_move(0, 0);
}

int menu_focus_get_key()
{
    if (!focusActive) {
        return -1;
    }

    Window* window = GNW_top_button_window();
    if (window == nullptr || window->id != focusWindowId) {
        return -1;
    }

    for (Button* button = window->buttonListHead; button != nullptr; button = button->next) {
        if (button->id == focusButtonId) {
            return button->lefMouseDownEventCode;
        }
    }

    return -1;
}

bool menu_focus_is_active()
{
    return focusActive;
}

void menu_focus_clear()
{
    focusActive = false;
    focusWindowId = -1;
    focusButtonId = -1;
    svga_set_focus_rect(nullptr);
}

// Lists the enabled buttons of a window in screen coordinates. Buttons that
// cover most of the window are click-anywhere areas, not choices.
static int menu_focus_collect(Window* window, FocusCandidate* candidates)
{
    int windowArea = (window->rect.lrx - window->rect.ulx + 1) * (window->rect.lry - window->rect.uly + 1);

    int count = 0;
    for (Button* button = window->buttonListHead; button != nullptr && count < MENU_FOCUS_MAX_BUTTONS; button = button->next) {
        if ((button->flags & BUTTON_FLAG_DISABLED) != 0) {
            continue;
        }

        int width = button->rect.lrx - button->rect.ulx + 1;
        int height = button->rect.lry - button->rect.uly + 1;
        if (width < 6 || height < 6 || width * height * 10 > windowArea * 4) {
            continue;
        }

        FocusCandidate* candidate = &(candidates[count++]);
        candidate->buttonId = button->id;
        candidate->rect.ulx = window->rect.ulx + button->rect.ulx;
        candidate->rect.uly = window->rect.uly + button->rect.uly;
        candidate->rect.lrx = window->rect.ulx + button->rect.lrx;
        candidate->rect.lry = window->rect.uly + button->rect.lry;
        candidate->centerX = (candidate->rect.ulx + candidate->rect.lrx) / 2;
        candidate->centerY = (candidate->rect.uly + candidate->rect.lry) / 2;
    }

    return count;
}

static void menu_focus_select(const FocusCandidate* candidate, int windowId)
{
    focusActive = true;
    focusWindowId = windowId;
    focusButtonId = candidate->buttonId;

    svga_set_focus_rect(&(candidate->rect));
    mouse_set_position(candidate->centerX, candidate->centerY);
    svga_set_pointer_position(candidate->centerX, candidate->centerY);
}

} // namespace fallout

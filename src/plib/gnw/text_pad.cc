#include "plib/gnw/text_pad.h"

#include <ctype.h>
#include <math.h>

#include "plib/color/color.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/input.h"
#include "plib/gnw/menu_focus.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"

namespace fallout {

#define TEXT_PAD_COLUMNS 10
#define TEXT_PAD_CHARACTER_ROWS 5

// The last row holds the keys that are not characters.
#define TEXT_PAD_ROWS (TEXT_PAD_CHARACTER_ROWS + 1)

// Only characters the game accepts in names and save descriptions. The text
// loops take codes from space to 'z'.
static const char padCharacters[TEXT_PAD_CHARACTER_ROWS][TEXT_PAD_COLUMNS + 1] = {
    "abcdefghij",
    "klmnopqrst",
    "uvwxyz0123",
    "456789-_!@",
    "#$`'^&()=.",
};

typedef struct SpecialKey {
    const char* label;
    int firstColumn;
    int columnCount;
} SpecialKey;

static const SpecialKey padSpecialKeys[] = {
    { "SHIFT", 0, 3 },
    { "SPACE", 3, 3 },
    { "DEL", 6, 2 },
    { "DONE", 8, 2 },
};

#define TEXT_PAD_SPECIAL_COUNT 4

static int padWindow = -1;
static int padColumn = 0;
static int padRow = 0;
static bool padUpper = false;

static void text_pad_draw();
static void text_pad_draw_pill(unsigned char* buffer, int pitch, int bufferHeight, int left, int top, int width, int height);
static int text_pad_special_at(int column);

// A palette index for a green of the given strength (0..1) on black.
static int text_pad_green(float strength)
{
    int green = (int)(strength * 31.0f + 0.5f);
    int red = (int)(strength * strength * 12.0f);
    return colorTable[(red << 10) | (green << 5) | (red / 2)];
}

void text_pad_begin()
{
    if (padWindow != -1 || !input_controller_in_use()) {
        return;
    }

    // The menu focus frame belongs to the screen behind the picker.
    menu_focus_clear();

    int cellWidth = text_max() + 8;
    int cellHeight = text_height() + 4;
    int width = cellWidth * TEXT_PAD_COLUMNS + 8;
    int height = cellHeight * TEXT_PAD_ROWS + text_height() + 18;

    int x = (scr_size.lrx - scr_size.ulx + 1 - width) / 2;
    int y = scr_size.lry - scr_size.uly + 1 - height - 4;

    padWindow = win_add(x, y, width, height, colorTable[0], WINDOW_MOVE_ON_TOP);
    if (padWindow == -1) {
        return;
    }

    padColumn = 0;
    padRow = 0;
    padUpper = false;

    text_pad_draw();
    win_show(padWindow);
}

void text_pad_end()
{
    if (padWindow == -1) {
        return;
    }

    win_delete(padWindow);
    padWindow = -1;
}

bool text_pad_is_active()
{
    return padWindow != -1;
}

void text_pad_move(int dx, int dy)
{
    if (padWindow == -1) {
        return;
    }

    padRow = (padRow + dy + TEXT_PAD_ROWS) % TEXT_PAD_ROWS;

    if (padRow == TEXT_PAD_CHARACTER_ROWS) {
        // Keys of different widths: step from one to the next.
        int index = text_pad_special_at(padColumn);
        if (dx != 0) {
            index = (index + dx + TEXT_PAD_SPECIAL_COUNT) % TEXT_PAD_SPECIAL_COUNT;
        }
        padColumn = padSpecialKeys[index].firstColumn;
    } else {
        padColumn = (padColumn + dx + TEXT_PAD_COLUMNS) % TEXT_PAD_COLUMNS;
    }

    text_pad_draw();
}

void text_pad_press()
{
    if (padWindow == -1) {
        return;
    }

    if (padRow == TEXT_PAD_CHARACTER_ROWS) {
        switch (text_pad_special_at(padColumn)) {
        case 0:
            text_pad_toggle_case();
            break;
        case 1:
            input_inject_key(SDL_SCANCODE_SPACE);
            break;
        case 2:
            input_inject_key(SDL_SCANCODE_BACKSPACE);
            break;
        case 3:
            input_inject_key(SDL_SCANCODE_RETURN);
            break;
        }
        return;
    }

    int ch = padCharacters[padRow][padColumn];
    if (padUpper) {
        ch = toupper(ch);
    }

    GNW_add_input_buffer(ch);
}

void text_pad_toggle_case()
{
    if (padWindow == -1) {
        return;
    }

    padUpper = !padUpper;
    text_pad_draw();
}

static int text_pad_special_at(int column)
{
    for (int index = 0; index < TEXT_PAD_SPECIAL_COUNT; index++) {
        if (column >= padSpecialKeys[index].firstColumn && column < padSpecialKeys[index].firstColumn + padSpecialKeys[index].columnCount) {
            return index;
        }
    }

    return 0;
}

static void text_pad_draw()
{
    unsigned char* buffer = win_get_buf(padWindow);
    int width = win_width(padWindow);
    int height = win_height(padWindow);

    int bright = colorTable[32767];
    int dim = text_pad_green(0.55f);
    int black = colorTable[0];

    int cellWidth = text_max() + 8;
    int cellHeight = text_height() + 4;

    buf_fill(buffer, width, height, width, black);

    // Hairline frame.
    int frame = text_pad_green(0.35f);
    buf_fill(buffer, width, 1, width, frame);
    buf_fill(buffer + width * (height - 1), width, 1, width, frame);
    buf_fill(buffer, 1, height, width, frame);
    buf_fill(buffer + width - 1, 1, height, width, frame);

    for (int row = 0; row < TEXT_PAD_CHARACTER_ROWS; row++) {
        for (int column = 0; column < TEXT_PAD_COLUMNS; column++) {
            int cellX = 4 + column * cellWidth;
            int cellY = 4 + row * cellHeight;

            bool selected = row == padRow && column == padColumn;
            if (selected) {
                text_pad_draw_pill(buffer, width, height, cellX + (cellWidth - cellHeight) / 2, cellY, cellHeight, cellHeight);
            }

            char text[2] = { padCharacters[row][column], '\0' };
            if (padUpper) {
                text[0] = (char)toupper(text[0]);
            }

            int textX = cellX + (cellWidth - text_width(text)) / 2;
            text_to_buf(buffer + width * (cellY + 2) + textX, text, width, width, selected ? bright : dim);
        }
    }

    int keyY = 4 + TEXT_PAD_CHARACTER_ROWS * cellHeight;
    for (int index = 0; index < TEXT_PAD_SPECIAL_COUNT; index++) {
        const SpecialKey* key = &(padSpecialKeys[index]);
        int keyX = 4 + key->firstColumn * cellWidth;
        int keyWidth = key->columnCount * cellWidth;

        bool selected = padRow == TEXT_PAD_CHARACTER_ROWS && text_pad_special_at(padColumn) == index;
        if (selected) {
            text_pad_draw_pill(buffer, width, height, keyX + 2, keyY, keyWidth - 4, cellHeight);
        }

        // SHIFT stays lit while upper case is on.
        bool lit = selected || (index == 0 && padUpper);
        int textX = keyX + (keyWidth - text_width(key->label)) / 2;
        text_to_buf(buffer + width * (keyY + 2) + textX, key->label, width, width, lit ? bright : dim);
    }

    int green = text_pad_green(0.7f);
    const char* hint = "A type  B delete  X space  Y case  Start done  Back cancel";
    int hintX = (width - text_width(hint)) / 2;
    text_to_buf(buffer + width * (keyY + cellHeight + 5) + (hintX > 2 ? hintX : 2), hint, width, width, green);

    win_draw(padWindow);
}

// Soft outline around a rounded bar. When the bar is as wide as it is tall, it
// is a circle. The line is brightest on the edge and fades over about two
// pixels, with a faint fill inside.
static void text_pad_draw_pill(unsigned char* buffer, int pitch, int bufferHeight, int left, int top, int width, int height)
{
    float radius = height / 2.0f;
    float centerY = top + radius;
    float startX = left + radius;
    float endX = left + width - radius;

    for (int y = top - 3; y < top + height + 3; y++) {
        for (int x = left - 3; x < left + width + 3; x++) {
            if (x < 0 || x >= pitch || y < 0 || y >= bufferHeight) {
                continue;
            }

            // Distance to the middle line of the bar, then to its edge.
            float px = x + 0.5f;
            float nearest = px < startX ? startX : (px > endX ? endX : px);
            float dx = px - nearest;
            float dy = y + 0.5f - centerY;
            float distance = sqrtf(dx * dx + dy * dy);
            float edge = fabsf(distance - radius);

            float strength = 0.0f;
            if (edge < 2.0f) {
                strength = 1.0f - edge / 2.0f;
            } else if (distance < radius) {
                strength = 0.12f;
            }

            if (strength <= 0.0f) {
                continue;
            }

            buffer[pitch * y + x] = text_pad_green(0.15f + strength * 0.85f * (edge < 2.0f ? 1.0f : 0.3f));
        }
    }
}

} // namespace fallout

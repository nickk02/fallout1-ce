#include "game/damage_popup.h"

#include <stdio.h>

#include "game/config.h"
#include "game/gconfig.h"
#include "game/textobj.h"
#include "game/tile.h"
#include "plib/color/color.h"
#include "plib/gnw/input.h"

namespace fallout {

#define DAMAGE_POPUP_MAX 16

// How long a number stays above the target, in seconds.
#define DAMAGE_POPUP_SECONDS 0.9

typedef struct DamagePopup {
    bool used;
    Object* target;
    char text[16];
    bool critical;
    bool hit;
    unsigned int showAt;
} DamagePopup;

static DamagePopup popups[DAMAGE_POPUP_MAX];

void damage_popup_schedule(Object* target, int damage, bool hit, bool critical, unsigned int delayMs)
{
    for (int index = 0; index < DAMAGE_POPUP_MAX; index++) {
        DamagePopup* popup = &(popups[index]);
        if (popup->used) {
            continue;
        }

        popup->used = true;
        popup->target = target;
        popup->hit = hit;
        popup->critical = critical;
        popup->showAt = get_time() + delayMs;
        if (hit) {
            snprintf(popup->text, sizeof(popup->text), "%d", damage);
        } else {
            snprintf(popup->text, sizeof(popup->text), "Miss");
        }
        return;
    }
}

void damage_popup_delay_all(unsigned int delayMs)
{
    for (int index = 0; index < DAMAGE_POPUP_MAX; index++) {
        if (popups[index].used) {
            popups[index].showAt += delayMs;
        }
    }
}

void damage_popup_update()
{
    unsigned int now = get_time();

    for (int index = 0; index < DAMAGE_POPUP_MAX; index++) {
        DamagePopup* popup = &(popups[index]);
        if (!popup->used || now < popup->showAt) {
            continue;
        }

        popup->used = false;

        // Text objects last as long as the speech delay allows. Shorten it for
        // this one, then put the player's setting back.
        double baseDelay = 3.5;
        double lineDelay = 1.4;
        config_get_double(&game_config, GAME_CONFIG_PREFERENCES_KEY, GAME_CONFIG_TEXT_BASE_DELAY_KEY, &baseDelay);
        config_get_double(&game_config, GAME_CONFIG_PREFERENCES_KEY, GAME_CONFIG_TEXT_LINE_DELAY_KEY, &lineDelay);

        text_object_set_base_delay(DAMAGE_POPUP_SECONDS);
        text_object_set_line_delay(0.0);

        int color = colorTable[31744]; // Red
        if (!popup->hit) {
            color = colorTable[32767]; // White
        } else if (popup->critical) {
            color = colorTable[32747]; // Yellow
        }

        Rect rect;
        if (text_object_create(popup->target, popup->text, 101, color, colorTable[0], &rect) == 0) {
            tile_refresh_rect(&rect, popup->target->elevation);
        }

        text_object_set_base_delay(baseDelay);
        text_object_set_line_delay(lineDelay);
    }
}

} // namespace fallout

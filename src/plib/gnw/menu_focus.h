#ifndef FALLOUT_PLIB_GNW_MENU_FOCUS_H_
#define FALLOUT_PLIB_GNW_MENU_FOCUS_H_

namespace fallout {

// Moves the focus to the neighboring button of the topmost window in the given
// direction (one of -1, 0, 1 on each axis). The first call only shows the
// focus. At the edge of a list, up and down scroll it. The cursor jumps to the
// focused button, so a click lands on it.
void menu_focus_move(int dx, int dy);

// Keeps the focus valid when windows open and close. Does nothing until the
// controller has moved the focus once.
void menu_focus_update();

// The event code that clicking the focused button gives, or -1 when no button
// has the focus. Screens use it to know which of their items is highlighted.
int menu_focus_get_key();

// `true` once the controller has taken over navigation.
bool menu_focus_is_active();

// Hides the focus. Called when the map takes over.
void menu_focus_clear();

} // namespace fallout

#endif /* FALLOUT_PLIB_GNW_MENU_FOCUS_H_ */

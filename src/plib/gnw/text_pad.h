#ifndef FALLOUT_PLIB_GNW_TEXT_PAD_H_
#define FALLOUT_PLIB_GNW_TEXT_PAD_H_

namespace fallout {

// On-screen character picker for the controller. It opens next to the text
// field when a text entry loop starts while the controller is the last device
// used, and sends its picks into the game's input queue as typed keys, so the
// text loops need no changes.
void text_pad_begin();
void text_pad_end();

// `true` while the picker is on screen.
bool text_pad_is_active();

// Moves the highlighted character.
void text_pad_move(int dx, int dy);

// Types the highlighted character.
void text_pad_press();

// Toggles between lower case and upper case.
void text_pad_toggle_case();

} // namespace fallout

#endif /* FALLOUT_PLIB_GNW_TEXT_PAD_H_ */

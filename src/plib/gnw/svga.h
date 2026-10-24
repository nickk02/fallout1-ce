#ifndef FALLOUT_PLIB_GNW_SVGA_H_
#define FALLOUT_PLIB_GNW_SVGA_H_

#include <SDL.h>

#include "fps_limiter.h"
#include "plib/gnw/rect.h"
#include "plib/gnw/svga_types.h"

namespace fallout {

extern Rect scr_size;
extern ScreenBlitFunc* scr_blit;

extern SDL_Window* gSdlWindow;
extern SDL_Surface* gSdlSurface;
extern SDL_Renderer* gSdlRenderer;
extern SDL_Texture* gSdlTexture;
extern SDL_Surface* gSdlTextureSurface;
extern FpsLimiter sharedFpsLimiter;

void GNW95_SetPaletteEntries(unsigned char* a1, int a2, int a3);
void GNW95_SetPalette(unsigned char* palette);
void GNW95_ShowRect(unsigned char* src, unsigned int src_pitch, unsigned int a3, unsigned int src_x, unsigned int src_y, unsigned int src_width, unsigned int src_height, unsigned int dest_x, unsigned int dest_y);

bool svga_init(VideoOptions* video_options);
void svga_exit();
void svga_toggle_fullscreen();

// The menus are 640 by 480 pictures. While this is on, that part of the screen
// is enlarged to fill the window, and the pointer follows. Used for the main
// menu and what it opens.
void svga_set_menu_zoom(bool enabled);
bool svga_menu_zoom_active();

// Makes the zoom show this rectangle of the game screen instead of the menu
// picture. A movie uses it to fill the window. Turning the zoom on or off puts
// the menu picture back.
void svga_set_menu_zoom_area(int x, int y, int width, int height);

// `true` when the zoom was asked for, even if the screen is too small for it.
bool svga_menu_zoom_enabled();

// Screen pixels per game pixel while the zoom is on, 1 otherwise.
float svga_menu_zoom_scale();

// The part of the game screen that is shown while the zoom is on.
void svga_menu_zoom_area(int* left, int* top, int* right, int* bottom);

// Minimizes the window. Full screen is left first, because a full screen window
// cannot be minimized.
void svga_minimize();

void svga_update_mouse_mode();

// Pointer position in game coordinates. Used when the pointer is not captured
// (windowed mode). `svga_get_pointer_position` returns `false` when captured.
bool svga_get_pointer_position(int* x, int* y);
void svga_set_pointer_position(int x, int y);

// `true` shortly after the game moved the pointer itself, so the resulting
// motion event is not the user's.
bool svga_pointer_warp_recent();

// Draws a frame around `rect` (game coordinates) until cleared with `NULL`.
void svga_set_focus_rect(const Rect* rect);

// Cycles how the picture is scaled to the window: nearest, smooth, sharp.
void svga_cycle_scale_filter();
int screenGetWidth();
int screenGetHeight();
void handleWindowSizeChanged();
void renderPresent();

} // namespace fallout

#endif /* FALLOUT_PLIB_GNW_SVGA_H_ */

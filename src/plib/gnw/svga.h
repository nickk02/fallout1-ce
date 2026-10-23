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

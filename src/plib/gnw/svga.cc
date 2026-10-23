#include "plib/gnw/svga.h"

#include <math.h>

#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/winmain.h"

namespace fallout {

static bool createRenderer(int width, int height);
static void destroyRenderer();

// screen rect
Rect scr_size;

// 0x6ACA18
ScreenBlitFunc* scr_blit = GNW95_ShowRect;

SDL_Window* gSdlWindow = NULL;
static SDL_Cursor* gBlankCursor = NULL;
SDL_Surface* gSdlSurface = NULL;
SDL_Renderer* gSdlRenderer = NULL;
SDL_Texture* gSdlTexture = NULL;
static SDL_Texture* gScaledTexture = NULL;
static int gScaleFilter = 2;

// Pixel-art enlargement done on the palette indexes before scaling to the
// window: 1 (off), 2 or 3.
static int gArtScale = 1;
static int gScaleFactor = 1;
SDL_Surface* gSdlTextureSurface = NULL;

// TODO: Remove once migration to update-render cycle is completed.
FpsLimiter sharedFpsLimiter;

// 0x4CB310
void GNW95_SetPaletteEntries(unsigned char* palette, int start, int count)
{
    if (gSdlSurface != NULL && gSdlSurface->format->palette != NULL) {
        SDL_Color colors[256];

        if (count != 0) {
            for (int index = 0; index < count; index++) {
                colors[index].r = palette[index * 3] << 2;
                colors[index].g = palette[index * 3 + 1] << 2;
                colors[index].b = palette[index * 3 + 2] << 2;
                colors[index].a = 255;
            }
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, start, count);
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
    }
}

// 0x4CB568
void GNW95_SetPalette(unsigned char* palette)
{
    if (gSdlSurface != NULL && gSdlSurface->format->palette != NULL) {
        SDL_Color colors[256];

        for (int index = 0; index < 256; index++) {
            colors[index].r = palette[index * 3] << 2;
            colors[index].g = palette[index * 3 + 1] << 2;
            colors[index].b = palette[index * 3 + 2] << 2;
            colors[index].a = 255;
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
    }
}

// 0x4CB850
void GNW95_ShowRect(unsigned char* src, unsigned int srcPitch, unsigned int a3, unsigned int srcX, unsigned int srcY, unsigned int srcWidth, unsigned int srcHeight, unsigned int destX, unsigned int destY)
{
    buf_to_buf(src + srcPitch * srcY + srcX, srcWidth, srcHeight, srcPitch, (unsigned char*)gSdlSurface->pixels + gSdlSurface->pitch * destY + destX, gSdlSurface->pitch);

    SDL_Rect srcRect;
    srcRect.x = destX;
    srcRect.y = destY;
    srcRect.w = srcWidth;
    srcRect.h = srcHeight;

    SDL_Rect destRect;
    destRect.x = destX;
    destRect.y = destY;
    SDL_BlitSurface(gSdlSurface, &srcRect, gSdlTextureSurface, &destRect);
}

void svga_toggle_fullscreen()
{
    if (gSdlWindow == NULL) {
        return;
    }

    bool fullscreen = (SDL_GetWindowFlags(gSdlWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    SDL_SetWindowFullscreen(gSdlWindow, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    svga_update_mouse_mode();
}

// The pointer is captured only in fullscreen. In a window it stays free so it
// can reach the title bar buttons and other applications.
void svga_update_mouse_mode()
{
    if (gSdlWindow == NULL) {
        return;
    }

    bool fullscreen = (SDL_GetWindowFlags(gSdlWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    SDL_SetRelativeMouseMode(fullscreen ? SDL_TRUE : SDL_FALSE);
}

bool svga_get_pointer_position(int* x, int* y)
{
    if (gSdlWindow == NULL || gSdlRenderer == NULL || SDL_GetRelativeMouseMode()) {
        return false;
    }

    int windowX;
    int windowY;
    SDL_GetMouseState(&windowX, &windowY);

    float logicalX;
    float logicalY;
    SDL_RenderWindowToLogical(gSdlRenderer, (float)windowX, (float)windowY, &logicalX, &logicalY);

    *x = (int)logicalX;
    *y = (int)logicalY;
    return true;
}

static Uint32 gLastWarpTicks = 0;

bool svga_pointer_warp_recent()
{
    return SDL_GetTicks() - gLastWarpTicks < 60;
}

void svga_set_pointer_position(int x, int y)
{
    if (gSdlWindow == NULL || gSdlRenderer == NULL || SDL_GetRelativeMouseMode()) {
        return;
    }

    gLastWarpTicks = SDL_GetTicks();

    int windowX;
    int windowY;
    SDL_RenderLogicalToWindow(gSdlRenderer, (float)x, (float)y, &windowX, &windowY);
    SDL_WarpMouseInWindow(gSdlWindow, windowX, windowY);
}

bool svga_init(VideoOptions* video_options)
{
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        return false;
    }

    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;

    if (video_options->fullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    // Fit the window to the screen: as large as fits with some margin, keeping
    // the game's shape. The renderer scales the picture to the window.
    int windowWidth = video_options->width * video_options->scale;
    int windowHeight = video_options->height * video_options->scale;

    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.w > 0 && usable.h > 0) {
        float fit = fminf(0.92f * usable.w / windowWidth, 0.92f * usable.h / windowHeight);
        windowWidth = (int)lroundf(windowWidth * fit);
        windowHeight = (int)lroundf(windowHeight * fit);
    }

    gSdlWindow = SDL_CreateWindow(GNW95_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowWidth,
        windowHeight,
        windowFlags);
    if (gSdlWindow == NULL) {
        return false;
    }

    gScaleFilter = video_options->scaleFilter;

    // Fullscreen may not cover the whole display (a notch or menu bar takes
    // some), so take the width from the window we really got.
    if (video_options->fitWidth) {
        int actualWidth;
        int actualHeight;
        SDL_GetWindowSize(gSdlWindow, &actualWidth, &actualHeight);
        if (actualWidth > 0 && actualHeight > 0) {
            int width = video_options->height * actualWidth / actualHeight;
            video_options->width = fminf(fmaxf((float)width, 960.0f), 1920.0f);
            video_options->width &= ~1;
        }
    }

    if (!createRenderer(video_options->width, video_options->height)) {
        destroyRenderer();

        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;

        return false;
    }

    // `winmain` hides the cursor before video init, and SDL resets that state
    // in `SDL_VideoInit`. The game draws its own cursor. Besides hiding it,
    // set a transparent cursor so the system pointer stays invisible when the
    // hidden state is lost (for example after focus or fullscreen changes).
    static const Uint8 blankData[8 * 2] = { 0 };
    gBlankCursor = SDL_CreateCursor(blankData, blankData, 16, 8, 0, 0);
    if (gBlankCursor != NULL) {
        SDL_SetCursor(gBlankCursor);
    }
    SDL_ShowCursor(SDL_DISABLE);

    svga_update_mouse_mode();

    gSdlSurface = SDL_CreateRGBSurface(0,
        video_options->width,
        video_options->height,
        8,
        0,
        0,
        0,
        0);
    if (gSdlSurface == NULL) {
        destroyRenderer();

        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;
    }

    SDL_Color colors[256];
    for (int index = 0; index < 256; index++) {
        colors[index].r = index;
        colors[index].g = index;
        colors[index].b = index;
        colors[index].a = 255;
    }

    SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);

    scr_size.ulx = 0;
    scr_size.uly = 0;
    scr_size.lrx = video_options->width - 1;
    scr_size.lry = video_options->height - 1;

    mouse_blit_trans = NULL;
    scr_blit = GNW95_ShowRect;
    mouse_blit = GNW95_ShowRect;

    return true;
}

void svga_exit()
{
    destroyRenderer();

    if (gSdlWindow != NULL) {
        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;
    }

    if (gBlankCursor != NULL) {
        SDL_FreeCursor(gBlankCursor);
        gBlankCursor = NULL;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

int screenGetWidth()
{
    // TODO: Make it on par with _xres;
    return rectGetWidth(&scr_size);
}

int screenGetHeight()
{
    // TODO: Make it on par with _yres.
    return rectGetHeight(&scr_size);
}

static bool createRenderer(int width, int height)
{
    gSdlRenderer = SDL_CreateRenderer(gSdlWindow, -1, 0);
    if (gSdlRenderer == NULL) {
        return false;
    }

    if (SDL_RenderSetLogicalSize(gSdlRenderer, width, height) != 0) {
        return false;
    }

    gArtScale = gScaleFilter == 3 ? 2 : (gScaleFilter == 4 ? 3 : 1);
    int textureWidth = width * gArtScale;
    int textureHeight = height * gArtScale;

    // Smooth scaling filters the picture. The others keep hard pixel edges.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, gScaleFilter == 1 ? "linear" : "nearest");
    gSdlTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, textureWidth, textureHeight);
    if (gSdlTexture == NULL) {
        return false;
    }

    // Sharp scaling first enlarges the picture by a whole number with hard
    // edges, then lets the window scale that smoothly. Pixels stay crisp and
    // the edges between them do not shimmer.
    if (gScaleFilter >= 2) {
        int outputWidth;
        int outputHeight;
        if (SDL_GetRendererOutputSize(gSdlRenderer, &outputWidth, &outputHeight) == 0 && outputWidth > 0 && outputHeight > 0) {
            int factorX = (outputWidth + textureWidth - 1) / textureWidth;
            int factorY = (outputHeight + textureHeight - 1) / textureHeight;
            gScaleFactor = factorX > factorY ? factorX : factorY;
        } else {
            gScaleFactor = 2;
        }

        if (gScaleFactor < 1) {
            gScaleFactor = 1;
        } else if (gScaleFactor > 8) {
            gScaleFactor = 8;
        }

        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        gScaledTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, textureWidth * gScaleFactor, textureHeight * gScaleFactor);
    }

    Uint32 format;
    if (SDL_QueryTexture(gSdlTexture, &format, NULL, NULL, NULL) != 0) {
        return false;
    }

    gSdlTextureSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, SDL_BITSPERPIXEL(format), format);
    if (gSdlTextureSurface == NULL) {
        return false;
    }

    return true;
}

static void destroyRenderer()
{
    if (gSdlTextureSurface != NULL) {
        SDL_FreeSurface(gSdlTextureSurface);
        gSdlTextureSurface = NULL;
    }

    if (gScaledTexture != NULL) {
        SDL_DestroyTexture(gScaledTexture);
        gScaledTexture = NULL;
    }

    if (gSdlTexture != NULL) {
        SDL_DestroyTexture(gSdlTexture);
        gSdlTexture = NULL;
    }

    if (gSdlRenderer != NULL) {
        SDL_DestroyRenderer(gSdlRenderer);
        gSdlRenderer = NULL;
    }
}

void svga_cycle_scale_filter()
{
    gScaleFilter = (gScaleFilter + 1) % 5;
    handleWindowSizeChanged();

    // The new picture buffer starts empty.
    win_refresh_all(&scr_size);
}

void handleWindowSizeChanged()
{
    destroyRenderer();
    createRenderer(screenGetWidth(), screenGetHeight());
}

static bool gFocusRectSet = false;
static SDL_Rect gFocusRect;

void svga_set_focus_rect(const Rect* rect)
{
    if (rect == NULL) {
        gFocusRectSet = false;
        return;
    }

    gFocusRect.x = rect->ulx;
    gFocusRect.y = rect->uly;
    gFocusRect.w = rect->lrx - rect->ulx + 1;
    gFocusRect.h = rect->lry - rect->uly + 1;
    gFocusRectSet = true;
}

// Enlarges the 8-bit picture two or three times without blending colors. Each
// new pixel copies one of its neighbors, so the picture keeps the game's
// palette. Edges and diagonals look smoother and nothing gets blurry. This is
// the Scale2x / Scale3x algorithm, working on palette indexes.
static void svga_enlarge(const unsigned char* src, int srcPitch, int width, int height, const Uint32* palette, Uint32* dest, int destPitch, int factor)
{
    for (int y = 0; y < height; y++) {
        const unsigned char* row = src + srcPitch * y;
        const unsigned char* above = y > 0 ? row - srcPitch : row;
        const unsigned char* below = y < height - 1 ? row + srcPitch : row;

        Uint32* out = dest + (y * factor) * destPitch;

        for (int x = 0; x < width; x++) {
            int left = x > 0 ? x - 1 : x;
            int right = x < width - 1 ? x + 1 : x;

            unsigned char a = above[left];
            unsigned char b = above[x];
            unsigned char c = above[right];
            unsigned char d = row[left];
            unsigned char e = row[x];
            unsigned char f = row[right];
            unsigned char g = below[left];
            unsigned char h = below[x];
            unsigned char i = below[right];

            if (factor == 2) {
                unsigned char e0 = e, e1 = e, e2 = e, e3 = e;
                if (b != h && d != f) {
                    e0 = d == b ? d : e;
                    e1 = b == f ? f : e;
                    e2 = d == h ? d : e;
                    e3 = h == f ? f : e;
                }

                Uint32* top = out + x * 2;
                top[0] = palette[e0];
                top[1] = palette[e1];
                top[destPitch] = palette[e2];
                top[destPitch + 1] = palette[e3];
            } else {
                unsigned char e0 = e, e1 = e, e2 = e, e3 = e, e5 = e, e6 = e, e7 = e, e8 = e;
                if (b != h && d != f) {
                    e0 = d == b ? d : e;
                    e1 = (d == b && e != c) || (b == f && e != a) ? b : e;
                    e2 = b == f ? f : e;
                    e3 = (d == b && e != g) || (d == h && e != a) ? d : e;
                    e5 = (b == f && e != i) || (h == f && e != c) ? f : e;
                    e6 = d == h ? d : e;
                    e7 = (d == h && e != i) || (h == f && e != g) ? h : e;
                    e8 = h == f ? f : e;
                }

                Uint32* top = out + x * 3;
                top[0] = palette[e0];
                top[1] = palette[e1];
                top[2] = palette[e2];
                top[destPitch] = palette[e3];
                top[destPitch + 1] = palette[e];
                top[destPitch + 2] = palette[e5];
                top[destPitch * 2] = palette[e6];
                top[destPitch * 2 + 1] = palette[e7];
                top[destPitch * 2 + 2] = palette[e8];
            }
        }
    }
}

void renderPresent()
{
    if (gArtScale > 1 && gSdlSurface != NULL && gSdlSurface->format->palette != NULL) {
        Uint32 palette[256];
        const SDL_Color* colors = gSdlSurface->format->palette->colors;
        for (int index = 0; index < 256; index++) {
            palette[index] = ((Uint32)colors[index].r << 16) | ((Uint32)colors[index].g << 8) | colors[index].b;
        }

        void* pixels;
        int pitch;
        if (SDL_LockTexture(gSdlTexture, NULL, &pixels, &pitch) == 0) {
            svga_enlarge((const unsigned char*)gSdlSurface->pixels, gSdlSurface->pitch, gSdlSurface->w, gSdlSurface->h, palette, (Uint32*)pixels, pitch / 4, gArtScale);
            SDL_UnlockTexture(gSdlTexture);
        }
    } else {
        SDL_UpdateTexture(gSdlTexture, NULL, gSdlTextureSurface->pixels, gSdlTextureSurface->pitch);
    }

    if (gScaledTexture != NULL) {
        // First pass: enlarge by a whole number with hard edges.
        SDL_SetRenderTarget(gSdlRenderer, gScaledTexture);
        SDL_RenderCopy(gSdlRenderer, gSdlTexture, NULL, NULL);
        SDL_SetRenderTarget(gSdlRenderer, NULL);

        SDL_RenderClear(gSdlRenderer);
        SDL_RenderCopy(gSdlRenderer, gScaledTexture, NULL, NULL);
    } else {
        SDL_RenderClear(gSdlRenderer);
        SDL_RenderCopy(gSdlRenderer, gSdlTexture, NULL, NULL);
    }

    // The focus frame is drawn over the finished picture so the game never
    // has to redraw anything to remove it.
    if (gFocusRectSet) {
        SDL_SetRenderDrawColor(gSdlRenderer, 60, 255, 60, 255);
        SDL_RenderDrawRect(gSdlRenderer, &gFocusRect);

        SDL_Rect inner = { gFocusRect.x + 1, gFocusRect.y + 1, gFocusRect.w - 2, gFocusRect.h - 2 };
        SDL_RenderDrawRect(gSdlRenderer, &inner);

        // `SDL_RenderClear` uses the draw color, so restore black or the next
        // frame's borders turn green.
        SDL_SetRenderDrawColor(gSdlRenderer, 0, 0, 0, 255);
    }

    SDL_RenderPresent(gSdlRenderer);
}

} // namespace fallout

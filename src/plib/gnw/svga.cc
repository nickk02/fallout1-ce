#include "plib/gnw/svga.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/winmain.h"

namespace fallout {

static bool createRenderer(int width, int height);
static void destroyRenderer();
static bool svga_zoom_geometry(int* srcX, int* srcY, float* scale, float* dstX, float* dstY);

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
// The focus highlight: a rounded bar, a circle on a square button, drawn with
// a see-through fill and a soft edge. It is built once per focus rectangle.
#define FOCUS_MARGIN 6
static SDL_Texture* gFocusTexture = NULL;
static bool gFocusTextureStale = true;

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

void svga_minimize()
{
    if (gSdlWindow == NULL) {
        return;
    }

    if ((SDL_GetWindowFlags(gSdlWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        SDL_SetWindowFullscreen(gSdlWindow, 0);
    }

    SDL_MinimizeWindow(gSdlWindow);
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

    int srcX;
    int srcY;
    float scale;
    float dstX;
    float dstY;
    if (svga_zoom_geometry(&srcX, &srcY, &scale, &dstX, &dstY)) {
        logicalX = srcX + (logicalX - dstX) / scale;
        logicalY = srcY + (logicalY - dstY) / scale;
    }

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

    float logicalX = (float)x;
    float logicalY = (float)y;
    int srcX;
    int srcY;
    float scale;
    float dstX;
    float dstY;
    if (svga_zoom_geometry(&srcX, &srcY, &scale, &dstX, &dstY)) {
        logicalX = dstX + ((float)x - srcX) * scale;
        logicalY = dstY + ((float)y - srcY) * scale;
    }

    int windowX;
    int windowY;
    SDL_RenderLogicalToWindow(gSdlRenderer, logicalX, logicalY, &windowX, &windowY);
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

    gArtScale = (gScaleFilter == 3 || gScaleFilter == 5) ? 2 : (gScaleFilter == 4 ? 3 : 1);
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
    if (gFocusTexture != NULL) {
        SDL_DestroyTexture(gFocusTexture);
        gFocusTexture = NULL;
        gFocusTextureStale = true;
    }

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
    gScaleFilter = (gScaleFilter + 1) % 6;
    handleWindowSizeChanged();

    // The new picture buffer starts empty.
    win_refresh_all(&scr_size);
}

void handleWindowSizeChanged()
{
    destroyRenderer();
    createRenderer(screenGetWidth(), screenGetHeight());
}

static bool gMenuZoom = false;

// The part of the game screen the zoom shows. By default the 640 by 480 menu
// picture in the middle; a movie sets the rectangle it plays in.
static bool gZoomCustom = false;
static int gZoomX = 0;
static int gZoomY = 0;
static int gZoomW = 640;
static int gZoomH = 480;

static void svga_zoom_size(int* width, int* height)
{
    *width = gZoomCustom ? gZoomW : 640;
    *height = gZoomCustom ? gZoomH : 480;
}

static bool svga_zoom_geometry(int* srcX, int* srcY, float* scale, float* dstX, float* dstY)
{
    int width = screenGetWidth();
    int height = screenGetHeight();
    if (!gMenuZoom || (width <= 640 && height <= 480)) {
        return false;
    }

    int areaW;
    int areaH;
    svga_zoom_size(&areaW, &areaH);
    if (areaW <= 0 || areaH <= 0) {
        return false;
    }

    float scaleX = (float)width / (float)areaW;
    float scaleY = (float)height / (float)areaH;
    float fit = scaleX < scaleY ? scaleX : scaleY;

    *srcX = gZoomCustom ? gZoomX : (width - 640) / 2;
    *srcY = gZoomCustom ? gZoomY : (height - 480) / 2;
    *scale = fit;
    *dstX = ((float)width - (float)areaW * fit) / 2.0f;
    *dstY = ((float)height - (float)areaH * fit) / 2.0f;
    return true;
}

void svga_set_menu_zoom(bool enabled)
{
    gMenuZoom = enabled;
    gZoomCustom = false;
}

void svga_set_menu_zoom_area(int x, int y, int width, int height)
{
    gZoomCustom = true;
    gZoomX = x;
    gZoomY = y;
    gZoomW = width;
    gZoomH = height;
}

bool svga_menu_zoom_enabled()
{
    return gMenuZoom;
}

bool svga_menu_zoom_active()
{
    int srcX;
    int srcY;
    float scale;
    float dstX;
    float dstY;
    return svga_zoom_geometry(&srcX, &srcY, &scale, &dstX, &dstY);
}

float svga_menu_zoom_scale()
{
    int srcX;
    int srcY;
    float scale;
    float dstX;
    float dstY;
    return svga_zoom_geometry(&srcX, &srcY, &scale, &dstX, &dstY) ? scale : 1.0f;
}

void svga_menu_zoom_area(int* left, int* top, int* right, int* bottom)
{
    int srcX;
    int srcY;
    float scale;
    float dstX;
    float dstY;
    if (svga_zoom_geometry(&srcX, &srcY, &scale, &dstX, &dstY)) {
        int areaW;
        int areaH;
        svga_zoom_size(&areaW, &areaH);
        *left = srcX;
        *top = srcY;
        *right = srcX + areaW - 1;
        *bottom = srcY + areaH - 1;
    }
}

static bool gFocusRectSet = false;
static SDL_Rect gFocusRect;

void svga_set_focus_rect(const Rect* rect)
{
    if (rect == NULL) {
        gFocusRectSet = false;
        return;
    }

    SDL_Rect next = { rect->ulx, rect->uly, rect->lrx - rect->ulx + 1, rect->lry - rect->uly + 1 };
    if (!gFocusRectSet || next.w != gFocusRect.w || next.h != gFocusRect.h) {
        gFocusTextureStale = true;
    }

    gFocusRect = next;
    gFocusRectSet = true;
}

static void svga_build_focus_texture()
{
    if (gFocusTexture != NULL) {
        SDL_DestroyTexture(gFocusTexture);
        gFocusTexture = NULL;
    }

    int width = gFocusRect.w + FOCUS_MARGIN * 2;
    int height = gFocusRect.h + FOCUS_MARGIN * 2;

    gFocusTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
    if (gFocusTexture == NULL) {
        return;
    }

    SDL_SetTextureBlendMode(gFocusTexture, SDL_BLENDMODE_BLEND);

    Uint32* pixels = (Uint32*)malloc(sizeof(Uint32) * width * height);
    if (pixels == NULL) {
        return;
    }

    // A circle over a roughly square button, a rounded rectangle over the rest.
    int shortSide = gFocusRect.w < gFocusRect.h ? gFocusRect.w : gFocusRect.h;
    int longSide = gFocusRect.w < gFocusRect.h ? gFocusRect.h : gFocusRect.w;
    bool circle = longSide * 10 <= shortSide * 13;
    float radius = circle ? shortSide / 2.0f : (shortSide / 4.0f < 3.0f ? 3.0f : (shortSide / 4.0f > 8.0f ? 8.0f : shortSide / 4.0f));

    // The rounded shape is a rectangle shrunk by the radius, then grown again.
    float left = FOCUS_MARGIN + radius;
    float right = FOCUS_MARGIN + gFocusRect.w - radius;
    float top = FOCUS_MARGIN + radius;
    float bottom = FOCUS_MARGIN + gFocusRect.h - radius;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float px = x + 0.5f;
            float py = y + 0.5f;
            float nearestX = px < left ? left : (px > right ? right : px);
            float nearestY = py < top ? top : (py > bottom ? bottom : py);
            float dx = px - nearestX;
            float dy = py - nearestY;
            float signedDistance = sqrtf(dx * dx + dy * dy) - radius;

            float alpha = 0.0f;
            if (signedDistance < 0.0f) {
                alpha = 0.16f;
            }

            float edge = fabsf(signedDistance);
            if (edge < 1.5f) {
                float line = 0.7f * (1.0f - edge / 1.5f);
                alpha = alpha > line ? alpha : line;
            }

            if (signedDistance > 0.0f && signedDistance < FOCUS_MARGIN) {
                float glow = 0.22f * (1.0f - signedDistance / FOCUS_MARGIN);
                glow *= glow * 4.5f;
                alpha = alpha > glow ? alpha : glow;
            }

            Uint32 a = (Uint32)(alpha * 255.0f);
            pixels[y * width + x] = (a << 24) | (140u << 16) | (255u << 8) | 170u;
        }
    }

    SDL_UpdateTexture(gFocusTexture, NULL, pixels, width * (int)sizeof(Uint32));
    free(pixels);
    gFocusTextureStale = false;
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

// Like the enlargement above, but the corner of a diagonal edge is a blend of
// the two colors instead of a copy of one, so slanted lines look anti-aliased.
// Colors only need to be similar, not equal, so soft gradients in the art are
// treated as edges too. It works in full color, so it is only used for output.
static inline Uint32 svga_mix(Uint32 first, Uint32 second, int secondWeight)
{
    // secondWeight is out of 4.
    Uint32 rb = ((first & 0xFF00FF) * (4 - secondWeight) + (second & 0xFF00FF) * secondWeight) >> 2;
    Uint32 g = ((first & 0x00FF00) * (4 - secondWeight) + (second & 0x00FF00) * secondWeight) >> 2;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

static void svga_enlarge_smooth(const unsigned char* src, int srcPitch, int width, int height, const Uint32* palette, Uint32* dest, int destPitch)
{
    // Which palette colors count as the same region.
    static unsigned char similar[256][256];
    for (int first = 0; first < 256; first++) {
        int r1 = (palette[first] >> 16) & 0xFF;
        int g1 = (palette[first] >> 8) & 0xFF;
        int b1 = palette[first] & 0xFF;
        for (int second = first; second < 256; second++) {
            int r2 = (palette[second] >> 16) & 0xFF;
            int g2 = (palette[second] >> 8) & 0xFF;
            int b2 = palette[second] & 0xFF;
            int distance = abs(r1 - r2) + abs(g1 - g2) + abs(b1 - b2);
            unsigned char value = distance <= 40 ? 1 : 0;
            similar[first][second] = value;
            similar[second][first] = value;
        }
    }

    for (int y = 0; y < height; y++) {
        const unsigned char* row = src + srcPitch * y;
        const unsigned char* above = y > 0 ? row - srcPitch : row;
        const unsigned char* below = y < height - 1 ? row + srcPitch : row;

        Uint32* out = dest + (y * 2) * destPitch;

        for (int x = 0; x < width; x++) {
            int left = x > 0 ? x - 1 : x;
            int right = x < width - 1 ? x + 1 : x;

            unsigned char b = above[x];
            unsigned char d = row[left];
            unsigned char e = row[x];
            unsigned char f = row[right];
            unsigned char h = below[x];

            Uint32 center = palette[e];
            Uint32 c0 = center;
            Uint32 c1 = center;
            Uint32 c2 = center;
            Uint32 c3 = center;

            // A corner is blended toward its two neighbors when they agree with
            // each other and differ from the pixels on the other side.
            if (similar[d][b] && !similar[b][f] && !similar[d][h] && !similar[e][d]) {
                c0 = svga_mix(center, svga_mix(palette[d], palette[b], 2), 3);
            }

            if (similar[b][f] && !similar[b][d] && !similar[f][h] && !similar[e][b]) {
                c1 = svga_mix(center, svga_mix(palette[b], palette[f], 2), 3);
            }

            if (similar[d][h] && !similar[d][b] && !similar[h][f] && !similar[e][d]) {
                c2 = svga_mix(center, svga_mix(palette[d], palette[h], 2), 3);
            }

            if (similar[h][f] && !similar[d][h] && !similar[b][f] && !similar[e][h]) {
                c3 = svga_mix(center, svga_mix(palette[h], palette[f], 2), 3);
            }

            Uint32* top = out + x * 2;
            top[0] = c0;
            top[1] = c1;
            top[destPitch] = c2;
            top[destPitch + 1] = c3;
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
            if (gScaleFilter == 5) {
                svga_enlarge_smooth((const unsigned char*)gSdlSurface->pixels, gSdlSurface->pitch, gSdlSurface->w, gSdlSurface->h, palette, (Uint32*)pixels, pitch / 4);
            } else {
                svga_enlarge((const unsigned char*)gSdlSurface->pixels, gSdlSurface->pitch, gSdlSurface->w, gSdlSurface->h, palette, (Uint32*)pixels, pitch / 4, gArtScale);
            }
            SDL_UnlockTexture(gSdlTexture);
        }
    } else {
        SDL_UpdateTexture(gSdlTexture, NULL, gSdlTextureSurface->pixels, gSdlTextureSurface->pitch);
    }

    SDL_Texture* shown = gSdlTexture;
    if (gScaledTexture != NULL) {
        // First pass: enlarge by a whole number with hard edges.
        SDL_SetRenderTarget(gSdlRenderer, gScaledTexture);
        SDL_RenderCopy(gSdlRenderer, gSdlTexture, NULL, NULL);
        SDL_SetRenderTarget(gSdlRenderer, NULL);
        shown = gScaledTexture;
    }

    SDL_RenderClear(gSdlRenderer);

    int zoomX;
    int zoomY;
    float zoomScale;
    float zoomDstX;
    float zoomDstY;
    if (svga_zoom_geometry(&zoomX, &zoomY, &zoomScale, &zoomDstX, &zoomDstY)) {
        // Only the menu's 640 by 480 picture is shown, enlarged to fit.
        int textureWidth;
        int textureHeight;
        SDL_QueryTexture(shown, NULL, NULL, &textureWidth, &textureHeight);
        float factor = (float)textureWidth / (float)screenGetWidth();

        int areaW;
        int areaH;
        svga_zoom_size(&areaW, &areaH);
        SDL_Rect source = { (int)(zoomX * factor), (int)(zoomY * factor), (int)(areaW * factor), (int)(areaH * factor) };
        SDL_FRect destination = { zoomDstX, zoomDstY, (float)areaW * zoomScale, (float)areaH * zoomScale };
        SDL_RenderCopyF(gSdlRenderer, shown, &source, &destination);
    } else {
        SDL_RenderCopy(gSdlRenderer, shown, NULL, NULL);
    }

    // The focus frame is drawn over the finished picture so the game never
    // has to redraw anything to remove it.
    if (gFocusRectSet) {
        if (gFocusTexture == NULL || gFocusTextureStale) {
            svga_build_focus_texture();
        }

        if (gFocusTexture != NULL) {
            SDL_FRect destination = { (float)(gFocusRect.x - FOCUS_MARGIN), (float)(gFocusRect.y - FOCUS_MARGIN), (float)(gFocusRect.w + FOCUS_MARGIN * 2), (float)(gFocusRect.h + FOCUS_MARGIN * 2) };
            if (zoomScale != 0.0f && svga_menu_zoom_active()) {
                destination.x = zoomDstX + ((float)(gFocusRect.x - FOCUS_MARGIN) - zoomX) * zoomScale;
                destination.y = zoomDstY + ((float)(gFocusRect.y - FOCUS_MARGIN) - zoomY) * zoomScale;
                destination.w *= zoomScale;
                destination.h *= zoomScale;
            }
            SDL_RenderCopyF(gSdlRenderer, gFocusTexture, NULL, &destination);
        }
    }

    SDL_RenderPresent(gSdlRenderer);
}

} // namespace fallout

#ifndef __FT2_VIDEO_H
#define __FT2_VIDEO_H

#include <stdint.h>
#include "ft2_header.h"
#include "ft2_palette.h"
#include "ft2_audio.h"

#define RGB_R(x) (((x) >> 16) & 0xFF)
#define RGB_G(x) (((x) >>  8) & 0xFF)
#define RGB_B(x) ( (x)        & 0xFF)
#define TO_RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))
#define P6_TO_P8(x) (uint8_t)(((x) * (255.0f /  63.0f)) + 0.5f)
#define P8_TO_P6(x) (uint8_t)(((x) * (63.0f  / 255.0f)) + 0.5f)

enum
{
    SPRITE_LEFT_LOOP_PIN  = 0,
    SPRITE_RIGHT_LOOP_PIN = 1,
    SPRITE_TEXT_CURSOR    = 2,
    SPRITE_MOUSE_POINTER  = 3, /* priority above all other sprites */

    SPRITE_NUM
};

struct video_t
{
    uint8_t customContrasts[2]; /* for palette */
    uint8_t upscaleFactor, fullscreen, vsync60HzPresent;
    int32_t renderW, renderH;
    uint32_t *frameBuffer, palette[PAL_NUM], vblankTimeLen, vblankTimeLenFrac;
    uint32_t xScaleMul, yScaleMul;
#ifdef _WIN32
    HWND hWnd;
#endif
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Surface *iconSurface;
} video;

typedef struct
{
    uint32_t *refreshBuffer;
    const uint8_t *data;
    int8_t visible;
    int16_t newX, newY, x, y;
    uint16_t w, h;
} sprite_t;

void flipFrame(void);
void showErrorMsgBox(const char *fmt, ...);
void updateWindowTitle(uint8_t forceUpdate);
void setPalettePreset(int16_t palettePreset);
void updatePaletteContrast(void);
void handleScopesFromChQueue(chSyncData_t *chSyncData, uint8_t *scopeUpdateStatus);

int8_t setupWindow(void);
int8_t setupRenderer(void);
void closeVideo(void);
void setLeftLoopPinState(int8_t clicked);
void setRightLoopPinState(int8_t clicked);
int32_t getSpritePosX(uint8_t sprite);
void eraseSprites(void);
void renderLoopPins(void);
void renderSprites(void);
int8_t setupSprites(void);
void freeSprites(void);
void setSpritePos(uint8_t sprite, int16_t x, int16_t y);
void changeSpriteData(uint8_t sprite, const uint8_t *data);
void hideSprite(uint8_t sprite);
void handleRedrawing(void);
void enterFullscreen(void);
void leaveFullScreen(void);
void toggleFullScreen(void);
void setWindowSizeFromConfig(uint8_t updateRenderer);
uint8_t recreateTexture(void);
void setupWaitVBL(void);
void waitVBL(void);

#endif

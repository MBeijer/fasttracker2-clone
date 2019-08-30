// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <SDL2/SDL_syswm.h>
#else
#include <unistd.h> // usleep()
#endif
#include "ft2_header.h"
#include "ft2_config.h"
#include "ft2_gfxdata.h"
#include "ft2_gui.h"
#include "ft2_video.h"
#include "ft2_events.h"
#include "ft2_mouse.h"
#include "ft2_scopes.h"
#include "ft2_pattern_ed.h"
#include "ft2_sample_ed.h"
#include "ft2_nibbles.h"
#include "ft2_inst_ed.h"
#include "ft2_diskop.h"
#include "ft2_about.h"
#include "ft2_trim.h"
#include "ft2_sampling.h"
#include "ft2_module_loader.h"
#include "ft2_midi.h"

// for FPS counter
#define FPS_SCAN_FRAMES 60
#define FPS_RENDER_W 280
#define FPS_RENDER_H (((FONT1_CHAR_H + 1) * 8) + 1)
#define FPS_RENDER_X 2
#define FPS_RENDER_Y 2

static const uint8_t textCursorData[12] =
{
	PAL_FORGRND, PAL_FORGRND, PAL_FORGRND,
	PAL_FORGRND, PAL_FORGRND, PAL_FORGRND,
	PAL_FORGRND, PAL_FORGRND, PAL_FORGRND,
	PAL_FORGRND, PAL_FORGRND, PAL_FORGRND
};

static bool songIsModified;
static char buf[1024], wndTitle[128 + PATH_MAX];
static uint64_t frameStartTime, timeNext64, timeNext64Frac;
static sprite_t sprites[SPRITE_NUM];
static double dRunningFPS, dFrameTime, dAvgFPS;

static void drawReplayerData(void);

void resetFPSCounter(void)
{
	editor.framesPassed = 0;
	buf[0] = '\0';
	dRunningFPS = VBLANK_HZ;
	dFrameTime = 1000.0 / VBLANK_HZ;
}

void beginFPSCounter(void)
{
	if (video.showFPSCounter)
		frameStartTime = SDL_GetPerformanceCounter();
}

static void drawFPSCounter(void)
{
	char *textPtr, ch;
	uint16_t xPos, yPos;
	double dRefreshRate, dAudLatency;

	if (!video.showFPSCounter)
		return;

	if (editor.framesPassed >= FPS_SCAN_FRAMES && (editor.framesPassed % FPS_SCAN_FRAMES) == 0)
	{
		dAvgFPS = dRunningFPS * (1.0 / FPS_SCAN_FRAMES);
		if (dAvgFPS < 0.0 || dAvgFPS > 99999999.9999)
			dAvgFPS = 99999999.9999; // prevent number from overflowing text box

		dRunningFPS = 0.0;
	}

	clearRect(FPS_RENDER_X+2, FPS_RENDER_Y+2, FPS_RENDER_W, FPS_RENDER_H);
	vLineDouble(FPS_RENDER_X, FPS_RENDER_Y+1, FPS_RENDER_H+2, PAL_FORGRND);
	vLineDouble(FPS_RENDER_X+FPS_RENDER_W, FPS_RENDER_Y+1, FPS_RENDER_H+2, PAL_FORGRND);
	hLineDouble(FPS_RENDER_X+1, FPS_RENDER_Y, FPS_RENDER_W, PAL_FORGRND);
	hLineDouble(FPS_RENDER_X+1, FPS_RENDER_Y+FPS_RENDER_H+2, FPS_RENDER_W, PAL_FORGRND);

	// test if enough data is collected yet
	if (editor.framesPassed < FPS_SCAN_FRAMES)
	{
		textOut(FPS_RENDER_X+53, FPS_RENDER_Y+39, PAL_FORGRND, "Collecting frame information...");
		return;
	}

	dRefreshRate = video.dMonitorRefreshRate;
	if (dRefreshRate < 0.0 || dRefreshRate > 9999.9)
		dRefreshRate = 9999.9; // prevent number from overflowing text box

	dAudLatency = audio.dAudioLatencyMs;
	if (dAudLatency < 0.0 || dAudLatency > 999999999.9999)
		dAudLatency = 999999999.9999; // prevent number from overflowing text box

	sprintf(buf, "Frames per second: %.4f\n" \
	             "Monitor refresh rate: %.1fHz (+/-)\n" \
	             "59..61Hz GPU VSync used: %s\n" \
	             "Audio frequency: %.1fkHz (expected %.1fkHz)\n" \
	             "Audio buffer samples: %d (expected %d)\n" \
	             "Audio channels: %d (expected %d)\n" \
	             "Audio latency: %.1fms (expected %.1fms)\n" \
	             "Press CTRL+SHIFT+F to close this box.\n",
	             dAvgFPS, dRefreshRate,
	             video.vsync60HzPresent ? "yes" : "no",
	             audio.haveFreq * (1.0 / 1000.0), audio.wantFreq * (1.0 / 1000.0),
	             audio.haveSamples, audio.wantSamples,
	             audio.haveChannels, audio.wantChannels,
	             dAudLatency, ((audio.wantSamples * 1000.0) / audio.wantFreq));

	// draw text

	xPos = FPS_RENDER_X + 3;
	yPos = FPS_RENDER_Y + 3;

	textPtr = buf;
	while (*textPtr != '\0')
	{
		ch = *textPtr++;
		if (ch == '\n')
		{
			yPos += FONT1_CHAR_H+1;
			xPos = FPS_RENDER_X + 3;
			continue;
		}

		charOut(xPos, yPos, PAL_FORGRND, ch);
		xPos += charWidth(ch);
	}
}

void endFPSCounter(void)
{
	uint64_t frameTimeDiff;
	double dHz;

	if (!video.showFPSCounter || frameStartTime == 0)
		return;

	frameTimeDiff = SDL_GetPerformanceCounter() - frameStartTime;
	dHz = 1000.0 / (frameTimeDiff * editor.dPerfFreqMulMs);
	dRunningFPS += dHz;
}

void flipFrame(void)
{
	uint32_t windowFlags =
#if SDL_VERSION_ATLEAST(2,0,0)
			SDL_GetWindowFlags(video.window);
#else
	0;
#endif

	renderSprites();
	//drawFPSCounter();
	//SDL_UpdateTexture(video.texture, NULL, video.frameBuffer, SCREEN_W * sizeof (int32_t));
	//SDL_RenderClear(video.renderer);
	//SDL_RenderCopy(video.renderer, video.texture, NULL, NULL);
	//SDL_RenderPresent(video.renderer);

	SDL_Surface* surface = SDL_GetWindowSurface(video.window);
	SDL_Surface* testSurface = SDL_CreateRGBSurfaceWithFormatFrom(video.frameBuffer,SCREEN_W, SCREEN_H, 32, SCREEN_W * sizeof(int32_t),0);

	//SDL_FillRect( surface, NULL, SDL_MapRGBA( testSurface->format, 0, 0, 0, 0) );
	SDL_FillRect( surface, NULL, SDL_MapRGB( surface->format, 0, 0, 0 ) );

	SDL_BlitSurface(testSurface, NULL, surface, NULL);

	SDL_Flip(surface);
	//SDL_BlitSurface(testSurface, NULL, surface, NULL);
	//SDL_Flip(surface);


	if( SDL_GetTicks() < 1000 / 60 )
	{
		SDL_Delay( ( 1000 / 60 ) - SDL_GetTicks() );
	}

	eraseSprites();

	if (!video.vsync60HzPresent)
	{
		waitVBL(); // we have no VSync, do crude thread sleeping to sync to ~60Hz
	}
	else
	{
		/* We have VSync, but it can unexpectedly get inactive in certain scenarios.
		** We have to force thread sleeping (to ~60Hz) if so.
		*/
#ifdef __APPLE__
		// macOS: VSync gets disabled if the window is 100% covered by another window. Let's add a (crude) fix:
		if ((windowFlags & SDL_WINDOW_MINIMIZED) || !(windowFlags & SDL_WINDOW_INPUT_FOCUS))
			waitVBL();
#elif __unix__
		// *NIX: VSync gets disabled in fullscreen mode (at least on some distros/systems). Let's add a fix:
		if ((windowFlags & SDL_WINDOW_MINIMIZED) || video.fullscreen)
			waitVBL();
#else
		if (!(windowFlags & SDL_WINDOW_MINIMIZED))
			waitVBL();
#endif
	}

	editor.framesPassed++;
}

void showErrorMsgBox(const char *fmt, ...)
{
	char strBuf[256];
	va_list args;

	// format the text string
	va_start(args, fmt);
	vsnprintf(strBuf, sizeof (strBuf), fmt, args);
	va_end(args);

	// window can be NULL here, no problem...
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", strBuf, video.window);
}

void updateRenderSizeVars(void)
{
	int32_t di;
#ifdef __APPLE__
	int32_t actualScreenW, actualScreenH;
	double dXUpscale, dYUpscale;
#endif
	float fXScale, fYScale;
	SDL_DisplayMode dm;

#if SDL_VERSION_ATLEAST(2,0,0)
	di = SDL_GetWindowDisplayIndex(video.window);
	if (di < 0)
		di = 0; // return display index 0 (default) on error

	SDL_GetDesktopDisplayMode(di, &dm);
#endif
	video.displayW = dm.w;
	video.displayH = dm.h;

	if (video.fullscreen)
	{
		if (config.windowFlags & FILTERING)
		{
			video.renderW = video.displayW;
			video.renderH = video.displayH;
			video.renderX = 0;
			video.renderY = 0;
		}
		else
		{
			SDL_RenderGetScale(video.renderer, &fXScale, &fYScale);

			video.renderW = (int32_t)(SCREEN_W * fXScale);
			video.renderH = (int32_t)(SCREEN_H * fYScale);

#ifdef __APPLE__
			// retina high-DPI hackery (SDL2 is bad at reporting actual rendering sizes on macOS w/ high-DPI)
			SDL_GL_GetDrawableSize(video.window, &actualScreenW, &actualScreenH);

			dXUpscale = (double)actualScreenW / video.displayW;
			dYUpscale = (double)actualScreenH / video.displayH;

			// downscale back to correct sizes
			if (dXUpscale != 0.0) video.renderW = (int32_t)(video.renderW / dXUpscale);
			if (dYUpscale != 0.0) video.renderH = (int32_t)(video.renderH / dYUpscale);
#endif
			video.renderX = (video.displayW - video.renderW) / 2;
			video.renderY = (video.displayH - video.renderH) / 2;
		}
	}
	else
	{
		SDL_GetWindowSize(video.window, &video.renderW, &video.renderH);

		video.renderX = 0;
		video.renderY = 0;
	}
}

void enterFullscreen(void)
{
	SDL_DisplayMode dm;

	strcpy(editor.ui.fullscreenButtonText, "Go windowed");
	if (editor.ui.configScreenShown && editor.currConfigScreen == CONFIG_SCREEN_MISCELLANEOUS)
		showConfigScreen(); // redraw so that we can see the new button text
#if SDL_VERSION_ATLEAST(2,0,0)
	if (config.windowFlags & FILTERING)
	{
		SDL_GetDesktopDisplayMode(0, &dm);
		SDL_RenderSetLogicalSize(video.renderer, dm.w, dm.h);
	}
	else
	{
		SDL_RenderSetLogicalSize(video.renderer, SCREEN_W, SCREEN_H);
	}

	SDL_SetWindowSize(video.window, SCREEN_W, SCREEN_H);
	SDL_SetWindowFullscreen(video.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
	SDL_SetWindowGrab(video.window, SDL_TRUE);
#endif
	updateRenderSizeVars();
	updateMouseScaling();
	setMousePosToCenter();
}

void leaveFullScreen(void)
{
	strcpy(editor.ui.fullscreenButtonText, "Go fullscreen");
	if (editor.ui.configScreenShown && editor.currConfigScreen == CONFIG_SCREEN_MISCELLANEOUS)
		showConfigScreen(); // redraw so that we can see the new button text

#if SDL_VERSION_ATLEAST(2,0,0)
	SDL_SetWindowFullscreen(video.window, 0);
	SDL_RenderSetLogicalSize(video.renderer, SCREEN_W, SCREEN_H);

	setWindowSizeFromConfig(false); // also updates mouse scaling and render size vars
	SDL_SetWindowSize(video.window, SCREEN_W * video.upscaleFactor, SCREEN_H * video.upscaleFactor);
	SDL_SetWindowPosition(video.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	SDL_SetWindowGrab(video.window, SDL_FALSE);
#endif

	updateRenderSizeVars();
	updateMouseScaling();
	setMousePosToCenter();
}

void toggleFullScreen(void)
{
	video.fullscreen ^= 1;

	if (video.fullscreen)
		enterFullscreen();
	else
		leaveFullScreen();
}

bool setupSprites(void)
{
	sprite_t *s;

	memset(sprites, 0, sizeof (sprites));

	s = &sprites[SPRITE_MOUSE_POINTER];
	s->data = mouseCursors;
	s->w = MOUSE_CURSOR_W;
	s->h = MOUSE_CURSOR_H;

	s = &sprites[SPRITE_LEFT_LOOP_PIN];
	s->data = leftLoopPinUnclicked;
	s->w = 16;
	s->h = SAMPLE_AREA_HEIGHT;

	s = &sprites[SPRITE_RIGHT_LOOP_PIN];
	s->data = rightLoopPinUnclicked;
	s->w = 16;
	s->h = SAMPLE_AREA_HEIGHT;

	s = &sprites[SPRITE_TEXT_CURSOR];
	s->data = textCursorData;
	s->w = 1;
	s->h = 12;

	hideSprite(SPRITE_MOUSE_POINTER);
	hideSprite(SPRITE_LEFT_LOOP_PIN);
	hideSprite(SPRITE_RIGHT_LOOP_PIN);
	hideSprite(SPRITE_TEXT_CURSOR);

	// setup refresh buffer (used to clear sprites after each frame)
	for (uint32_t i = 0; i < SPRITE_NUM; i++)
	{
		sprites[i].refreshBuffer = (uint32_t *)malloc((sprites[i].w * sprites[i].h) * sizeof (int32_t));
		if (sprites[i].refreshBuffer == NULL)
			return false;
	}

	return true;
}

void changeSpriteData(uint8_t sprite, const uint8_t *data)
{
	sprites[sprite].data = data;
	memset(sprites[sprite].refreshBuffer, 0, sprites[sprite].w * sprites[sprite].h * sizeof (int32_t));
}

void freeSprites(void)
{
	for (uint32_t i = 0; i < SPRITE_NUM; i++)
	{
		if (sprites[i].refreshBuffer != NULL)
		{
			free(sprites[i].refreshBuffer);
			sprites[i].refreshBuffer = NULL;
		}
	}
}

void setLeftLoopPinState(bool clicked)
{
	changeSpriteData(SPRITE_LEFT_LOOP_PIN, clicked ? leftLoopPinClicked : leftLoopPinUnclicked);
}

void setRightLoopPinState(bool clicked)
{
	changeSpriteData(SPRITE_RIGHT_LOOP_PIN, clicked ? rightLoopPinClicked : rightLoopPinUnclicked);
}

int32_t getSpritePosX(uint8_t sprite)
{
	return sprites[sprite].x;
}

void setSpritePos(uint8_t sprite, int16_t x, int16_t y)
{
	sprites[sprite].newX = x;
	sprites[sprite].newY = y;
}

void hideSprite(uint8_t sprite)
{
	sprites[sprite].newX = SCREEN_W;
}

void eraseSprites(void)
{
	int8_t i;
	register int32_t x, y, sw, sh, srcPitch, dstPitch;
	const uint32_t *src32;
	uint32_t *dst32;
	sprite_t *s;

	for (i = (SPRITE_NUM - 1); i >= 0; i--) // erasing must be done in reverse order
	{
		s = &sprites[i];
		if (s->x >= SCREEN_W) // sprite is hidden, don't erase
			continue;

		assert(s->y >= 0 && s->refreshBuffer != NULL);

		sw = s->w;
		sh = s->h;
		x  = s->x;
		y  = s->y;

		// if x is negative, adjust variables (can only happen on loop pins in smp. ed.)
		if (x < 0)
		{
			sw += x; // subtraction
			x = 0;
		}

		src32 = s->refreshBuffer;
		dst32 = &video.frameBuffer[(y * SCREEN_W) + x];

		if (y+sh >= SCREEN_H) sh = SCREEN_H - y;
		if (x+sw >= SCREEN_W) sw = SCREEN_W - x;

		srcPitch = s->w - sw;
		dstPitch = SCREEN_W - sw;

		for (y = 0; y < sh; y++)
		{
			for (x = 0; x < sw; x++)
				*dst32++ = *src32++;

			src32 += srcPitch;
			dst32 += dstPitch;
		}
	}
}

void renderSprites(void)
{
	const uint8_t *src8;
	register int32_t x, y, sw, sh, srcPitch, dstPitch;
	uint32_t i, *clr32, *dst32, windowFlags;
	sprite_t *s;

	for (i = 0; i < SPRITE_NUM; i++)
	{
		if (i == SPRITE_LEFT_LOOP_PIN || i == SPRITE_RIGHT_LOOP_PIN)
			continue; // these need special drawing (done elsewhere)

		// don't render the text edit cursor if window is inactive
		if (i == SPRITE_TEXT_CURSOR)
		{
			assert(video.window != NULL);
#if SDL_VERSION_ATLEAST(2,0,0)
			windowFlags = SDL_GetWindowFlags(video.window);
#endif
			if (!(windowFlags & SDL_WINDOW_INPUT_FOCUS))
				continue;
		}

		s = &sprites[i];

		// set new sprite position
		s->x = s->newX;
		s->y = s->newY;

		if (s->x >= SCREEN_W) // sprite is hidden, don't draw nor fill clear buffer
			continue;

		assert(s->x >= 0 && s->y >= 0 && s->data != NULL && s->refreshBuffer != NULL);

		sw = s->w;
		sh = s->h;
		src8 = s->data;
		dst32 = &video.frameBuffer[(s->y * SCREEN_W) + s->x];
		clr32 = s->refreshBuffer;

		// handle xy clipping
		if (s->y+sh >= SCREEN_H) sh = SCREEN_H - s->y;
		if (s->x+sw >= SCREEN_W) sw = SCREEN_W - s->x;

		srcPitch = s->w - sw;
		dstPitch = SCREEN_W - sw;

		if (mouse.mouseOverTextBox && i == SPRITE_MOUSE_POINTER)
		{
			// text edit mouse pointer (has color changing depending on content under it)
			for (y = 0; y < sh; y++)
			{
				for (x = 0; x < sw; x++)
				{
					*clr32++ = *dst32; // fill clear buffer

					if (*src8 != PAL_TRANSPR)
					{
						if (!(*dst32 & 0xFFFFFF) || *dst32 == video.palette[PAL_TEXTMRK])
							*dst32 = 0xB3DBF6;
						else
							*dst32 = 0x004ECE;
					}

					dst32++;
					src8++;
				}

				clr32 += srcPitch;
				src8 += srcPitch;
				dst32 += dstPitch;
			}
		}
		else
		{
			// normal sprites
			for (y = 0; y < sh; y++)
			{
				for (x = 0; x < sw; x++)
				{
					*clr32++ = *dst32; // fill clear buffer

					if (*src8 != PAL_TRANSPR)
					{
						assert(*src8 < PAL_NUM);
						*dst32 = video.palette[*src8];
					}

					dst32++;
					src8++;
				}

				clr32 += srcPitch;
				src8 += srcPitch;
				dst32 += dstPitch;
			}
		}
	}
}

void renderLoopPins(void)
{
	uint8_t pal;
	const uint8_t *src8;
	int32_t sx;
	register int32_t x, y, sw, sh, srcPitch, dstPitch;
	uint32_t *clr32, *dst32;
	sprite_t *s;

	// left loop pin

	s = &sprites[SPRITE_LEFT_LOOP_PIN];
	assert(s->data != NULL && s->refreshBuffer != NULL);

	// set new sprite position
	s->x = s->newX;
	s->y = s->newY;

	if (s->x < SCREEN_W) // loop pin shown?
	{
		sw = s->w;
		sh = s->h;
		sx = s->x;

		src8 = s->data;
		clr32 = s->refreshBuffer;

		// if x is negative, adjust variables
		if (sx < 0)
		{
			sw += sx; // subtraction
			src8 -= sx; // addition
			sx = 0;
		}

		dst32 = &video.frameBuffer[(s->y * SCREEN_W) + sx];

		// handle x clipping
		if (s->x+sw >= SCREEN_W) sw = SCREEN_W - s->x;

		srcPitch = s->w - sw;
		dstPitch = SCREEN_W - sw;

		for (y = 0; y < sh; y++)
		{
			for (x = 0; x < sw; x++)
			{
				*clr32++ = *dst32; // fill clear buffer

				if (*src8 != PAL_TRANSPR)
				{
					assert(*src8 < PAL_NUM);
					*dst32 = video.palette[*src8];
				}

				dst32++;
				src8++;
			}

			src8 += srcPitch;
			clr32 += srcPitch;
			dst32 += dstPitch;
		}
	}

	// right loop pin

	s = &sprites[SPRITE_RIGHT_LOOP_PIN];
	assert(s->data != NULL && s->refreshBuffer != NULL);

	// set new sprite position
	s->x = s->newX;
	s->y = s->newY;

	if (s->x < SCREEN_W) // loop pin shown?
	{
		s->x = s->newX;
		s->y = s->newY;

		sw = s->w;
		sh = s->h;
		sx = s->x;

		src8 = s->data;
		clr32 = s->refreshBuffer;

		// if x is negative, adjust variables
		if (sx < 0)
		{
			sw += sx; // subtraction
			src8 -= sx; // addition
			sx = 0;
		}

		dst32 = &video.frameBuffer[(s->y * SCREEN_W) + sx];

		// handle x clipping
		if (s->x+sw >= SCREEN_W) sw = SCREEN_W - s->x;

		srcPitch = s->w - sw;
		dstPitch = SCREEN_W - sw;

		for (y = 0; y < sh; y++)
		{
			for (x = 0; x < sw; x++)
			{
				*clr32++ = *dst32;

				if (*src8 != PAL_TRANSPR)
				{
					assert(*src8 < PAL_NUM);
					if (y < 9 && *src8 == PAL_LOOPPIN)
					{
						// don't draw marker line on top of left loop pin's thumb graphics
						pal = *dst32 >> 24;
						if (pal != PAL_DESKTOP && pal != PAL_DSKTOP1 && pal != PAL_DSKTOP2)
							*dst32 = video.palette[*src8];
					}
					else
					{
						*dst32 = video.palette[*src8];
					}
				}

				dst32++;
				src8++;
			}

			src8 += srcPitch;
			clr32 += srcPitch;
			dst32 += dstPitch;
		}
	}
}

void setupWaitVBL(void)
{
	// set next frame time
	timeNext64 = SDL_GetPerformanceCounter() + video.vblankTimeLen;
	timeNext64Frac = video.vblankTimeLenFrac;
}

void waitVBL(void)
{
	// this routine almost never delays if we have 60Hz vsync, but it's still needed in some occasions
	int32_t time32;
	uint32_t diff32;
	uint64_t time64;
	time64 = SDL_GetPerformanceCounter();
	if (time64 < timeNext64)
	{
		assert(timeNext64-time64 <= 0xFFFFFFFFULL);
		diff32 = (uint32_t)(timeNext64 - time64);

		// convert and round to microseconds
		time32 = (int32_t)((diff32 * editor.dPerfFreqMulMicro) + 0.5);

		// delay until we have reached next frame
		if (time32 > 0)
			usleep(time32);
	}
	// update next frame time

	timeNext64 += video.vblankTimeLen;

	timeNext64Frac += video.vblankTimeLenFrac;
	if (timeNext64Frac >= (1ULL << 32))
	{
		timeNext64Frac &= 0xFFFFFFFF;
		timeNext64++;
	}
}

void closeVideo(void)
{
	if (video.texture != NULL)
	{
		SDL_DestroyTexture(video.texture);
		video.texture = NULL;
	}

	if (video.renderer != NULL)
	{
		SDL_DestroyRenderer(video.renderer);
		video.renderer = NULL;
	}

	if (video.window != NULL)
	{
		SDL_DestroyWindow(video.window);
		video.window = NULL;
	}

	if (video.frameBuffer != NULL)
	{
		free(video.frameBuffer);
		video.frameBuffer = NULL;
	}
}

void setWindowSizeFromConfig(bool updateRenderer)
{
#define MAX_UPSCALE_FACTOR 16 // 10112x6400 - ought to be good enough for many years to come

	uint8_t i, oldUpscaleFactor;
	SDL_DisplayMode dm;

	oldUpscaleFactor = video.upscaleFactor;
#if SDL_VERSION_ATLEAST(2,0,0)
	if (config.windowFlags & WINSIZE_AUTO)
	{
		// find out which upscaling factor is the biggest to fit on screen
		if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
		{
			for (i = MAX_UPSCALE_FACTOR; i >= 1; i--)
			{
				// slightly bigger than 632x400 because of window title, window borders and taskbar/menu
				if (dm.w >= 640*i && dm.h >= 450*i)
				{
					video.upscaleFactor = i;
					break;
				}
			}

			if (i == 0)
				video.upscaleFactor = 1; // 1x is not going to fit, but use 1x anyways...
		}
		else
		{
			// couldn't get screen resolution, set to 1x
			video.upscaleFactor = 1;
		}
	}
	else if (config.windowFlags & WINSIZE_1X) video.upscaleFactor = 1;
	else if (config.windowFlags & WINSIZE_2X) video.upscaleFactor = 2;
	else if (config.windowFlags & WINSIZE_3X) video.upscaleFactor = 3;
	else if (config.windowFlags & WINSIZE_4X) video.upscaleFactor = 4;
#endif
	if (updateRenderer)
	{
#if SDL_VERSION_ATLEAST(2,0,0)
		SDL_SetWindowSize(video.window, SCREEN_W * video.upscaleFactor, SCREEN_H * video.upscaleFactor);

		if (oldUpscaleFactor != video.upscaleFactor)
			SDL_SetWindowPosition(video.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
#endif
		updateRenderSizeVars();
		updateMouseScaling();
		setMousePosToCenter();
	}
}

void updateWindowTitle(bool forceUpdate)
{
	char *songTitle;

	if (!forceUpdate && songIsModified == song.isModified)
		return; // window title is already set to the same

	songTitle = getCurrSongFilename();
	if (songTitle != NULL)
	{
		if (song.isModified)
			sprintf(wndTitle, "Fasttracker II clone (beta #%d) - \"%s\" (unsaved)", BETA_VERSION, songTitle);
		else
			sprintf(wndTitle, "Fasttracker II clone (beta #%d) - \"%s\"", BETA_VERSION, songTitle);
	}
	else
	{
		if (song.isModified)
			sprintf(wndTitle, "Fasttracker II clone (beta #%d) - \"untitled\" (unsaved)", BETA_VERSION);
		else
			sprintf(wndTitle, "Fasttracker II clone (beta #%d) - \"untitled\"", BETA_VERSION);
	}

	SDL_SetWindowTitle(video.window, wndTitle);
	songIsModified = song.isModified;
}

bool recreateTexture(void)
{
	if (video.texture != NULL)
	{
		SDL_DestroyTexture(video.texture);
		video.texture = NULL;
	}

	if (config.windowFlags & FILTERING)
		SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "best");
	else
		SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "nearest");

	video.texture = SDL_CreateTexture(video.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
	if (video.texture == NULL)
	{
		showErrorMsgBox("Couldn't create a %dx%d GPU texture:\n%s\n\nIs your GPU (+ driver) too old?", SCREEN_W, SCREEN_H, SDL_GetError());
		return false;
	}
#if SDL_VERSION_ATLEAST(2,0,0)
	SDL_SetTextureBlendMode(video.texture, SDL_BLENDMODE_NONE);
#endif
	return true;
}

bool setupWindow(void)
{
	uint32_t windowFlags;
	SDL_DisplayMode dm;

	video.vsync60HzPresent = false;
	windowFlags = SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI;

	setWindowSizeFromConfig(false);

#if SDL_PATCHLEVEL >= 5 && SDL_VERSION_ATLEAST(2,0,5)// SDL 2.0.5 or later
	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif
#if SDL_VERSION_ATLEAST(2,0,0)
	SDL_GetDesktopDisplayMode(0, &dm);
#endif
	video.dMonitorRefreshRate = (double)dm.refresh_rate;

	if (dm.refresh_rate >= 59 && dm.refresh_rate <= 61)
		video.vsync60HzPresent = true;

	if (config.windowFlags & FORCE_VSYNC_OFF)
		video.vsync60HzPresent = false;

	video.window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
					SCREEN_W /* video.upscaleFactor*/, SCREEN_H /* video.upscaleFactor*/,
					windowFlags);

	if (video.window == NULL)
	{
		showErrorMsgBox("Couldn't create SDL window:\n%s", SDL_GetError());
		return false;
	}

	updateWindowTitle(true);

	return true;
}

bool setupRenderer(void)
{
	uint32_t rendererFlags;

	rendererFlags = 0;
	if (video.vsync60HzPresent)
		rendererFlags |= SDL_RENDERER_PRESENTVSYNC;

	video.renderer = SDL_CreateRenderer(video.window, -1, rendererFlags);
	if (video.renderer == NULL)
	{
		if (video.vsync60HzPresent)
		{
			// try again without vsync flag
			video.vsync60HzPresent = false;

			rendererFlags &= ~SDL_RENDERER_PRESENTVSYNC;
			video.renderer = SDL_CreateRenderer(video.window, -1, rendererFlags);
		}

		if (video.renderer == NULL)
		{
			showErrorMsgBox("Couldn't create SDL renderer:\n%s\n\nIs your GPU (+ driver) too old?",
				SDL_GetError());
			return false;
		}
	}

	SDL_RenderSetLogicalSize(video.renderer, SCREEN_W, SCREEN_H);

#if SDL_VERSION_ATLEAST(2,0,0)
#if SDL_PATCHLEVEL >= 5
	SDL_RenderSetIntegerScale(video.renderer, SDL_TRUE);
#endif

	SDL_SetRenderDrawBlendMode(video.renderer, SDL_BLENDMODE_NONE);
#endif

	if (!recreateTexture())
	{
		showErrorMsgBox("Couldn't create a %dx%d GPU texture:\n%s\n\nIs your GPU (+ driver) too old?",
			SCREEN_W, SCREEN_H, SDL_GetError());
		return false;
	}

	// framebuffer used by SDL (for texture)
	video.frameBuffer = (uint32_t *)malloc(SCREEN_W * SCREEN_H * sizeof (int32_t));
	if (video.frameBuffer == NULL)
	{
		showErrorMsgBox("Not enough memory!");
		return false;
	}

	if (!setupSprites())
		return false;

	updateRenderSizeVars();
	updateMouseScaling();

	if (config.specialFlags2 & HARDWARE_MOUSE)
		SDL_ShowCursor(SDL_TRUE);
	else
		SDL_ShowCursor(SDL_FALSE);

	return true;
}

void handleRedrawing(void)
{
	textBox_t *txt;

	if (!editor.ui.configScreenShown && !editor.ui.helpScreenShown)
	{
		if (editor.ui.aboutScreenShown)
		{
			aboutFrame();
		}
		else if (editor.ui.nibblesShown)
		{
			if (editor.NI_Play)
				moveNibblePlayers();
		}
		else
		{
			if (editor.ui.updatePosSections)
			{
				editor.ui.updatePosSections = false;

				if (!editor.ui.diskOpShown)
				{
					drawSongRepS();
					drawSongLength();
					drawPosEdNums(editor.songPos);
					drawEditPattern(editor.editPattern);
					drawPatternLength(editor.editPattern);
					drawSongBPM(editor.speed);
					drawSongSpeed(editor.tempo);
					drawGlobalVol(editor.globalVol);

					if (!songPlaying || editor.wavIsRendering)
						setScrollBarPos(SB_POS_ED, editor.songPos, false);

					// draw current mode text (not while in extended pattern editor mode)
					if (!editor.ui.extended)
					{
						fillRect(115, 80, 74, 10, PAL_DESKTOP);

						     if (playMode == PLAYMODE_PATT)    textOut(115, 80, PAL_FORGRND, "> Play ptn. <");
						else if (playMode == PLAYMODE_EDIT)    textOut(121, 80, PAL_FORGRND, "> Editing <");
						else if (playMode == PLAYMODE_RECSONG) textOut(114, 80, PAL_FORGRND, "> Rec. sng. <");
						else if (playMode == PLAYMODE_RECPATT) textOut(115, 80, PAL_FORGRND, "> Rec. ptn. <");
					}
				}
			}

			if (!editor.ui.extended)
			{
				if (!editor.ui.diskOpShown)
					drawPlaybackTime();

				     if (editor.ui.sampleEditorExtShown) handleSampleEditorExtRedrawing();
				else if (editor.ui.scopesShown) drawScopes();
			}
		}
	}

	drawReplayerData();

	     if (editor.ui.instEditorShown) handleInstEditorRedrawing();
	else if (editor.ui.sampleEditorShown) handleSamplerRedrawing();

	// blink text edit cursor
	if (editor.editTextFlag && mouse.lastEditBox != -1)
	{
		assert(mouse.lastEditBox >= 0 && mouse.lastEditBox < NUM_TEXTBOXES);

		txt = &textBoxes[mouse.lastEditBox];
		if (editor.textCursorBlinkCounter < 256/2 && !textIsMarked() && !(mouse.leftButtonPressed | mouse.rightButtonPressed))
			setSpritePos(SPRITE_TEXT_CURSOR, getTextCursorX(txt), getTextCursorY(txt) - 1); // show text cursor
		else
			hideSprite(SPRITE_TEXT_CURSOR); // hide text cursor

		editor.textCursorBlinkCounter += TEXT_CURSOR_BLINK_RATE;
	}

	if (editor.busy)
		animateBusyMouse();

	renderLoopPins();
}

static void drawReplayerData(void)
{
	bool drawPosText;

	if (songPlaying)
	{
		if (editor.ui.drawReplayerPianoFlag)
		{
			editor.ui.drawReplayerPianoFlag = false;
			if (editor.ui.instEditorShown)
			{
				if (chSyncEntry != NULL)
					drawPianoReplayer(chSyncEntry);
			}
		}

		drawPosText = true;
		if (editor.ui.configScreenShown || editor.ui.nibblesShown     ||
			editor.ui.helpScreenShown   || editor.ui.aboutScreenShown ||
			editor.ui.diskOpShown)
		{
			drawPosText = false;
		}

		if (drawPosText)
		{
			if (editor.ui.drawBPMFlag)
			{
				editor.ui.drawBPMFlag = false;
				drawSongBPM(editor.speed);
			}
			
			if (editor.ui.drawSpeedFlag)
			{
				editor.ui.drawSpeedFlag = false;
				drawSongSpeed(editor.tempo);
			}

			if (editor.ui.drawGlobVolFlag)
			{
				editor.ui.drawGlobVolFlag = false;
				drawGlobalVol(editor.globalVol);
			}

			if (editor.ui.drawPosEdFlag)
			{
				editor.ui.drawPosEdFlag = false;
				drawPosEdNums(editor.songPos);
				setScrollBarPos(SB_POS_ED, editor.songPos, false);
			}

			if (editor.ui.drawPattNumLenFlag)
			{
				editor.ui.drawPattNumLenFlag = false;
				drawEditPattern(editor.editPattern);
				drawPatternLength(editor.editPattern);
			}
		}
	}
	else if (editor.ui.instEditorShown)
	{
		drawPiano();
	}

	// handle pattern data updates
	if (editor.ui.updatePatternEditor)
	{
		editor.ui.updatePatternEditor = false;
		if (editor.ui.patternEditorShown)
			writePattern(editor.pattPos, editor.editPattern);
	}
}

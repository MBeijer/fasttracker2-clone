/* for finding memory leaks in debug mode with Visual Studio */
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include "ft2_header.h"
#include "ft2_gui.h"
#include "ft2_video.h"
#include "ft2_scopes.h"
#include "ft2_help.h"
#include "ft2_sample_ed.h"
#include "ft2_inst_ed.h"
#include "ft2_pattern_ed.h"
#include "ft2_mouse.h"
#include "ft2_config.h"
#include "ft2_diskop.h"
#include "ft2_gfxdata.h"
#include "ft2_audioselector.h"
#include "ft2_midi.h"

static uint8_t mouseBusyGfxBackwards;
static int16_t mouseShape;
static int32_t mouseModeGfxOffs, mouseBusyGfxFrame;

void animateBusyMouse(void)
{
    if (config.mouseAnimType == MOUSE_BUSY_SHAPE_CLOCK)
    {
        if ((editor.framesPassed % 7) == 6)
        {
            if (mouseBusyGfxBackwards)
            {
                if (--mouseBusyGfxFrame <= 0)
                {
                    mouseBusyGfxFrame = 0;
                    mouseBusyGfxBackwards = false;
                }
            }
            else
            {
                if (++mouseBusyGfxFrame >= (MOUSE_CLOCK_ANI_FRAMES - 1))
                {
                    mouseBusyGfxFrame = MOUSE_CLOCK_ANI_FRAMES - 1;
                    mouseBusyGfxBackwards = true;
                }
            }

            changeSpriteData(SPRITE_MOUSE_POINTER, &mouseCursorBusyClock[(mouseBusyGfxFrame % MOUSE_CLOCK_ANI_FRAMES) * (MOUSE_CURSOR_W * MOUSE_CURSOR_H)]);
        }
    }
    else
    {
        if ((editor.framesPassed % 5) == 4)
        {
            mouseBusyGfxFrame = (mouseBusyGfxFrame + 1) % MOUSE_GLASS_ANI_FRAMES;
            changeSpriteData(SPRITE_MOUSE_POINTER, &mouseCursorBusyGlass[mouseBusyGfxFrame * (MOUSE_CURSOR_W * MOUSE_CURSOR_H)]);
        }
    }
}

void setMouseShape(int16_t shape)
{
    const uint8_t *gfxPtr;

    if (editor.busy)
    {
        if (config.mouseAnimType == MOUSE_BUSY_SHAPE_CLOCK)
            gfxPtr = &mouseCursorBusyClock[(mouseBusyGfxFrame % MOUSE_GLASS_ANI_FRAMES) * (MOUSE_CURSOR_W * MOUSE_CURSOR_H)];
        else
            gfxPtr = &mouseCursorBusyGlass[(mouseBusyGfxFrame % MOUSE_CLOCK_ANI_FRAMES) * (MOUSE_CURSOR_W * MOUSE_CURSOR_H)];
    }
    else
    {
        gfxPtr = &mouseCursors[mouseModeGfxOffs];
        switch (shape)
        {
            case MOUSE_IDLE_SHAPE_NICE:    gfxPtr += ( 0 * (MOUSE_CURSOR_W * MOUSE_CURSOR_H)); break;
            case MOUSE_IDLE_SHAPE_UGLY:    gfxPtr += ( 1 * (MOUSE_CURSOR_W * MOUSE_CURSOR_H)); break;
            case MOUSE_IDLE_SHAPE_AWFUL:   gfxPtr += ( 2 * (MOUSE_CURSOR_W * MOUSE_CURSOR_H)); break;
            case MOUSE_IDLE_SHAPE_USEABLE: gfxPtr += ( 3 * (MOUSE_CURSOR_W * MOUSE_CURSOR_H)); break;
            case MOUSE_IDLE_TEXT_EDIT:     gfxPtr += (12 * (MOUSE_CURSOR_W * MOUSE_CURSOR_H)); break;
            default: return;
        }
    }

    mouseShape = shape;
    changeSpriteData(SPRITE_MOUSE_POINTER, gfxPtr);
}

static void setTextEditMouse(void)
{
    setMouseShape(MOUSE_IDLE_TEXT_EDIT);
    mouse.xBias = -2;
    mouse.yBias = -6;
}

static void clearTextEditMouse(void)
{
    setMouseShape(config.mouseType);
    mouse.xBias = 0;
    mouse.yBias = 0;
}

static void changeMouseIfOverTextEditBoxes(void)
{
    int16_t i, mx, my;
    textBox_t *t;

    mouse.mouseOverTextBox = false;
    if (editor.busy || (mouse.mode != MOUSE_MODE_NORMAL))
        return;

    mx = mouse.x;
    my = mouse.y;

    for (i = 0; i < NUM_TEXTBOXES; ++i)
    {
        if (editor.ui.systemRequestShown && (i > 0))
            continue;

        t = &textBoxes[i];
        if (t->visible)
        {
            if (!t->changeMouseCursor && !(editor.ui.editTextFlag && (i == mouse.lastEditBox)))
                continue;

            if ((my >= t->y) && (my < (t->y + t->h)) && (mx >= t->x) && (mx < (t->x + t->w)))
            {
                mouse.mouseOverTextBox = true;
                setTextEditMouse();
                return;
            }
        }
    }

    /* we're not inside a text edit box, set back mouse cursor */
    if ((i == NUM_TEXTBOXES) && (mouseShape == MOUSE_IDLE_TEXT_EDIT))
        clearTextEditMouse();
}

void setMouseMode(uint8_t mode)
{
    switch (mode)
    {
        case MOUSE_MODE_NORMAL: { mouse.mode = mode; mouseModeGfxOffs = 0 * (MOUSE_CURSOR_W * MOUSE_CURSOR_H); } break;
        case MOUSE_MODE_DELETE: { mouse.mode = mode; mouseModeGfxOffs = 4 * (MOUSE_CURSOR_W * MOUSE_CURSOR_H); } break;
        case MOUSE_MODE_RENAME: { mouse.mode = mode; mouseModeGfxOffs = 8 * (MOUSE_CURSOR_W * MOUSE_CURSOR_H); } break;

        default: return;
    }

    setMouseShape(config.mouseType);
}

void resetMouseBusyAnimation(void)
{
    mouseBusyGfxBackwards = false;
    mouseBusyGfxFrame     = 0;
}

void setMouseBusy(int8_t busy) /* can be called from other threads */
{
    if (busy)
    {
        editor.ui.setMouseIdle = false;
        editor.ui.setMouseBusy = true;
    }
    else
    {
        editor.ui.setMouseBusy = false;
        editor.ui.setMouseIdle = true;
    }
}

void mouseAnimOn(void)
{
    editor.ui.setMouseBusy = false;
    editor.ui.setMouseIdle = false;

    editor.busy = true;
    setMouseShape(config.mouseAnimType);
}

void mouseAnimOff(void)
{
    editor.ui.setMouseBusy = false;
    editor.ui.setMouseIdle = false;

    editor.busy = false;
    setMouseShape(config.mouseType);
}

void updateMouseScaling(void)
{
    int32_t mx, my, w, h;
    float fScaleX, fScaleY;
    SDL_DisplayMode dm;
    SDL_WindowFlags wf;

    SDL_GetWindowSize(video.window, &w, &h);

    fScaleX = w / (float)(SCREEN_W);
    fScaleY = h / (float)(SCREEN_H);

    wf = (SDL_WindowFlags)(SDL_GetWindowFlags(video.window));

    /* correct scaling and/or put mouse cursor in center */
    if (video.fullscreen)
    {
        SDL_GetDesktopDisplayMode(0, &dm);

#if SDL_PATCHLEVEL >= 4
        mx = dm.w / 2;
        my = dm.h / 2;
#endif

        if (config.windowFlags & FILTERING)
        {
            fScaleX = dm.w / (float)(SCREEN_W);
            fScaleY = dm.h / (float)(SCREEN_H);
        }
#if SDL_PATCHLEVEL >= 4
        else
        {
            mx -= (dm.w - (int32_t)(SCREEN_W * fScaleX)) / 2;
            my -= (dm.h - (int32_t)(SCREEN_H * fScaleY)) / 2;

            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
        }

        if (wf & SDL_WINDOW_SHOWN)
            SDL_WarpMouseGlobal(mx, my);
#endif
    }
    else
    {
        if (wf & SDL_WINDOW_SHOWN)
            SDL_WarpMouseInWindow(video.window, (int32_t)(SCREEN_W * fScaleX) / 2, (int32_t)(SCREEN_H * fScaleY) / 2);
    }

    video.fXScale    = fScaleX;
    video.fYScale    = fScaleY;
    video.fXScaleMul = 1.0f / fScaleX;
    video.fYScaleMul = 1.0f / fScaleY;
}

static void mouseWheelDecRow(void)
{
    int16_t pattPos;

    if (!songPlaying)
    {
        pattPos = editor.pattPos - 1;
        if (pattPos < 0)
            pattPos = pattLens[editor.editPattern] - 1;

        setPos(-1, pattPos);
    }
}

static void mouseWheelIncRow(void)
{
    int16_t pattPos;

    if (!songPlaying)
    {
        pattPos = editor.pattPos + 1;
        if (pattPos > (pattLens[editor.editPattern] - 1))
            pattPos = 0;

        setPos(-1, pattPos);
    }
}

void mouseWheelHandler(uint8_t directionUp)
{
    if (editor.ui.systemRequestShown)
        return;

    if (editor.ui.editTextFlag)
        return;

    if (editor.ui.extended)
    {
        if (mouse.y <= 52)
        {
            if (mouse.x <= 111)
                directionUp ? decSongPos() : incSongPos();
            else if (mouse.x >= 386)
                directionUp ? decCurIns() : incCurIns();
        }
        else
        {
            directionUp ? mouseWheelDecRow() : mouseWheelIncRow();
        }

        return;
    }

    if (mouse.y < 173)
    {
        /* top screens */

        if (editor.ui.helpScreenShown)
        {
            /* help screen */

            if (directionUp)
            {
                helpScrollUp();
                helpScrollUp();
            }
            else
            {
                helpScrollDown();
                helpScrollDown();
            }
        }
        else if (editor.ui.diskOpShown)
        {
            /* disk op - 3x speed */
            if (mouse.x <= 355)
            {
                if (directionUp)
                {
                    pbDiskOpListUp();
                    pbDiskOpListUp();
                    pbDiskOpListUp();
                }
                else
                {
                    pbDiskOpListDown();
                    pbDiskOpListDown();
                    pbDiskOpListDown();
                }
            }
        }
        else if (editor.ui.configScreenShown)
        {
            if (editor.currentConfigScreen == CONFIG_SCREEN_IO_DEVICES)
            {
                /* audio device selectors */
                if ((mouse.x >= 110) && (mouse.x <= 355) && (mouse.y <= 173))
                {
                    if (mouse.y < 87)
                        directionUp ? scrollAudOutputDevListUp() : scrollAudOutputDevListDown();
                    else
                        directionUp ? scrollAudInputDevListUp() : scrollAudInputDevListDown();
                }
            }
            else if (editor.currentConfigScreen == CONFIG_SCREEN_MIDI_INPUT)
            {
                /* midi input device selector */
                if ((mouse.x >= 110) && (mouse.x <= 503) && (mouse.y <= 173))
                    directionUp ? scrollMidiInputDevListUp() : scrollMidiInputDevListDown();
            }
        }

        if (!editor.ui.aboutScreenShown  && !editor.ui.helpScreenShown &&
            !editor.ui.configScreenShown && !editor.ui.nibblesShown)
        {
            if ((mouse.x >= 421) && (mouse.y <= 173))
            {
                if (mouse.y <= 93)
                    directionUp ? decCurIns() : incCurIns();
                else if (mouse.y >= 94)
                    directionUp ? decCurSmp() : incCurSmp();
            }
            else if (!editor.ui.diskOpShown && (mouse.x <= 111) && (mouse.y <= 76))
            {
                directionUp ? decSongPos() : incSongPos();
            }
        }
    }
    else
    {
        /* bottom screens */

        if (editor.ui.sampleEditorShown)
            directionUp ? mouseZoomSampleDataIn() : mouseZoomSampleDataOut();
        else if (editor.ui.patternEditorShown)
            directionUp ? mouseWheelDecRow() : mouseWheelIncRow();
    }
}

static int8_t testSamplerDataMouseDown(void)
{
    if (editor.ui.sampleEditorShown)
    {
        if ((mouse.y >= 174) && (mouse.y <= 327) && (editor.ui.sampleDataOrLoopDrag == -1))
        {
            handleSampleDataMouseDown(false);
            return (true);
        }
    }

    return (false);
}

static int8_t testPatternDataMouseDown(void)
{
    uint16_t y1, y2;

    if (editor.ui.patternEditorShown)
    {
        y1 = editor.ui.extended ? 56 : 176;
        y2 = editor.ui.pattChanScrollShown ? 382 : 396;

        if ((mouse.y >= y1) && (mouse.y <= y2))
        {
            if ((mouse.x >= 29) && (mouse.x <= 602))
            {
                handlePatternDataMouseDown(false);
                return (true);
            }
        }
    }

    return (false);
}

void mouseButtonUpHandler(uint8_t mouseButton)
{
#ifndef __APPLE__
    if (!video.fullscreen) /* release mouse button trap */
        SDL_SetWindowGrab(video.window, false);
#endif

    if (mouseButton == SDL_BUTTON_LEFT)
    {
        mouse.leftButtonPressed  = false;
        mouse.leftButtonReleased = true;

        if (editor.ui.leftLoopPinMoving)
        {
            setLeftLoopPinState(false);
            editor.ui.leftLoopPinMoving = false;
        }

        if (editor.ui.rightLoopPinMoving)
        {
            setRightLoopPinState(false);
            editor.ui.rightLoopPinMoving = false;
        }
    }
    else if (mouseButton == SDL_BUTTON_RIGHT)
    {
        mouse.rightButtonPressed  = false;
        mouse.rightButtonReleased = true;
    }

    mouse.firstTimePressingButton = false;
    mouse.buttonCounter = 0;
    editor.textCursorBlinkCounter = 0;

    /* if we used both mouse button at the same time and released *one*, don't release GUI object */
    if ( mouse.leftButtonPressed && !mouse.rightButtonPressed) return;
    if (!mouse.leftButtonPressed &&  mouse.rightButtonPressed) return;

    /* mouse 0,0 = open exit dialog */
    if ((mouse.x == 0) && (mouse.y == 0)) 
    {
        if (quitBox(false) == 1)
            editor.ui.throwExit = true;

        return;
    }

    if (editor.ui.sampleEditorShown)
        testSmpEdMouseUp();

    mouse.lastX = 0;
    mouse.lastY = 0;

    editor.ui.sampleDataOrLoopDrag = -1;

    /* check if we released a GUI object */
    testDiskOpMouseRelease();
    testPushButtonMouseRelease(true);
    testCheckBoxMouseRelease();
    testScrollBarMouseRelease();
    testRadioButtonMouseRelease();

    /* revert "delete/rename" mouse modes (disk op.) */
    if ((mouse.lastUsedObjectID != PB_DISKOP_DELETE) && (mouse.lastUsedObjectID != PB_DISKOP_RENAME))
    {
        if (mouse.mode != MOUSE_MODE_NORMAL)
            setMouseMode(MOUSE_MODE_NORMAL);
    }

    mouse.lastUsedObjectID   = OBJECT_ID_NONE;
    mouse.lastUsedObjectType = OBJECT_NONE;
}

void mouseButtonDownHandler(uint8_t mouseButton)
{
#ifndef __APPLE__
    if (!video.fullscreen) /* trap mouse pointer while holding down left and/or right button */
        SDL_SetWindowGrab(video.window, true);
#endif

    /* if already holding left button and clicking right, don't do mouse down handling */
    if ((mouseButton == SDL_BUTTON_RIGHT) && mouse.leftButtonPressed)
    {
        if (editor.ui.sampleDataOrLoopDrag == -1)
        {
            mouse.rightButtonPressed  = true;
            mouse.rightButtonReleased = false;
        }

        /* kludge - we must do scope solo/unmute all here */
        testScopesMouseDown();

        return;
    }

    /* if already holding right button and clicking left, don't do mouse down handling */
    if ((mouseButton == SDL_BUTTON_LEFT) && mouse.rightButtonPressed)
    {
        if (editor.ui.sampleDataOrLoopDrag == -1)
        {
            mouse.leftButtonPressed  = true;
            mouse.leftButtonReleased = false;
        }

        /* kludge - we must do scope solo/unmute all here */
        testScopesMouseDown();

        return;
    }

         if (mouseButton == SDL_BUTTON_LEFT)  mouse.leftButtonPressed  = true;
    else if (mouseButton == SDL_BUTTON_RIGHT) mouse.rightButtonPressed = true;

    mouse.leftButtonReleased  = false;
    mouse.rightButtonReleased = false;

    /* don't do mouse down testing here if we already are using an object */
    if (mouse.lastUsedObjectType != OBJECT_NONE)
        return;

    /* kludge #2 */
    if ((mouse.lastUsedObjectType != OBJECT_PUSHBUTTON) && (mouse.lastUsedObjectID != OBJECT_ID_NONE))
        return;

    /* kludge #3 */
    if (!mouse.rightButtonPressed)
        mouse.lastUsedObjectID = OBJECT_ID_NONE;

    /* check if we pressed a GUI object */

    /* test objects like this - clickable things *never* overlap, so no need to test all
    ** other objects if we clicked on one already */
    if (testTextBoxMouseDown())             return;
    if (testPushButtonMouseDown())          return;
    if (testCheckBoxMouseDown())            return;
    if (testScrollBarMouseDown())           return;
    if (testRadioButtonMouseDown())         return;

    /* from this point, we don't need to test more widgets if a system request box is shown */
    if (editor.ui.systemRequestShown)
        return;

    if (testInstrSwitcherMouseDown())       return;
    if (testInstrVolEnvMouseDown(false))    return;
    if (testInstrPanEnvMouseDown(false))    return;
    if (testDiskOpMouseDown(false))         return;
    if (testPianoKeysMouseDown(false))      return;
    if (testSamplerDataMouseDown())         return;
    if (testPatternDataMouseDown())         return;
    if (testScopesMouseDown())              return;
    if (testAudioDeviceListsMouseDown())    return;
    if (testMidiInputDeviceListMouseDown()) return;
}

void handleLastGUIObjectDown(void)
{
    if ((mouse.leftButtonPressed || mouse.rightButtonPressed) && (mouse.lastUsedObjectType != OBJECT_NONE))
    {
        switch (mouse.lastUsedObjectType)
        {
            case OBJECT_PUSHBUTTON:  handlePushButtonsWhileMouseDown();  break;
            case OBJECT_RADIOBUTTON: handleRadioButtonsWhileMouseDown(); break;
            case OBJECT_CHECKBOX:    handleCheckBoxesWhileMouseDown();   break;
            case OBJECT_SCROLLBAR:   handleScrollBarsWhileMouseDown();   break;
            case OBJECT_TEXTBOX:     handleTextBoxWhileMouseDown();      break;
            case OBJECT_INSTRSWITCH: testInstrSwitcherMouseDown();       break;
            case OBJECT_PATTERNMARK: handlePatternDataMouseDown(true);   break;
            case OBJECT_DISKOPLIST:  testDiskOpMouseDown(true);          break;
            case OBJECT_SMPDATA:     handleSampleDataMouseDown(true);    break;
            case OBJECT_PIANO:       testPianoKeysMouseDown(true);       break;
            case OBJECT_INSVOLENV:   testInstrVolEnvMouseDown(true);     break;
            case OBJECT_INSPANENV:   testInstrPanEnvMouseDown(true);     break;

            default: break;
        }
    }
}

void readMouseXY(void)
{
    int16_t x, y;
    int32_t mx, my, endX, endY;

    SDL_PumpEvents(); /* gathers all pending input from devices into the event queue */
    SDL_GetMouseState(&mx, &my);

#if SDL_PATCHLEVEL >= 4
    /* in centered fullscreen mode, prevent mouse cursor from getting stuck outside */
    if (video.fullscreen && !(config.windowFlags & FILTERING))
    {
        endX = (int32_t)(SCREEN_W * video.fXScale);
        endY = (int32_t)(SCREEN_H * video.fYScale);

        if (mx >= endX) SDL_WarpMouseGlobal(endX - 1, my);
        if (my >= endY) SDL_WarpMouseGlobal(mx, endY - 1);
    }
#endif

    /* scale mouse positions to match window size */
    if ((video.fXScale > 1.0f) || (video.fYScale > 1.0f))
    {
        mx = (int32_t)(mx * video.fXScaleMul);
        my = (int32_t)(my * video.fYScaleMul);
    }

    /* clamp to edges */
    mx = CLAMP(mx, 0, SCREEN_W - 1);
    my = CLAMP(my, 0, SCREEN_H - 1);

    x = (int16_t)(mx);
    y = (int16_t)(my);

    mouse.x = x;
    mouse.y = y;

    /* for text editing cursor (do this after clamp) */
    x += mouse.xBias;
    y += mouse.yBias;

    if (x < 0) x = 0;
    if (y < 0) y = 0;

    setSpritePos(SPRITE_MOUSE_POINTER, x, y);
    changeMouseIfOverTextEditBoxes();
}

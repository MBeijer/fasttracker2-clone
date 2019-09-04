/* for finding memory leaks in debug mode with Visual Studio */
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#else
#include <unistd.h> /* usleep() */
#endif
#include "ft2_header.h"
#include "ft2_config.h"
#include "ft2_audio.h"
#include "ft2_gui.h"
#include "ft2_midi.h"
#include "ft2_gfxdata.h"
#include "ft2_scopes.h"
#include "ft2_mouse.h"
#include "ft2_video.h"

enum
{
    LOOP_NONE     = 0,
    LOOP_FORWARD  = 1,
    LOOP_PINGPONG = 2
};

#define SCOPE_DATA_HEIGHT 36
#define NUM_LATCH_BUFFERS 4

/* data to be read from main update thread during sample trigger */
typedef struct scopeState_t
{
    int8_t *pek;
    uint8_t typ;
    int32_t len, repS, repL, playOffset;
} scopeState_t;

/* actual scope data */
typedef struct scope_t
{
    const int8_t *sampleData8;
    const int16_t *sampleData16;
    int8_t SVol;
    uint8_t wasCleared, sample16bit, loopType;
    int32_t SRepS, SRepL, SLen, SPos;
    uint32_t SFrq, drawSFrq, SPosDec, posXOR, drawPosXOR;
    volatile uint8_t active, latchFlag, latchOffset, nextLatchOffset;
} scope_t;

static volatile uint8_t scopesUpdating, scopesDisplaying;
static uint64_t timeNext64;
static scope_t scope[MAX_VOICES];
static SDL_Thread *scopeThread;
static volatile scopeState_t scopeNewState[MAX_VOICES][NUM_LATCH_BUFFERS];

#ifdef _WIN32
static NTSTATUS (__stdcall *NtDelayExecution)(BOOL Alertable, PLARGE_INTEGER DelayInterval);
#endif

static const uint16_t scopeLenTab[16][32] =
{
    /*  2 ch */ {285,285},
    /*  4 ch */ {141,141,141,141},
    /*  6 ch */ {93,93,93,93,93,93},
    /*  8 ch */ {69,69,69,69,69,69,69,69},
    /* 10 ch */ {55,55,55,54,54,55,55,55,54,54},
    /* 12 ch */ {45,45,45,45,45,45,45,45,45,45,45,45},
    /* 14 ch */ {39,38,38,38,38,38,38,39,38,38,38,38,38,38},
    /* 16 ch */ {33,33,33,33,33,33,33,33,33,33,33,33,33,33,33,33},
    /* 18 ch */ {29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29},
    /* 20 ch */ {26,26,26,26,26,26,26,26,25,25,26,26,26,26,26,26,26,26,25,25},
    /* 22 ch */ {24,24,23,23,23,23,23,23,23,23,23,24,24,23,23,23,23,23,23,23,23,23},
    /* 24 ch */ {21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21},
    /* 26 ch */ {20,20,19,19,19,19,19,19,19,19,19,19,19,20,20,19,19,19,19,19,19,19,19,19,19,19},
    /* 28 ch */ {18,18,18,18,18,18,18,18,17,17,17,17,17,17,18,18,18,18,18,18,18,18,17,17,17,17,17,17},
    /* 30 ch */ {17,17,17,16,16,16,16,16,16,16,16,16,16,16,16,17,17,17,16,16,16,16,16,16,16,16,16,16,16,16},
    /* 32 ch */ {15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15}
};

int32_t getSamplePosition(uint8_t ch)
{
    int32_t pos;
    scope_t *sc;

    sc = &scope[ch];

    if (sc->latchFlag)
        return (-1); /* scope is latching at the moment, try again later */

    pos = sc->SPos; /* cache it */

    if (sc->active && (pos >= 0) && (pos < sc->SLen))
    {
        if (sc->sample16bit)
            pos *= 2;

        /* do this check again here - check if scope is latching at the moment */
        if (sc->latchFlag)
            return (-1);

        return (pos);
    }

    return (-1); /* not active or overflown */
}

void stopAllScopes(void)
{
    uint8_t i;
    scope_t *sc;

    /* wait for scopes to finish updating */
    while (scopesUpdating);

    for (i = 0; i < MAX_VOICES; ++i)
    {
        sc = &scope[i];

        sc->latchFlag = false;
        sc->active    = false;
    }

    /* wait for scope displaying to be done (safety) */
    while (scopesDisplaying);
}

/* toggle mute */
static void setChannel(uint8_t nr, uint8_t on)
{
    stmTyp *ch;

    ch = &stm[nr];

    ch->stOff = !on;
    if (ch->stOff)
    {
        ch->effTyp   = 0;
        ch->eff      = 0;
        ch->realVol  = 0;
        ch->outVol   = 0;
        ch->oldVol   = 0;
        ch->finalVol = 0;
        ch->outPan   = 128;
        ch->oldPan   = 128;
        ch->finalPan = 128;
        ch->status   = IS_Vol;

        ch->envSustainActive = false; /* non-FT2 bug fix for stuck piano keys */
    }

    scope[nr].wasCleared = false;
}

static void drawScopeNumber(uint16_t scopeXOffs, uint16_t scopeYOffs, uint8_t channel, uint8_t outline)
{
    scopeYOffs++;
    channel++;

    if (outline)
    {
        if (channel < 10) /* one digit? */
        {
            charOutOutlined(scopeXOffs, scopeYOffs, PAL_MOUSEPT, '0' + channel);
        }
        else
        {
            charOutOutlined(scopeXOffs,     scopeYOffs, PAL_MOUSEPT, '0' + (channel / 10));
            charOutOutlined(scopeXOffs + 7, scopeYOffs, PAL_MOUSEPT, '0' + (channel % 10));
        }
    }
    else
    {
        if (channel < 10) /* one digit? */
        {
            charOut(scopeXOffs, scopeYOffs, PAL_MOUSEPT, '0' + channel);
        }
        else
        {
            charOut(scopeXOffs,     scopeYOffs, PAL_MOUSEPT, '0' + (channel / 10));
            charOut(scopeXOffs + 7, scopeYOffs, PAL_MOUSEPT, '0' + (channel % 10));
        }
    }
}

static void redrawScope(uint8_t ch)
{
    const uint8_t scopeMuteBMPWidths[16] =
    {
        162,111, 76, 56, 42, 35, 28, 24,
         21, 21, 17, 17, 12, 12,  9,  9
    };

    const uint8_t scopeMuteBMPHeights[16] =
    {
        27, 27, 26, 25, 25, 25, 24, 24,
        24, 24, 24, 24, 24, 24, 24, 24
    };

    const uint8_t *scopeMuteBMPPointers[16] =
    {
        scopeMuteBMP1, scopeMuteBMP2, scopeMuteBMP3, scopeMuteBMP4,
        scopeMuteBMP5, scopeMuteBMP6, scopeMuteBMP7, scopeMuteBMP8,
        scopeMuteBMP9, scopeMuteBMP9, scopeMuteBMP10,scopeMuteBMP10,
        scopeMuteBMP11,scopeMuteBMP11,scopeMuteBMP12,scopeMuteBMP12
    };

    int8_t chansPerRow;
    uint8_t i, chanLookup;
    int16_t scopeLen, muteGfxLen, muteGfxX;
    const uint16_t *scopeLens;
    uint16_t x, y;

    chansPerRow = song.antChn / 2;
    chanLookup  = chansPerRow - 1;
    scopeLens   = scopeLenTab[chanLookup];

    x = 2;
    y = 94;

    scopeLen = 0; /* prevent compiler warning */
    for (i = 0; i < song.antChn; ++i)
    {
        scopeLen = scopeLens[i];

        if (i == chansPerRow) /* did we reach end of row? */
        {
            /* yes, go one row down */
            x  = 2;
            y += 39;
        }

        if (i == ch)
            break;

        /* adjust position to next channel */
        x += (scopeLen + 3);
    }

    drawFramework(x, y, scopeLen + 2, 38, FRAMEWORK_TYPE2);

    /* draw mute graphics if channel is muted */
    if (editor.channelMute[i])
    {
        muteGfxLen = scopeMuteBMPWidths[chanLookup];
        muteGfxX   = x + ((scopeLen - muteGfxLen) / 2);

        blitFast(muteGfxX, y + 6, scopeMuteBMPPointers[chanLookup], muteGfxLen, scopeMuteBMPHeights[chanLookup]);

        /* draw channel number if needed */
        if (config.ptnChnNumbers)
            drawScopeNumber(x + 1, y + 1, i, true);
    }

    scope[ch].wasCleared = false;
}

void unmuteAllChansOnMusicLoad(void)
{
    uint8_t i;

    for (i = 0; i < MAX_VOICES; ++i)
    {
        editor.channelMute[i] = false;
        scope[i].wasCleared   = false;
    }
}

static void muteChannel(uint8_t ch)
{
    editor.channelMute[ch] = true;
    setChannel(ch, false);
    redrawScope(ch);
}

static void unmuteChannel(uint8_t ch)
{
    editor.channelMute[ch] = false;
    setChannel(ch, true);
    redrawScope(ch);
}

static void toggleChannelMute(uint8_t ch)
{
    editor.channelMute[ch] ^= 1;
    setChannel(ch, !editor.channelMute[ch]);
    redrawScope(ch);
}

int8_t testScopesMouseDown(void)
{
    int8_t chanToToggle, chansPerRow;
    uint8_t test, i;
    uint16_t x;
    const uint16_t *scopeLens;

    if (!editor.ui.scopesShown)
        return (false);

    if ((mouse.y >= 95) && (mouse.y <= 169) &&
        (mouse.x >=  3) && (mouse.x <= 288))
    {
        if ((mouse.y > 130) && (mouse.y < 134))
            return (true);

        chansPerRow = song.antChn / 2;
        scopeLens   = scopeLenTab[chansPerRow - 1];

        /* find out if we clicked inside a scope */
        x = 3;
        for (i = 0; i < chansPerRow; ++i)
        {
            if ((mouse.x >= x) && (mouse.x < (x + scopeLens[i])))
                break;

            x += (scopeLens[i] + 3);
        }

        if (i == chansPerRow)
            return (true); /* scope framework was clicked instead */

        chanToToggle = i;
        if (mouse.y >= 134) /* second row of scopes? */
            chanToToggle += chansPerRow; /* yes, increase lookup offset */

        MY_ASSERT(chanToToggle < song.antChn)

        if (mouse.leftButtonPressed && mouse.rightButtonPressed)
        {
            test = false;
            for (i = 0; i < song.antChn; ++i)
            {
                if ((i != chanToToggle) && editor.channelMute[i])
                    test = true;
            }

            if (test)
            {
                for (i = 0; i < song.antChn; ++i)
                    unmuteChannel(i);
            }
            else
            {
                for (i = 0; i < song.antChn; ++i)
                {
                    if (i == chanToToggle)
                        unmuteChannel(i);
                    else
                        muteChannel(i);
                }
            }
        }
        else if (mouse.leftButtonPressed && !mouse.rightButtonPressed)
        {
            toggleChannelMute(chanToToggle);
        }
        else
        {
            /* toggle channel recording */
            config.multiRecChn[chanToToggle] ^= 1;
            redrawScope(chanToToggle);
        }

        return (true);
    }

    return (false);
}

void setScopeRate(uint8_t ch, uint32_t rate)
{
    const uint32_t scopeRateMul = 0xFFFFFFFF / VBLANK_HZ; /* div->mul trick */
    scope_t *s;

    s = &scope[ch];

    /* since rate (0..534749) << 16 would overflow in a 32-bit variable, we need
    ** to do a 64-bit MUL */

    /* this converts to fast logic on most 32-bit CPUs */
    s->SFrq = (uint32_t)(((uint64_t)(rate) * scopeRateMul) >> 16);

    s->drawSFrq = rate << 4; /* sample data read delta (when drawing the samples) */
}

void setScopeVolume(uint8_t ch, uint16_t vol)
{
    scope[ch].SVol = (int8_t)((vol * SCOPE_DATA_HEIGHT) / 256);
}

void latchScope(uint8_t ch, int8_t *pek, int32_t len, int32_t repS, int32_t repL, uint8_t typ, int32_t playOffset)
{
    volatile scopeState_t *newState;
    scope_t *s;

    s = &scope[ch];

    s->latchOffset = s->nextLatchOffset;
    s->nextLatchOffset = (s->nextLatchOffset + 1) % NUM_LATCH_BUFFERS;

    newState = &scopeNewState[ch][s->latchOffset];

    newState->pek  = pek;
    newState->len  = len;
    newState->repS = repS;
    newState->repL = repL;
    newState->typ  = typ;
    newState->playOffset = playOffset;

    /* XXX: let's hope the CPU does no reordering on this one... */
    s->latchFlag = true;
}

static void triggerScope(uint8_t ch)
{
    int8_t *sampleData;
    uint8_t loopFlag, sampleIs16Bit;
    int32_t sampleLength, sampleLoopBegin, sampleLoopLength, position;
    scope_t *sc;
    volatile scopeState_t *s;

    sc = &scope[ch];
    s  = &scopeNewState[ch][sc->latchOffset];

    sampleData       = s->pek;
    sampleLength     = s->len;
    sampleLoopBegin  = s->repS;
    sampleLoopLength = s->repL;
    loopFlag         = s->typ & 3;
    sampleIs16Bit    = s->typ & 16;
    position         = s->playOffset;

    if ((sampleData == NULL) || (sampleLength < 1))
    {
        sc->active = false; /* shut down scope */
        return;
    }

    if (sampleIs16Bit)
    {
        MY_ASSERT(!(sampleLoopBegin  & 1))
        MY_ASSERT(!(sampleLength     & 1))
        MY_ASSERT(!(sampleLoopLength & 1))

        sampleLoopBegin  /= 2;
        sampleLength     /= 2;
        sampleLoopLength /= 2;

        sc->sampleData16 = (const int16_t *)(s->pek);
    }
    else
    {
        sc->sampleData8 = (const int8_t *)(s->pek);
    }

    if (sampleLoopLength < 1)
        loopFlag = false;

    sc->sample16bit = sampleIs16Bit;

    sc->posXOR   = 0; /* forwards */
    sc->SLen     = loopFlag ? (sampleLoopBegin + sampleLoopLength) : sampleLength;
    sc->SRepS    = sampleLoopBegin;
    sc->SRepL    = sampleLoopLength;
    sc->SPos     = position;
    sc->SPosDec  = 0; /* position fraction */
    sc->loopType = loopFlag;

    /* test if 9xx position overflows */
    if (sc->SPos >= sc->SLen)
    {
        sc->active = false; /* shut down scope */
        return;
    }

    sc->active = true;
}

static void updateScopes(void)
{
    uint8_t i;
    int32_t readPos, loopOverflow;
    scope_t *sc;

    scopesUpdating = true;
    for (i = 0; i < song.antChn; ++i)
    {
        sc = &scope[i];

        if (sc->latchFlag)
        {
            triggerScope(i);
            sc->latchFlag = false; /* we can now read sampling position from scopes in other threads */

            continue; /* don't increase sampling position on trigger frame */
        }

        /* scope position update */

        if (!sc->active)
            continue;

        sc->SPosDec += sc->SFrq;
        if (sc->SPosDec > 65535)
        {
            readPos = sc->SPos + (int32_t)((sc->SPosDec >> 16) ^ sc->posXOR);
            sc->SPosDec &= 0xFFFF;

            /* handle loop wrapping or sample end */

            if (sc->posXOR == 0xFFFFFFFF) /* backwards (definitely bidi loop) */
            {
                if (readPos < sc->SRepS)
                {
                    sc->posXOR = 0; /* forwards */

                    if (sc->SRepL < 2)
                        readPos = sc->SRepS;
                    else
                        readPos = sc->SRepS + ((sc->SRepS - readPos - 1) % sc->SRepL);

                    MY_ASSERT((readPos >= sc->SRepS) && (readPos < sc->SLen))
                }
            }
            else if (sc->loopType == LOOP_NONE) /* no loop */
            {
                if (readPos >= sc->SLen)
                {
                    sc->active = false;
                    continue;
                }
            }
            else if (readPos >= sc->SLen) /* forward or pingpong loop */
            {
                if (sc->SRepL < 2)
                    loopOverflow = 0;
                else
                    loopOverflow = (readPos - sc->SLen) % sc->SRepL;

                if (sc->loopType == LOOP_PINGPONG)
                {
                    sc->posXOR = 0xFFFFFFFF; /* backwards */
                    readPos = (sc->SLen - 1) - loopOverflow;
                }
                else /* forward loop */
                {
                    readPos = sc->SRepS + loopOverflow;
                }

                MY_ASSERT((readPos >= sc->SRepS) && (readPos < sc->SLen))
            }

            MY_ASSERT((readPos >= 0) && (readPos < sc->SLen))

            sc->SPos = readPos; /* update it */
        }
    }
    scopesUpdating = false;
}

static void scopeLine(int16_t x1, int16_t y1, int16_t y2)
{
    int16_t d, sy, dy;
    uint16_t ay;
    int32_t pitch;
    uint32_t pixVal, *dst32;

    dy = y2 - y1;
    ay = ABS(dy);
    sy = SGN(dy);

    pixVal = video.palette[PAL_PATTEXT];
    pitch  = sy * SCREEN_W;

    dst32  = &video.frameBuffer[(y1 * SCREEN_W) + x1];
    *dst32 = pixVal;

    if (ay <= 1)
    {
        if (ay != 0)
            dst32 += pitch;

        *++dst32 = pixVal;
    }
    else
    {
        d = 2 - ay;

        ay *= 2;
        while (y1 != y2)
        {
            if (d >= 0)
            {
                d -= ay;
                dst32++;
            }

            y1 += sy;
            d  += 2;

             dst32 += pitch;
            *dst32  = pixVal;
        }
    }
}

static inline int8_t getScaledScopeSample8(scope_t *sc, int32_t drawPos)
{
    if (!sc->active)
        return (0);

    MY_ASSERT((drawPos >= 0) && (drawPos < sc->SLen))
    return ((int8_t)((sc->sampleData8[drawPos] * sc->SVol) >> 8));
}

static inline int8_t getScaledScopeSample16(scope_t *sc, int32_t drawPos)
{
    if (!sc->active)
        return (0);

    MY_ASSERT((drawPos >= 0) && (drawPos < sc->SLen))
    return ((int8_t)((sc->sampleData16[drawPos] * sc->SVol) >> 16));
}

#define SCOPE_UPDATE_POS \
    scopeDrawFrac += sc->drawSFrq; \
    if (scopeDrawFrac > 65535) \
    { \
        scopeDrawPos  += (int32_t)((scopeDrawFrac >> 16) ^ sc->drawPosXOR); \
        scopeDrawFrac &= 0xFFFF; \
        \
        if (sc->loopType == LOOP_NONE) \
        { \
            if (scopeDrawPos >= sc->SLen) \
                sc->active = false; \
        } \
        else if (sc->drawPosXOR == 0xFFFFFFFF) /* backwards (definitely bidi loop) */ \
        { \
            if (scopeDrawPos < sc->SRepS) \
            { \
                sc->drawPosXOR = 0; /* forwards */ \
                \
                if (sc->SRepL < 2) \
                    scopeDrawPos = sc->SRepS; \
                else  \
                    scopeDrawPos = sc->SRepS + ((sc->SRepS - scopeDrawPos - 1) % sc->SRepL); \
                \
                MY_ASSERT((scopeDrawPos >= sc->SRepS) && (scopeDrawPos < sc->SLen)) \
            } \
        } \
        else if (scopeDrawPos >= sc->SLen) /* forward or pingpong loop */ \
        { \
            if (sc->SRepL < 2) \
                loopOverflow = 0; \
            else \
                loopOverflow = (scopeDrawPos - sc->SLen) % sc->SRepL; \
            \
            if (sc->loopType == LOOP_PINGPONG) \
            { \
                sc->drawPosXOR = 0xFFFFFFFF; /* backwards */ \
                scopeDrawPos = (sc->SLen - 1) - loopOverflow; \
            } \
            else /* forward loop */ \
            { \
                scopeDrawPos = sc->SRepS + loopOverflow; \
            } \
            \
            MY_ASSERT((scopeDrawPos >= sc->SRepS) && (scopeDrawPos < sc->SLen)) \
        } \
    } \

void drawScopes(void)
{
    uint8_t i;
    int16_t y1, y2, sample, scopeLineY;
    const uint16_t *scopeLens;
    uint16_t x16, scopeXOffs, scopeYOffs, scopeDrawLen;
    int32_t scopeDrawPos, loopOverflow;
    uint32_t x, len, scopeDrawFrac, scopePixelColor;
    scope_t cachedScope, *sc;

    scopesDisplaying = true;

    scopeLens  = scopeLenTab[(song.antChn / 2) - 1];
    scopeXOffs = 3;
    scopeYOffs = 95;
    scopeLineY = 112;

    for (i = 0; i < song.antChn; ++i)
    {
        /* check if we are at the bottom row */
        if (i == (song.antChn / 2))
        {
            scopeXOffs = 3;
            scopeYOffs = 134;
            scopeLineY = 151;
        }

        scopeDrawLen = scopeLens[i];

        if (!editor.channelMute[i])
        {
            sc = &scope[i];

            /* cache scope channel to lower thread race condition issues */
            memcpy(&cachedScope, sc, sizeof (scope_t));
            sc = &cachedScope;

            if (sc->active && (sc->SVol > 0) && !audio.locked)
            {
                /* scope is active */

                scope[i].wasCleared = false;

                /* clear scope background */
                clearRect(scopeXOffs, scopeYOffs, scopeDrawLen, SCOPE_DATA_HEIGHT);

                scopeDrawPos   = sc->SPos;
                scopeDrawFrac  = 0;
                sc->drawPosXOR = sc->posXOR;

                /* draw current scope */
                if (config.specialFlags & LINED_SCOPES)
                {
                    /* LINE SCOPE */

                    if (sc->sample16bit)
                    {
                        y1 = scopeLineY - getScaledScopeSample16(sc, scopeDrawPos);
                        SCOPE_UPDATE_POS

                        x16 = scopeXOffs;
                        len = scopeXOffs + (scopeDrawLen - 1);

                        for (; x16 < len; ++x16)
                        {
                            y2 = scopeLineY - getScaledScopeSample16(sc, scopeDrawPos);
                            scopeLine(x16, y1, y2);
                            y1 = y2;

                            SCOPE_UPDATE_POS
                        }
                    }
                    else
                    {
                        y1 = scopeLineY - getScaledScopeSample8(sc, scopeDrawPos);
                        SCOPE_UPDATE_POS

                        x16 = scopeXOffs;
                        len = scopeXOffs + (scopeDrawLen - 1);

                        for (; x16 < len; ++x16)
                        {
                            y2 = scopeLineY - getScaledScopeSample8(sc, scopeDrawPos);
                            scopeLine(x16, y1, y2);
                            y1 = y2;

                            SCOPE_UPDATE_POS
                        }
                    }
                }
                else
                {
                    /* PIXEL SCOPE */

                    scopePixelColor = video.palette[PAL_PATTEXT];

                    x   = scopeXOffs;
                    len = scopeXOffs + scopeDrawLen;

                    if (sc->sample16bit)
                    {
                        for (; x < len; ++x)
                        {
                            sample = getScaledScopeSample16(sc, scopeDrawPos);
                            video.frameBuffer[((scopeLineY - sample) * SCREEN_W) + x] = scopePixelColor;

                            SCOPE_UPDATE_POS
                        }
                    }
                    else
                    {
                        for (; x < len; ++x)
                        {
                            sample = getScaledScopeSample8(sc, scopeDrawPos);
                            video.frameBuffer[((scopeLineY - sample) * SCREEN_W) + x] = scopePixelColor;

                            SCOPE_UPDATE_POS
                        }
                    }
                }
            }
            else
            {
                /* scope is inactive */

                sc = &scope[i];
                if (!sc->wasCleared)
                {
                    /* clear scope background */
                    clearRect(scopeXOffs, scopeYOffs, scopeDrawLen, SCOPE_DATA_HEIGHT);

                    /* draw empty line */
                    hLine(scopeXOffs, scopeLineY, scopeDrawLen, PAL_PATTEXT);

                    sc->wasCleared = true;
                }
            }

            /* draw channel numbering (if enabled) */
            if (config.ptnChnNumbers)
                drawScopeNumber(scopeXOffs, scopeYOffs, i, false);

            /* draw rec. symbol (if enabled) */
            if (config.multiRecChn[i])
                blit(scopeXOffs, scopeYOffs + 31, scopeRecBMP, 13, 4);
        }

        /* align x to next scope */
        scopeXOffs += (scopeDrawLen + 3);
    }

    scopesDisplaying = false;
}

void drawScopeFramework(void)
{
    uint8_t i;

    drawFramework(0, 92, 291, 81, FRAMEWORK_TYPE1);
    for (i = 0; i < song.antChn; ++i)
        redrawScope(i);
}

void handleScopesFromChQueue(chSyncData_t *chSyncData, uint8_t *scopeUpdateStatus)
{
    uint8_t i, status;
    channel_t *ch;
    sampleTyp *s;

    for (i = 0; i < song.antChn; ++i)
    {
        ch = &chSyncData->channels[i];
        status = scopeUpdateStatus[i];

        if (status & IS_Vol)    setScopeVolume(i, ch->finalVol);
        if (status & IS_Period) setScopeRate(i, getFrequenceValueScope(ch->finalPeriod));

        if (status & IS_NyTon) /* trigger sample */
        {
            s = ch->smpPtr;
            if (s != NULL)
            {
                if (ch->instrNr == (MAX_INST + 1))
                {
                    /* sample editor ("Wave, "Range", "Display") */
                    latchScope(i, s->pek, s->len, s->repS, s->repL, s->typ, ch->smpStartPos + editor.samplePlayOffset);
                    editor.samplePlayOffset = 0;
                }
                else
                {
                    /* normal note jamming */
                    latchScope(i, s->pek, s->len, s->repS, s->repL, s->typ, ch->smpStartPos);
                }

                /* set curr. channel for getting sampling position line (sample editor) */
                if ((ch->instrNr == (MAX_INST + 1)) || ((editor.curInstr == ch->instrNr) && (editor.curSmp == ch->sampleNr)))
                    editor.curSmpChannel = i;
            }
        }
    }
}

#ifdef _WIN32 /* usleep() implementation for Windows */
static void usleep(uint32_t usec)
{
    LARGE_INTEGER lpDueTime;

    if (NtDelayExecution == NULL)
    {
        Sleep((uint32_t)((usec / 1000.0) + 0.5));
    }
    else
    {
        /* this prevents a 64-bit MUL (will not overflow with typical values anyway) */
        lpDueTime.HighPart = 0xFFFFFFFF;
        lpDueTime.LowPart  = (DWORD)(-10 * (int32_t)(usec));

        NtDelayExecution(false, &lpDueTime);
    }
}
#endif

static int32_t SDLCALL scopeThreadFunc(void *ptr)
{
    int32_t time32;
    uint32_t diff32;
    uint64_t time64;
    double dTime;

    /* this is needed for the scopes to stutter slightly less (confirmed) */
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);

    /* set next frame time */
    timeNext64 = SDL_GetPerformanceCounter() + video.vblankTimeLen;

    while (editor.programRunning)
    {
        editor.scopeThreadMutex = true;
        updateScopes();
        editor.scopeThreadMutex = false;

        time64 = SDL_GetPerformanceCounter();
        if (time64 < timeNext64)
        {
            MY_ASSERT((timeNext64 - time64) <= 0xFFFFFFFFULL)
            diff32 = (uint32_t)(timeNext64 - time64);

            /* convert and round to microseconds */
            dTime = diff32 * editor.dPerfFreqMulMicro;
            double2int32_round(time32, dTime);

            /* delay until we have reached next tick */
            if (time32 > 0)
                usleep(time32);
        }

        /* update next tick time */
        timeNext64 += video.vblankTimeLen;
    }

    (void)(ptr); /* make compiler happy */

    return (true);
}

uint8_t initScopes(void)
{
#ifdef _WIN32
    NtDelayExecution = (NTSTATUS (__stdcall *)(BOOL, PLARGE_INTEGER))(GetProcAddress(GetModuleHandle("ntdll.dll"), "NtDelayExecution"));
#endif

    scopeThread = SDL_CreateThread(scopeThreadFunc, "FT2 Clone Visuals Thread", NULL);
    if (scopeThread == NULL)
    {
        showErrorMsgBox("Couldn't create scope thread!");
        return (false);
    }

    /* don't let thread wait for this thread, let it clean up on its own when done */
    SDL_DetachThread(scopeThread);

    return (true);
}

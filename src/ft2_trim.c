/* for finding memory leaks in debug mode with Visual Studio */
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <math.h>
#include "ft2_header.h"
#include "ft2_sample_ed.h"
#include "ft2_gui.h"
#include "ft2_scopes.h"
#include "ft2_pattern_ed.h"
#include "ft2_replayer.h"
#include "ft2_audio.h"
#include "ft2_mouse.h"

/*
** "Messy But Works�"
** - 8bitbubsy Solutions, Inc.
*/

static char byteFormatBuffer[64], tmpInstrName[1 + MAX_INST][22 + 1], tmpInstName[MAX_INST][22 + 1];
static uint8_t removePatt, removeInst, removeSamp, removeChans, removeSmpDataAfterLoop, convSmpsTo8Bit;
static uint8_t instrUsed[MAX_INST], instrOrder[MAX_INST], pattUsed[MAX_PATTERNS], pattOrder[MAX_PATTERNS];
static int16_t oldPattLens[MAX_PATTERNS], tmpPattLens[MAX_PATTERNS];
static int64_t xmSize64 = -1, xmAfterTrimSize64 = -1, spaceSaved64 = -1;
static tonTyp *oldPatts[MAX_PATTERNS], *tmpPatt[MAX_PATTERNS];
static instrTyp tmpInstr[1 + MAX_INST], tmpInst[MAX_INST];
static SDL_Thread *trimThread;

void pbTrimCalc(void);

static void remapInstrInSong(uint8_t src, uint8_t dst, int16_t ap)
{
    int16_t i, j, k, pattLen;
    tonTyp *pattPtr, *note;

    for (i = 0; i < ap; ++i)
    {
        pattPtr = patt[i];
        if (pattPtr == NULL)
            continue;

        pattLen = pattLens[i];
        for (j = 0; j < pattLen; ++j)
        {
            for (k = 0; k < MAX_VOICES; ++k)
            {
                note = &pattPtr[(j * MAX_VOICES) + k];
                if (note->instr == src)
                    note->instr = dst;
            }
        }
    }
}

static int8_t tempInstrIsEmpty(uint16_t nr)
{
    assert((nr != 0) && (nr < (1 + MAX_INST)));

    /* test if name is not empty */
    if (tmpInstrName[nr][0] != '\0')
        return (false);

    /* test data integrity against instrument 0, which is "empty" and non-editable */
    if (memcmp(&tmpInstr[nr], &tmpInstr[0], sizeof (instrTyp)) != 0)
        return (false);

    /* empty */
    return (true);
}

static int16_t getUsedTempSamples(uint16_t nr)
{
    int8_t i, j;
    instrTyp *ins;

    if (tempInstrIsEmpty(nr))
        return (0);

    ins = &tmpInstr[nr];

    i = 16 - 1;
    while ((i >= 0) && (ins->samp[i].pek == NULL) && (ins->samp[i].name[0] == '\0'))
        i--;

    if (i >= 0)
    {
        for (j = 0; j < 96; ++j)
        {
            if (ins->ta[j] > i)
                i = ins->ta[j];
        }
    }

    return (i + 1);
}

static int64_t getTempInsAndSmpSize(void)
{
    int16_t i, a, j, k, ai;
    int64_t currSize64;
    instrTyp *ins;

    ai = MAX_INST;
    while ((ai > 0) && (getUsedTempSamples(ai) == 0) && (tmpInstrName[ai][0] == '\0'))
        ai--;

    currSize64 = 0;

    /* count instrument and sample data size in song */
    for (i = 1; i <= ai; ++i)
    {
        if (tempInstrIsEmpty(i))
            j = 0;
        else
            j = i;

        a = getUsedTempSamples(i);
        if (a > 0)
            currSize64 += (INSTR_HEADER_SIZE + (a * sizeof (sampleHeaderTyp)));
        else
            currSize64 += (22 + 11);

        ins = &tmpInstr[j];
        for (k = 0; k < a; ++k)
        {
            if (ins->samp[k].pek != NULL)
                currSize64 += ins->samp[k].len;
        }
    }

    return (currSize64);
}

static void wipeInstrUnused(uint8_t testWipeSize, int16_t *ai, int16_t ap, uint8_t antChn)
{
    uint8_t newInst;
    int16_t numInsts, newNumInsts, instToDel, i, j, k, pattLen;
    tonTyp *pattPtr;

    numInsts = *ai;

    /* calculate what instruments are used (slow method) */
    memset(instrUsed, 0, numInsts);
    for (i = 0; i < ap; ++i)
    {
        if (testWipeSize)
        {
            pattPtr = tmpPatt[i];
            pattLen = tmpPattLens[i];
        }
        else
        {
            pattPtr = patt[i];
            pattLen = pattLens[i];
        }

        if (pattPtr == NULL)
            continue;

        for (j = 0; j < pattLen; ++j)
        {
            for (k = 0; k < antChn; ++k)
            {
                newInst = pattPtr[(j * MAX_VOICES) + k].instr;
                if ((newInst > 0) && (newInst <= MAX_INST))
                    instrUsed[newInst - 1] = true;
            }
        }
    }

    instToDel   = 0;
    newInst     = 0;
    newNumInsts = 0;

    memset(instrOrder, 0, numInsts);
    for (i = 0; i < numInsts; ++i)
    {
        if (instrUsed[i])
        {
            newNumInsts++;
            instrOrder[i] = newInst++;
        }
        else
        {
            instToDel++;
        }
    }

    if (instToDel == 0)
        return;

    if (testWipeSize)
    {
        /* relocate instruments */

        memcpy(tmpInstName, &tmpInstrName[1], MAX_INST * sizeof (song.instrName[0]));
        memcpy(tmpInst,     &tmpInstr[1],     MAX_INST * sizeof (instrTyp));

        memset(&tmpInstrName[1], 0, numInsts * sizeof (song.instrName[0]));
        memset(&tmpInstr[1],     0, numInsts * sizeof (instrTyp));

        for (i = 0; i < numInsts; ++i)
        {
            if (instrUsed[i])
            {
                newInst = instrOrder[i];

                memcpy(&tmpInstr[1 + newInst],    &tmpInst[i], sizeof (instrTyp));
                strcpy(tmpInstrName[1 + newInst], tmpInstName[i]);
            }
        }

        *ai = newNumInsts;
        return;
    }

    /* clear unused instruments */
    for (i = 0; i < numInsts; ++i)
    {
        if (!instrUsed[i])
            clearInstr((uint8_t)(1 + i));
    }

    /* relocate instruments */

    memcpy(tmpInstName, &song.instrName[1], MAX_INST * sizeof (song.instrName[0]));
    memcpy(tmpInst,     &instr[1],          MAX_INST * sizeof (instrTyp));

    memset(&song.instrName[1], 0, numInsts * sizeof (song.instrName[0]));
    memset(&instr[1],          0, numInsts * sizeof (instrTyp));

    for (i = 0; i < numInsts; ++i)
    {
        setStdEnvelope(1 + i, 0, 3); /* instruments need this after clearing */

        if (instrUsed[i])
        {
            newInst = instrOrder[i];
            remapInstrInSong(1 + (uint8_t)(i), 1 + newInst, ap);

            memcpy(&instr[1 + newInst], &tmpInst[i], sizeof (instrTyp));
            strcpy(song.instrName[1 + newInst], tmpInstName[i]);
        }
    }

    *ai = newNumInsts;
}

static void wipePattsUnused(uint8_t testWipeSize, int16_t *ap)
{
    uint8_t newPatt;
    int16_t usedPatts, newUsedPatts, i, *pLens;
    tonTyp **p;

    usedPatts = *ap;
    memset(pattUsed, 0, usedPatts);

    newUsedPatts = 0;
    for (i = 0; i < song.len; ++i)
    {
        newPatt = song.songTab[i];
        if ((newPatt < usedPatts) && !pattUsed[newPatt])
        {
            pattUsed[newPatt] = true;
            newUsedPatts++;
        }
    }

    if ((newUsedPatts == 0) || (newUsedPatts == usedPatts))
        return; /* nothing to do! */

    newPatt = 0;
    memset(pattOrder, 0, usedPatts);
    for (i = 0; i < usedPatts; ++i)
    {
        if (pattUsed[i])
            pattOrder[i] = newPatt++;
    }

    if (testWipeSize)
    {
        p     = tmpPatt;
        pLens = tmpPattLens;
    }
    else
    {
        p     = patt;
        pLens = pattLens;
    }

    memcpy(oldPatts,    p,     usedPatts * sizeof (tonTyp *));
    memcpy(oldPattLens, pLens, usedPatts * sizeof (int16_t));
    memset(p,           0,     usedPatts * sizeof (tonTyp *));
    memset(pLens,       0,     usedPatts * sizeof (int16_t));

    /* relocate patterns */
    for (i = 0; i < usedPatts; ++i)
    {
        p[i] = NULL;

        if (!pattUsed[i])
        {
            if (!testWipeSize)
            {
                if (oldPatts[i] != NULL)
                {
                    free(oldPatts[i]);
                    oldPatts[i] = NULL;
                }
            }
        }
        else
        {
            newPatt = pattOrder[i];

            p[newPatt]     = oldPatts[i];
            pLens[newPatt] = oldPattLens[i];
        }
    }

    if (!testWipeSize)
    {
        for (i = 0; i < MAX_PATTERNS; ++i)
        {
            if (patt[i] == NULL)
                pattLens[i] = 64;
        }

        /* reorder order list (and clear unused entries) */
        for (i = 0; i < 256; ++i)
        {
            if (i < song.len)
                song.songTab[i] = pattOrder[song.songTab[i]];
            else
                song.songTab[i] = 0;
        }
    }

    *ap = newUsedPatts;
}

static void wipeSamplesUnused(uint8_t testWipeSize, int16_t ai)
{
    uint8_t newSamp, smpUsed[16], smpOrder[16];
    int16_t i, j, k, l;
    instrTyp *ins;
    sampleTyp *s, tempSamples[16];

    for (i = 1; i <= ai; ++i)
    {
        if (!testWipeSize)
        {
            if (instrIsEmpty(i))
                l = 0;
            else
                l = i;

            ins = &instr[l];
            l   = getUsedSamples(i);
        }
        else
        {
            if (tempInstrIsEmpty(i))
                l = 0;
            else
                l = i;

            ins = &tmpInstr[l];
            l = getUsedTempSamples(i);
        }

        memset(smpUsed, 0, l);
        if (l > 0)
        {
            for (j = 0; j < l; ++j)
            {
                s = &ins->samp[j];

                /* check if sample is referenced in instrument */
                for (k = 0; k < 96; ++k)
                {
                    if (ins->ta[k] == j)
                    {
                        smpUsed[j] = true;
                        break; /* sample is used */
                    }
                }

                if (k == 96)
                {
                    /* sample is unused */

                    if ((s->pek != NULL) && !testWipeSize)
                        free(s->pek);

                    memset(s, 0, sizeof (sampleTyp));
                    s->pek = NULL;
                }
            }

            /* create re-order list */
            newSamp = 0;
            memset(smpOrder, 0, l);
            for (j = 0; j < l; ++j)
            {
                if (smpUsed[j])
                    smpOrder[j] = newSamp++;
            }

            /* re-order samples */
            memcpy(tempSamples, ins->samp, l * sizeof (sampleTyp));
            memset(ins->samp,   0,         l * sizeof (sampleTyp));
            for (j = 0; j < l; ++j)
            {
                if (smpUsed[j])
                    ins->samp[smpOrder[j]] = tempSamples[j];
            }

            /* re-order note->sample list */
            for (j = 0; j < 96; ++j)
            {
                newSamp = ins->ta[j];
                if (smpUsed[newSamp])
                    ins->ta[j] = smpOrder[newSamp];
                else
                    ins->ta[j] = 0;
            }
        }
    }
}

static void wipeSmpDataAfterLoop(uint8_t testWipeSize, int16_t ai)
{
    int16_t i, j, l;
    instrTyp *ins;
    sampleTyp *s;

    for (i = 1; i <= ai; ++i)
    {
        if (!testWipeSize)
        {
            if (instrIsEmpty(i))
                l = 0;
            else
                l = i;

            ins = &instr[l];
            l   = getUsedSamples(i);
        }
        else
        {
            if (tempInstrIsEmpty(i))
                l = 0;
            else
                l = i;

            ins = &tmpInstr[l];
            l   = getUsedTempSamples(i);
        }

        for (j = 0; j < l; ++j)
        {
            s = &ins->samp[j];
            if ((s->pek != NULL) && (s->typ & 3) && (s->len > 0) && (s->len > (s->repS + s->repL)))
            {
                if (!testWipeSize)
                    restoreSample(s);

                s->len = s->repS + s->repL;
                if (!testWipeSize)
                {
                    if (s->len <= 0)
                    {
                        s->len = 0;
                        free(s->pek);
                        s->pek = NULL;
                    }
                    else
                    {
                        s->pek = (int8_t *)(realloc(s->pek, s->len + 4));
                    }
                }

                if (!testWipeSize)
                    fixSample(s);
            }
        }
    }
}

static void convertSamplesTo8bit(uint8_t testWipeSize, int16_t ai)
{
    int8_t *newSmpPtr;
    int16_t *smpPtr16, i, j, k;
    int32_t l;
    instrTyp *ins;
    sampleTyp *s;

    for (i = 1; i <= ai; ++i)
    {
        if (!testWipeSize)
        {
            if (instrIsEmpty(i))
                k = 0;
            else
                k = i;

            ins = &instr[k];
            k   = getUsedSamples(i);
        }
        else
        {
            if (tempInstrIsEmpty(i))
                k = 0;
            else
                k = i;

            ins = &tmpInstr[k];
            k   = getUsedTempSamples(i);
        }

        for (j = 0; j < k; ++j)
        {
            s = &ins->samp[j];
            if ((s->pek != NULL) && (s->typ & 16) && (s->len > 0))
            {
                if (testWipeSize)
                {
                    s->typ  &= ~16;
                    s->len  /= 2;
                    s->repL /= 2;
                    s->repS /= 2;
                }
                else
                {
                    newSmpPtr = (int8_t *)(malloc((s->len / 2) + 4));
                    if (newSmpPtr != NULL)
                    {
                        restoreSample(s);

                        s->typ &= ~16;

                        s->len  /= 2;
                        s->repL /= 2;
                        s->repS /= 2;

                        smpPtr16 = (int16_t *)(s->pek);
                        for (l = 0; l < s->len; ++l)
                            newSmpPtr[l] = (int8_t)(smpPtr16[l] >> 8);

                        free(s->pek);
                        s->pek = newSmpPtr;

                        fixSample(s);
                    }
                }
            }
        }
    }
}

static uint16_t getPackedPattSize(tonTyp *pattern, uint16_t numRows, uint8_t antChn)
{
    uint8_t bytes[sizeof (tonTyp)], packBits, *writePtr, *firstBytePtr, *pattPtr;
    uint16_t row, chn, totalPackLen;

    totalPackLen = 0;

    pattPtr = (uint8_t *)(pattern);

    writePtr = pattPtr;
    for (row = 0; row < numRows; ++row)
    {
        for (chn = 0; chn < antChn; ++chn)
        {
            memcpy(bytes, pattPtr, sizeof (tonTyp));
            pattPtr += sizeof (tonTyp);

            firstBytePtr = writePtr++;

            packBits = 0;
            if (bytes[0] > 0) { packBits |= 1; writePtr++; } /* note */
            if (bytes[1] > 0) { packBits |= 2; writePtr++; } /* instrument */
            if (bytes[2] > 0) { packBits |= 4; writePtr++; } /* volume column */
            if (bytes[3] > 0) { packBits |= 8; writePtr++; } /* effect */

            if (packBits == 15) /* first four bits set? */
            {
                /* no packing needed, write pattern data as is */
                totalPackLen += 5;
                writePtr += 5;

                continue;
            }

            if (bytes[4] > 0) writePtr++; /* effect parameter */

            totalPackLen += (uint16_t)(writePtr - firstBytePtr); /* bytes writen */
        }

        /* skip unused channels */
        pattPtr += (sizeof (tonTyp) * (MAX_VOICES - antChn));
    }

    return (totalPackLen);
}

static int8_t tmpPatternEmpty(uint16_t nr, uint8_t antChn)
{
    uint8_t j, *scanPtr;
    uint16_t i, pattLen;
    uint32_t zeroTest;

    if (tmpPatt[nr] == NULL)
        return (true);

    scanPtr = (uint8_t *)(tmpPatt[nr]);
    pattLen = tmpPattLens[nr];

    for (i = 0; i < pattLen; ++i)
    {
        for (j = 0; j < antChn; ++j)
        {
            zeroTest  = scanPtr[0];
            zeroTest += scanPtr[1];
            zeroTest += scanPtr[2];
            zeroTest += scanPtr[3];
            zeroTest += scanPtr[4];

            if (zeroTest != 0)
                return (false);

            scanPtr += sizeof (tonTyp);
        }

        scanPtr += (sizeof (tonTyp) * (MAX_VOICES - antChn));
    }

    return (true);
}

static int64_t calculateXMSize(void)
{
    int16_t i, j, k, ap, ai, a;
    int64_t currSize64;
    instrTyp *ins;

    /* count header size in song */
    currSize64 = sizeof (songHeaderTyp);

    /* count number of patterns that would be saved */
    ap = MAX_PATTERNS;
    do
    {
        if (patternEmpty(ap - 1))
            ap--;
        else
            break;
    }
    while (ap > 0);

    /* count number of instruments */
    ai = MAX_INST;
    while ((ai > 0) && (getUsedSamples(ai) == 0) && (song.instrName[ai][0] == '\0'))
        ai--;

    /* count packed pattern data size in song */
    for (i = 0; i < ap; ++i)
    {
        currSize64 += sizeof (patternHeaderTyp);
        if (!patternEmpty(i))
            currSize64 += getPackedPattSize(patt[i], pattLens[i], song.antChn);
    }

    /* count instrument and sample data size in song */
    for (i = 1; i <= ai; ++i)
    {
        if (instrIsEmpty(i))
            j = 0;
        else
            j = i;

        a = getUsedSamples(i);
        if (a > 0)
            currSize64 += (INSTR_HEADER_SIZE + (a * sizeof (sampleHeaderTyp)));
        else
            currSize64 += (22 + 11);

        ins = &instr[j];
        for (k = 0; k < a; ++k)
        {
            if (ins->samp[k].pek != NULL)
                currSize64 += ins->samp[k].len;
        }
    }

    return (currSize64);
}

static int64_t calculateTrimSize(void)
{
    uint8_t antChn;
    int16_t ap, i, j, k, ai, highestChan, pattLen;
    int32_t pattDataLen, newPattDataLen;
    int64_t bytes64, oldInstrSize64, newInstrSize64;
    tonTyp *note, *pattPtr;

    antChn         = song.antChn;
    pattDataLen    = 0;
    newPattDataLen = 0;
    bytes64        = 0;
    oldInstrSize64 = 0;

    /* copy over temp data */
    memcpy(tmpPatt,      patt,           sizeof (tmpPatt));
    memcpy(tmpPattLens,  pattLens,       sizeof (tmpPattLens));
    memcpy(tmpInstr,     instr,          sizeof (tmpInstr));
    memcpy(tmpInstrName, song.instrName, sizeof (tmpInstrName));

    /* get current size of all instruments and their samples */
    if (removeInst || removeSamp || removeSmpDataAfterLoop || convSmpsTo8Bit)
        oldInstrSize64 = getTempInsAndSmpSize();

    /* count number of patterns that would be saved */
    ap = MAX_PATTERNS;
    do
    {
        if (tmpPatternEmpty(ap - 1, antChn))
            ap--;
        else
            break;
    }
    while (ap > 0);

    /* count number of instruments that would be saved */
    ai = MAX_INST;
    while ((ai > 0) && (getUsedTempSamples(ai) == 0) && (tmpInstrName[ai][0] == '\0'))
        ai--;

    /* calculate "remove unused samples" size */
    if (removeSamp) wipeSamplesUnused(true, ai);

    /* calculate "remove sample data after loop" size */
    if (removeSmpDataAfterLoop) wipeSmpDataAfterLoop(true, ai);

    /* calculate "convert samples to 8-bit" size */
    if (convSmpsTo8Bit) convertSamplesTo8bit(true, ai);

    /* get old pattern data length */
    if (removeChans || removePatt)
    {
        for (i = 0; i < ap; ++i)
        {
            pattDataLen += sizeof (patternHeaderTyp);
            if (!tmpPatternEmpty(i, antChn))
                pattDataLen += getPackedPattSize(tmpPatt[i], tmpPattLens[i], antChn);
        }
    }

    /* calculate "remove unused channels" size */
    if (removeChans)
    {
        /* get real number of channels */
        highestChan = -1;
        for (i = 0; i < ap; ++i)
        {
            pattPtr = tmpPatt[i];
            if (pattPtr == NULL)
                continue;

            pattLen = tmpPattLens[i];
            for (j = 0; j < pattLen; ++j)
            {
                for (k = 0; k < antChn; ++k)
                {
                    note = &pattPtr[(j * MAX_VOICES) + k];
                    if (note->eff || note->effTyp || note->instr || note->ton || note->vol)
                    {
                        if (k > highestChan)
                            highestChan = k;
                    }
                }
            }
        }

        /* set new number of channels (and make it an even number) */
        if (highestChan >= 0)
        {
            highestChan++;
            if (highestChan & 1)
                highestChan++;

            antChn = (uint8_t)(CLAMP(highestChan, 2, antChn));
        }
    }

    /* calculate "remove unused patterns" size */
    if (removePatt) wipePattsUnused(true, &ap);

    /* calculate new pattern data size */
    if (removeChans || removePatt)
    {
        for (i = 0; i < ap; ++i)
        {
            newPattDataLen += sizeof (patternHeaderTyp);
            if (!tmpPatternEmpty(i, antChn))
                newPattDataLen += getPackedPattSize(tmpPatt[i], tmpPattLens[i], antChn);
        }

        assert(pattDataLen >= newPattDataLen);

        if (pattDataLen > newPattDataLen)
            bytes64 += (pattDataLen - newPattDataLen);
    }

    /* calculate "remove unused instruments" size */
    if (removeInst) wipeInstrUnused(true, &ai, ap, antChn);

    /* calculat new instruments and samples size */
    if (removeInst || removeSamp || removeSmpDataAfterLoop || convSmpsTo8Bit)
    {
        newInstrSize64 = getTempInsAndSmpSize();

        assert(oldInstrSize64 >= newInstrSize64);

        if (oldInstrSize64 > newInstrSize64)
            bytes64 += (oldInstrSize64 - newInstrSize64);
    }

    return (bytes64);
}

static int32_t SDLCALL trimThreadFunc(void *ptr)
{
    int16_t ap, ai, i, j, k, pattLen, highestChan;
    tonTyp *pattPtr, *note;

    /* audio callback is not running now, so we're safe */

    /* count number of patterns */
    ap = MAX_PATTERNS;
    do
    {
        if (patternEmpty(ap - 1))
            ap--;
        else
            break;
    }
    while (ap > 0);

    /* count number of instruments */
    ai = MAX_INST;
    while ((ai > 0) && (getUsedSamples(ai) == 0) && (song.instrName[ai][0] == '\0'))
        ai--;

    /* remove unused samples */
    if (removeSamp)
        wipeSamplesUnused(false, ai);

    /* remove sample data after loop */
    if (removeSmpDataAfterLoop)
        wipeSmpDataAfterLoop(false, ai);

    /* convert samples to 8-bit */
    if (convSmpsTo8Bit)
        convertSamplesTo8bit(false, ai);

    /* removed unused channels */
    if (removeChans)
    {
        /* count used channels */
        highestChan = -1;
        for (i = 0; i < ap; ++i)
        {
            pattPtr = patt[i];
            if (pattPtr == NULL)
                continue;

            pattLen = pattLens[i];
            for (j = 0; j < pattLen; ++j)
            {
                for (k = 0; k < song.antChn; ++k)
                {
                    note = &pattPtr[(j * MAX_VOICES) + k];
                    if (note->eff || note->effTyp || note->instr || note->ton || note->vol)
                    {
                        if (k > highestChan)
                            highestChan = k;
                    }
                }
            }
        }

        /* set new 'channels used' number */
        if (highestChan >= 0)
        {
            highestChan++;
            if (highestChan & 1)
                highestChan++;

            song.antChn = (uint8_t)(CLAMP(highestChan, 2, song.antChn));
        }

        /* clear potentially unused channel data */
        if (song.antChn < MAX_VOICES)
        {
            for (i = 0; i < MAX_PATTERNS; ++i)
            {
                pattPtr = patt[i];
                if (pattPtr == NULL)
                    continue;

                pattLen = pattLens[i];
                for (j = 0; j < pattLen; ++j)
                    memset(&pattPtr[(j * MAX_VOICES) + song.antChn], 0,
                        sizeof (tonTyp) * (MAX_VOICES - song.antChn));
            }
        }
    }

    /* clear unused patterns */
    if (removePatt)
       wipePattsUnused(false, &ap);

    /* remove unused instruments */
    if (removeInst)
        wipeInstrUnused(false, &ai, ap, song.antChn);

    editor.trimThreadWasDone = true;

    (void)(ptr); /* prevent compiler warning */

    return (true);
}

void trimThreadDone(void)
{
    if (removePatt)
        setPos(song.songPos, song.pattPos);

    if (removeInst)
    {
        editor.currVolEnvPoint = 0;
        editor.currPanEnvPoint = 0;
    }

    hideTopScreen();
    showTopScreen(true);
    showBottomScreen();

    if (removeChans)
    {
        if (editor.ui.patternEditorShown)
        {
            if (editor.ui.channelOffset > (song.antChn - editor.ui.numChannelsShown))
                setScrollBarPos(SB_CHAN_SCROLL, song.antChn - editor.ui.numChannelsShown, true);
        }

        if (editor.cursor.ch >= (editor.ui.channelOffset + editor.ui.numChannelsShown))
            editor.cursor.ch  = (editor.ui.channelOffset + editor.ui.numChannelsShown) - 1;
    }

    checkMarkLimits();

    if (removeSamp || convSmpsTo8Bit)
        updateSampleEditorSample();

    pbTrimCalc();

    setSongModifiedFlag();
    unlockMixerCallback();

    setMouseBusy(false);
}

static char *formatBytes(uint64_t bytes)
{
    double dBytes;

    if (bytes == 0)
    {
        strcpy(byteFormatBuffer, "0 bytes");
        return (byteFormatBuffer);
    }

    bytes %= (1000U * 1024 * 1024 * 999ULL); /* wrap around gigabytes in case of overflow (uh-oh) */
    if (bytes >= (1024 * 1024 * 1024 * 9ULL))
    {
        /* gigabytes */
        dBytes = bytes / (1024.0 * 1024.0 * 1024.0);
        if (dBytes < 100)
            sprintf(byteFormatBuffer, "%.3fGB", dBytes);
        else
            sprintf(byteFormatBuffer, "%dGB", (int32_t)(round(dBytes)));
    }
    else if (bytes >= (1024 * 1024 * 9))
    {
        /* megabytes */
        dBytes = bytes / (1024.0 * 1024.0);
        if (dBytes < 100)
            sprintf(byteFormatBuffer, "%.3fMB", dBytes);
        else
            sprintf(byteFormatBuffer, "%dMB", (int32_t)(round(dBytes)));
    }
    else if (bytes >= (1024 * 9))
    {
        /* kilobytes */
        dBytes = bytes / 1024.0;
        if (dBytes < 100)
            sprintf(byteFormatBuffer, "%.3fkB", dBytes);
        else
            sprintf(byteFormatBuffer, "%dkB", (int32_t)(round(dBytes)));
    }
    else
    {
        /* bytes */
        sprintf(byteFormatBuffer, "%dB", (int32_t)(bytes));
    }

    return (byteFormatBuffer);
}

void drawTrimScreen(void)
{
    char sizeBuf[16];

    drawFramework(0,   92, 136, 81, FRAMEWORK_TYPE1);
    drawFramework(136, 92, 155, 81, FRAMEWORK_TYPE1);

    textOutShadow(4,    95, PAL_FORGRND, PAL_DSKTOP2, "What to remove:");
    textOutShadow(19,  109, PAL_FORGRND, PAL_DSKTOP2, "Unused patterns");
    textOutShadow(19,  122, PAL_FORGRND, PAL_DSKTOP2, "Unused instruments");
    textOutShadow(19,  135, PAL_FORGRND, PAL_DSKTOP2, "Unused samples");
    textOutShadow(19,  148, PAL_FORGRND, PAL_DSKTOP2, "Unused channels");
    textOutShadow(19,  161, PAL_FORGRND, PAL_DSKTOP2, "Smp. dat. after loop");

    textOutShadow(155,  96, PAL_FORGRND, PAL_DSKTOP2, "Conv. samples to 8-bit");
    textOutShadow(140, 111, PAL_FORGRND, PAL_DSKTOP2, ".xm size before");
    textOutShadow(140, 124, PAL_FORGRND, PAL_DSKTOP2, ".xm size after");
    textOutShadow(140, 137, PAL_FORGRND, PAL_DSKTOP2, "Bytes to save");

    if (xmSize64 > -1)
    {
        sprintf(sizeBuf, "%s", formatBytes(xmSize64));
        textOut(234, 111, PAL_FORGRND, sizeBuf);
    }
    else
    {
        textOut(234, 111, PAL_FORGRND, "Unknown");
    }

    if (xmAfterTrimSize64 > -1)
    {
        sprintf(sizeBuf, "%s", formatBytes(xmAfterTrimSize64));
        textOut(234, 124, PAL_FORGRND, sizeBuf);
    }
    else
    {
        textOut(234, 124, PAL_FORGRND, "Unknown");
    }

    if (spaceSaved64 > -1)
    {
        sprintf(sizeBuf, "%s", formatBytes(spaceSaved64));
        textOut(234, 137, PAL_FORGRND, sizeBuf);
    }
    else
    {
        textOut(234, 137, PAL_FORGRND, "Unknown");
    }

    showCheckBox(CB_TRIM_PATT);
    showCheckBox(CB_TRIM_INST);
    showCheckBox(CB_TRIM_SAMP);
    showCheckBox(CB_TRIM_CHAN);
    showCheckBox(CB_TRIM_SMPD);
    showCheckBox(CB_TRIM_CONV);
    showPushButton(PB_TRIM_CALC);
    showPushButton(PB_TRIM_TRIM);
}

void hideTrimScreen(void)
{
    hideCheckBox(CB_TRIM_PATT);
    hideCheckBox(CB_TRIM_INST);
    hideCheckBox(CB_TRIM_SAMP);
    hideCheckBox(CB_TRIM_CHAN);
    hideCheckBox(CB_TRIM_SMPD);
    hideCheckBox(CB_TRIM_CONV);
    hidePushButton(PB_TRIM_CALC);
    hidePushButton(PB_TRIM_TRIM);

    editor.ui.trimScreenShown = false;
    editor.ui.scopesShown = true;
    drawScopeFramework();
}

void showTrimScreen(void)
{
    if (editor.ui.extended)
        exitPatternEditorExtended();

    hideTopScreen();
    showTopScreen(false);

    editor.ui.trimScreenShown = true;
    editor.ui.scopesShown = false;

    drawTrimScreen();
}

void toggleTrimScreen(void)
{
    if (editor.ui.trimScreenShown)
        hideTrimScreen();
    else
        showTrimScreen();
}

void setInitialTrimFlags(void)
{
    removePatt  = true;
    removeInst  = true;
    removeSamp  = true;
    removeChans = true;
    removeSmpDataAfterLoop = true;
    convSmpsTo8Bit = false;

    checkBoxes[CB_TRIM_PATT].checked = true;
    checkBoxes[CB_TRIM_INST].checked = true;
    checkBoxes[CB_TRIM_SAMP].checked = true;
    checkBoxes[CB_TRIM_CHAN].checked = true;
    checkBoxes[CB_TRIM_SMPD].checked = true;
    checkBoxes[CB_TRIM_CONV].checked = false;
}

void cbTrimUnusedPatt(void)
{
    removePatt ^= 1;
}

void cbTrimUnusedInst(void)
{
    removeInst ^= 1;
}

void cbTrimUnusedSamp(void)
{
    removeSamp ^= 1;
}

void cbTrimUnusedChans(void)
{
    removeChans ^= 1;
}

void cbTrimUnusedSmpData(void)
{
    removeSmpDataAfterLoop ^= 1;
}

void cbTrimSmpsTo8Bit(void)
{
    convSmpsTo8Bit ^= 1;
}

void pbTrimCalc(void)
{
    xmSize64 = calculateXMSize();
    spaceSaved64 = calculateTrimSize();

    xmAfterTrimSize64 = xmSize64 - spaceSaved64;
    if (xmAfterTrimSize64 < 0)
        xmAfterTrimSize64 = 0;

    if (editor.ui.trimScreenShown)
        drawTrimScreen();
}

void pbTrimDoTrim(void)
{
    if (!removePatt && !removeInst && !removeSamp && !removeChans && !removeSmpDataAfterLoop && !convSmpsTo8Bit)
        return; /* nothing to trim... */

    if (okBox(2, "System request", "Are you sure you want to trim the song? Making a backup of the song first is recommended.") != 1)
        return;

    mouseAnimOn();
    lockMixerCallback();

    trimThread = SDL_CreateThread(trimThreadFunc, "FT2 Trim Thread", NULL);
    if (trimThread == NULL)
    {
        unlockMixerCallback();
        mouseAnimOff();
        return;
    }

    /* don't let thread wait for this thread, let it clean up on its own when done */
    SDL_DetachThread(trimThread);
}

void resetTrimSizes(void)
{
    xmSize64 = -1;
    xmAfterTrimSize64 = -1;
    spaceSaved64 = -1;

    if (editor.ui.trimScreenShown)
        drawTrimScreen();
}

#ifndef __FT2_MIX_H
#define __FT2_MIX_H

#include <stdint.h>
#include "ft2_audio.h"

typedef void (*mixRoutine)(void *, int32_t);

extern const mixRoutine mixRoutineTable[24]; /* ft2_mix.c */

#endif

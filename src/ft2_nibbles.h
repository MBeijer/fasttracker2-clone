#ifndef __FT2_NIBBLES_H
#define __FT2_NIBBLES_H

#include <stdint.h>
#include <SDL2/SDL.h>

void nibblesKeyAdministrator(SDL_Scancode scancode);
void moveNibblePlayers(void);
void showNibblesScreen(void);
void hideNibblesScreen(void);
void exitNibblesScreen(void);

/* pushbuttons */
void pbNibbles(void);
void nibblesPlay(void);
void nibblesHelp(void);
void nibblesHighScore(void);
void nibblesExit(void);

/* radiobuttons */
void nibblesSet1Player(void);
void nibblesSet2Players(void);
void nibblesSetNovice(void);
void nibblesSetAverage(void);
void nibblesSetPro(void);
void nibblesSetTriton(void);

/* checkboxes */
void nibblesToggleSurround(void);
void nibblesToggleGrid(void);
void nibblesToggleWrap(void);

uint8_t testNibblesCheatCodes(SDL_Keycode keycode);

#endif

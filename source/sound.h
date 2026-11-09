/* sound.h - small synthesised navigation sounds (no system files used) */
#ifndef SOUND_H
#define SOUND_H

enum { SND_MOVE, SND_OK, SND_BACK, SND_TAB, SND_STAR };

void sound_enable(int on);    /* starts the audio thread the first time it is turned on */
void sound_play(int which);
void sound_shutdown(void);    /* before leaving the app */

#endif

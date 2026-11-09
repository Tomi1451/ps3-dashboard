/* disc.h - notices a disc going into the drive (asks Cobra for the real disc type) */
#ifndef DISC_H
#define DISC_H

enum { DISC_NONE = 0, DISC_PS1, DISC_PS2, DISC_PS3, DISC_OTHER };

typedef struct {
    int  type;            /* DISC_* */
    int  virt;            /* 1 = a game image is mounted over it, so /dev_bdvd isn't the real disc */
    char title[96];       /* PS3: PARAM.SFO TITLE, otherwise "" */
    char id[16];          /* PS3 TITLE_ID, or the PS1/PS2 boot file as an ID (SLES-12345) */
} DiscInfo;

/* call once per frame; returns 1 on the frame a new disc is noticed (fills *d) */
int  disc_poll(DiscInfo *d);
const char *disc_name(int type);
int  disc_image_mounted(void);
void disc_read_id(DiscInfo *d);   /* re-read the ID from the real disc (after unmounting) */     /* 1 yes, 0 no, -1 unknown */

#endif

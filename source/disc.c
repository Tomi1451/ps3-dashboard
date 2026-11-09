/* disc.c - disc-inserted detection through the Cobra syscall that webMAN itself uses
   (syscall 8, opcode 0x7020 = get disc type: real disc, effective disc, mounted image) */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <ppu-lv2.h>
#include <lv2/sysfs.h>
#include <sys/file.h>

#include "disc.h"
#include "library.h"
#include "log.h"

#define DEVICE_TYPE_PS3_DVD 0xFF70
#define DEVICE_TYPE_PS3_BD  0xFF71
#define DEVICE_TYPE_PS2_CD  0xFF60
#define DEVICE_TYPE_PS2_DVD 0xFF61
#define DEVICE_TYPE_PSX_CD  0xFF50

static int frame = 0, last_real = -1, logged = 0;

static int get_disc_type(u32 *real, u32 *eff, u32 *fake)
{
    lv2syscall4(8, 0x7020, (u64)(u32) real, (u64)(u32) eff, (u64)(u32) fake);
    return_to_user_prog(int);
}

static int file_ok(const char *p)
{
    sysFSStat st;
    return sysLv2FsStat(p, &st) == 0;
}

/* SYSTEM.CNF "BOOT = cdrom:\SLES_123.45;1" (BOOT2 on PS2) -> "SLES-12345" */
static void read_boot_id(char *id, size_t n)
{
    char buf[512], *p;
    s32 fd; u64 got = 0;
    id[0] = 0;
    if (sysLv2FsOpen("/dev_bdvd/SYSTEM.CNF", SYS_O_RDONLY, &fd, 0, NULL, 0) != 0) return;
    sysLv2FsRead(fd, buf, sizeof(buf) - 1, &got);
    sysLv2FsClose(fd);
    buf[got] = 0;
    p = strstr(buf, "cdrom");
    if (!p) return;
    while (*p && *p != '\\' && *p != ':') p++;
    while (*p == '\\' || *p == ':') p++;
    size_t j = 0;
    for (; *p && *p != ';' && *p != '\r' && *p != '\n' && j + 1 < n; p++) {
        if (*p == '_') id[j++] = '-';
        else if (*p != '.') id[j++] = toupper((unsigned char) *p);
    }
    id[j] = 0;
}

const char *disc_name(int type)
{
    switch (type) {
        case DISC_PS1: return "PlayStation disc";
        case DISC_PS2: return "PlayStation 2 disc";
        case DISC_PS3: return "PlayStation 3 disc";
        default:       return "Disc";
    }
}

int disc_poll(DiscInfo *d)
{
    u32 real = 0, eff = 0, fake = 0;
    int r;
    if (frame++ % 30) return 0;          /* twice a second is plenty */

    r = get_disc_type(&real, &eff, &fake);
    if (r != 0) {
        if (!logged) { log_step("disc: Cobra disc-type call failed (%d) - disc detection off", r); logged = 1; }
        return 0;
    }
    if ((int) real == last_real) return 0;
    log_step("disc: real 0x%X effective 0x%X image 0x%X", real, eff, fake);
    int first = (last_real < 0);
    last_real = (int) real;
    if (real == 0) return 0;              /* disc taken out */

    memset(d, 0, sizeof(*d));
    d->virt = (fake != 0);
    if (real == DEVICE_TYPE_PS3_BD || real == DEVICE_TYPE_PS3_DVD) d->type = DISC_PS3;
    else if (real == DEVICE_TYPE_PS2_CD || real == DEVICE_TYPE_PS2_DVD) d->type = DISC_PS2;
    else if (real == DEVICE_TYPE_PSX_CD) d->type = DISC_PS1;
    else d->type = DISC_OTHER;           /* film, music, data disc: leave those to the XMB */

    if (!d->virt) {                       /* /dev_bdvd is the real disc, so we can read its details */
        if (d->type == DISC_PS3 && file_ok("/dev_bdvd/PS3_GAME/PARAM.SFO"))
            read_sfo("/dev_bdvd/PS3_GAME/PARAM.SFO", d->title, sizeof(d->title), d->id, sizeof(d->id));
        else if (d->type == DISC_PS1 || d->type == DISC_PS2)
            read_boot_id(d->id, sizeof(d->id));
    }
    log_step("disc: %s  title '%s'  id '%s'%s%s", disc_name(d->type), d->title, d->id,
             d->virt ? "  (game image mounted over it)" : "", first ? "  (already in at start)" : "");
    return d->type != DISC_OTHER;
}

/* 1 if Cobra has a game image mounted over the drive (reads would come from the image) */
int disc_image_mounted(void)
{
    u32 real = 0, eff = 0, fake = 0;
    if (get_disc_type(&real, &eff, &fake) != 0) return -1;
    return fake != 0;
}

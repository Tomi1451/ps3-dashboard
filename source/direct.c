/* direct.c - PS1 games: webMAN mounts the image while we are still running, then we hand
   over to the PS3's own PS1 emulator (/dev_flash/ps1emu/ps1_netemu.self) the way IRISMAN
   does, with the same arguments. Saves go to one of your existing PS1 memory cards in
   /dev_hdd0/savedata/vmc (the most recently used one unless chosen in Settings).
   PS3 games: mount, then start the game's EBOOT.BIN (its update's EBOOT if installed).
   PS2 games can't be started this way (they need the PS2 Classics emulator set up) - the
   caller falls back to the XMB route for those, and whenever anything here fails. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ppu-lv2.h>
#include <lv2/sysfs.h>
#include <sys/file.h>
#include <sys/process.h>
#include <lv2/process.h>

#include "direct.h"
#include "launch.h"
#include "disc.h"
#include "settings.h"
#include "sound.h"
#include "log.h"

#define VMC_DIR "/dev_hdd0/savedata/vmc"

static int exists(const char *p) { sysFSStat st; return sysLv2FsStat(p, &st) == 0; }

int ps1_cards(char names[][64], int max)
{
    s32 fd; sysFSDirent e; u64 rd; int n = 0;
    if (sysLv2FsOpenDir(VMC_DIR, &fd) != 0) return 0;
    while (n < max && sysLv2FsReadDir(fd, &e, &rd) == 0 && rd > 0) {
        size_t l = strlen(e.d_name);
        if (l > 4 && l < 64 && !strcasecmp(e.d_name + l - 4, ".VM1")) strcpy(names[n++], e.d_name);
    }
    sysLv2FsCloseDir(fd);
    return n;
}

const char *ps1_card_in_use(void)
{
    static char best[64];
    char names[32][64], p[160];
    int n = ps1_cards(names, 32), i;
    time_t newest = 0;
    best[0] = 0;
    for (i = 0; i < n; i++)
        if (settings.ps1_card[0] && !strcmp(names[i], settings.ps1_card)) { strcpy(best, names[i]); return best; }
    for (i = 0; i < n; i++) {                       /* automatic: the card used most recently */
        sysFSStat st;
        snprintf(p, sizeof(p), VMC_DIR "/%s", names[i]);
        if (sysLv2FsStat(p, &st) == 0 && (best[0] == 0 || st.st_mtime > newest)) { newest = st.st_mtime; strcpy(best, names[i]); }
    }
    return best;
}

static int start_ps1(const char *path)
{
    static char a_mc1[64], a_mc2[64];
    const char *args[9];
    const char *card = ps1_card_in_use();
    int m;

    if (!exists("/dev_flash/ps1emu/ps1_netemu.self")) { log_step("direct: no ps1_netemu on this firmware"); return 0; }
    log_step("direct: mounting %s", path);
    if (!mount_only(path)) return 0;
    m = disc_image_mounted();
    log_step("direct: image mounted = %d", m);
    if (m != 1) return 0;

    snprintf(a_mc1, sizeof(a_mc1), "%s", card[0] ? card : "No Memory Card");
    snprintf(a_mc2, sizeof(a_mc2), "No Memory Card");
    args[0] = a_mc1;            /* memory card slot 1 */
    args[1] = a_mc2;            /* memory card slot 2 */
    args[2] = "0082";           /* region (as IRISMAN) */
    args[3] = "1600";           /* ps1_netemu */
    args[4] = "";
    args[5] = "1";
    args[6] = "2";              /* full screen */
    args[7] = "1";              /* smoothing */
    args[8] = NULL;
    log_step("direct: starting ps1_netemu, card '%s'", a_mc1);
    sound_shutdown();
    sysProcessExitSpawn2("/dev_flash/ps1emu/ps1_netemu.self", args, NULL, NULL, 0, 1001, SYS_PROCESS_SPAWN_STACK_SIZE_1M);
    log_step("direct: ps1_netemu did not start");
    return 0;
}

static int start_ps3(const Game *g, const char *path)
{
    char eboot[200];
    int m;
    log_step("direct: mounting %s", path);
    if (!mount_only(path)) return 0;
    m = disc_image_mounted();
    log_step("direct: image mounted = %d", m);
    eboot[0] = 0;
    if (g->id[0]) {                                 /* an installed update has the newer EBOOT */
        snprintf(eboot, sizeof(eboot), "/dev_hdd0/game/%s/USRDIR/EBOOT.BIN", g->id);
        if (!exists(eboot)) eboot[0] = 0;
    }
    if (!eboot[0]) strcpy(eboot, "/dev_bdvd/PS3_GAME/USRDIR/EBOOT.BIN");
    if (!exists(eboot)) { log_step("direct: %s not found", eboot); return 0; }
    log_step("direct: starting %s", eboot);
    sound_shutdown();
    sysProcessExitSpawn2(eboot, NULL, NULL, NULL, 0, 1001, SYS_PROCESS_SPAWN_STACK_SIZE_1M);
    log_step("direct: EBOOT did not start");
    return 0;
}

int direct_start(const Game *g, const char *path)
{
    if (g->plat == PLAT_PS1 && settings.direct_ps1) return start_ps1(path);
    if (g->plat == PLAT_PS3 && settings.direct_ps1) return start_ps3(g, path);
    return 0;
}

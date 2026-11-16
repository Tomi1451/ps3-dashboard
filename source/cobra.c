/* cobra.c - PS Dashboard's own game loader.

   Cobra is part of the custom firmware (and of PS3HEN). It is what lets a PS3 treat a game
   image on the hard drive as if it were a disc in the drive. Apps talk to it through lv2
   syscall 8 with an "opcode" saying what to do; these are the published Cobra opcodes.
   Everything here is our own code: nothing else needs to be installed for it to work. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ppu-lv2.h>
#include <lv2/sysfs.h>
#include <sys/file.h>

#include "cobra.h"
#include "log.h"

/* Cobra opcodes (syscall 8) */
#define OP_GET_VERSION        0x7000
#define OP_GET_VERSION2       0x7001
#define OP_GET_DISC_TYPE      0x7020
#define OP_FAKE_STORAGE_EVENT 0x7022
#define OP_MOUNT_PS3_DISCFILE 0x7024
#define OP_MOUNT_PSX_DISCFILE 0x7027
#define OP_MOUNT_PS2_DISCFILE 0x7028
#define OP_UMOUNT_DISCFILE    0x702C
#define OP_IS_HEN             0x1337

#define BDVD_DRIVE 0x101000000000006ULL
#define MAX_PARTS  32
#define MAX_TRACKS 99

typedef struct {                   /* one CD track, as Cobra wants it (SCSI track descriptor) */
    u8  reserved;
    u8  adr_control;               /* 0x14 data, 0x10 audio */
    u8  track_number;
    u8  reserved2;
    u32 track_start_addr;          /* sector (LBA) */
} __attribute__((packed)) Track;

/* ---------------------------------------------------------------- the calls */
static s32 sc8_1(u64 op)                          { lv2syscall1(8, op); return_to_user_prog(s32); }
static s32 sc8_2(u64 op, u64 a)                   { lv2syscall2(8, op, a); return_to_user_prog(s32); }
static s32 sc8_3(u64 op, u64 a, u64 b)            { lv2syscall3(8, op, a, b); return_to_user_prog(s32); }
static s32 sc8_4(u64 op, u64 a, u64 b, u64 c)     { lv2syscall4(8, op, a, b, c); return_to_user_prog(s32); }
static s32 sc8_5(u64 op, u64 a, u64 b, u64 c, u64 d) { lv2syscall5(8, op, a, b, c, d); return_to_user_prog(s32); }
static s32 map_path(const char *from, const char *to) { lv2syscall2(35, (u64)(u32) from, (u64)(u32) to); return_to_user_prog(s32); }

static int exists(const char *p) { sysFSStat st; return sysLv2FsStat(p, &st) == 0; }
static int is_dir(const char *p) { sysFSStat st; return sysLv2FsStat(p, &st) == 0 && ((st.st_mode & 0170000) == 0040000); }

static void disc_types(u32 *real, u32 *eff, u32 *img)
{
    *real = *eff = *img = 0;
    sc8_4(OP_GET_DISC_TYPE, (u64)(u32) real, (u64)(u32) eff, (u64)(u32) img);
}

/* ---------------------------------------------------------------- what's there */
int cobra_present(void)
{
    u32 v = 0;
    return sc8_2(OP_GET_VERSION, (u64)(u32) &v) == 0;
}

int cobra_is_hen(void) { return sc8_1(OP_IS_HEN) == OP_IS_HEN; }

const char *cobra_describe(void)
{
    static char s[48];
    u32 v1 = 0;
    u16 v = 0;
    if (sc8_2(OP_GET_VERSION, (u64)(u32) &v1) != 0) return "Not found";
    if (sc8_2(OP_GET_VERSION2, (u64)(u32) &v) != 0 || !v) { snprintf(s, sizeof(s), "Cobra (%s)", cobra_is_hen() ? "PS3HEN" : "CFW"); return s; }
    if ((v & 0x0F) == 0) snprintf(s, sizeof(s), "Cobra %X.%X (%s)", v >> 8, (v & 0xF0) >> 4, cobra_is_hen() ? "PS3HEN" : "CFW");
    else                 snprintf(s, sizeof(s), "Cobra %X.%02X (%s)", v >> 8, v & 0xFF, cobra_is_hen() ? "PS3HEN" : "CFW");
    return s;
}

/* ---------------------------------------------------------------- disc events
   After mounting, the system is told "a disc was inserted" so it reads the new one. */
static u32 ejected_real = 0;

static void fake_eject(void)
{
    u32 eff, img;
    disc_types(&ejected_real, &eff, &img);
    sc8_4(OP_FAKE_STORAGE_EVENT, 4, 0, BDVD_DRIVE);
    sc8_4(OP_FAKE_STORAGE_EVENT, 8, 0, BDVD_DRIVE);
}

static void fake_insert(void)
{
    u32 real, eff, img;
    disc_types(&real, &eff, &img);
    if (!ejected_real && !real && !eff && !img) return;          /* nothing to insert */
    u64 param = (u64) ejected_real << 32;
    disc_types(&ejected_real, &eff, &img);
    sc8_4(OP_FAKE_STORAGE_EVENT, 7, 0, BDVD_DRIVE);
    sc8_4(OP_FAKE_STORAGE_EVENT, 3, param, BDVD_DRIVE);
}

static void wait_for(const char *p, int tenths)
{
    while (tenths-- > 0 && !exists(p)) usleep(100000);
}

/* ---------------------------------------------------------------- unmount */
int game_unmount(void)
{
    u32 real, eff, img;
    if (!cobra_present()) return 0;
    map_path("/dev_bdvd", NULL);                                 /* folder games are mapped, not mounted */
    map_path("//dev_bdvd", NULL);
    map_path("/app_home", NULL);
    disc_types(&real, &eff, &img);
    if (img) {
        fake_eject();
        sc8_1(OP_UMOUNT_DISCFILE);
        usleep(20000);
        if (real) fake_insert();                                 /* the real disc comes back */
    }
    log_step("cobra: unmounted (image was 0x%X, real disc 0x%X)", img, real);
    return 1;
}

/* ---------------------------------------------------------------- images */
/* big ISOs on FAT32 USB drives are split: name.iso.0, name.iso.1, ... */
static int iso_parts(const char *path, char names[][420], u32 *list)
{
    int n = 1;
    size_t l = strlen(path);
    snprintf(names[0], 420, "%s", path);
    if (l > 2 && path[l - 2] == '.' && path[l - 1] == '0') {
        for (; n < MAX_PARTS; n++) {
            snprintf(names[n], 420, "%.*s.%d", (int) (l - 2), path, n);
            if (!exists(names[n])) break;
        }
    }
    for (int i = 0; i < n; i++) list[i] = (u32)(u64) names[i];
    return n;
}

/* MSF "mm:ss:ff" -> sector */
static int msf(const char *s)
{
    int m = 0, sec = 0, f = 0;
    if (sscanf(s, "%d:%d:%d", &m, &sec, &f) != 3) return -1;
    return (m * 60 + sec) * 75 + f;
}

/* tracks from the .cue next to a .bin (first track data, the rest CD audio).
   No .cue, or an .iso: one data track at sector 0. */
static int read_tracks(const char *image, Track *t)
{
    char cue[420], buf[8192], *p, *dot;
    s32 fd; u64 got = 0;
    int n = 0;
    memset(t, 0, sizeof(Track) * MAX_TRACKS);
    snprintf(cue, sizeof(cue), "%s", image);
    dot = strrchr(cue, '.');
    if (dot && strlen(dot) == 4) {
        strcpy(dot, ".cue");
        if (!exists(cue)) strcpy(dot, ".CUE");
        if (exists(cue) && sysLv2FsOpen(cue, SYS_O_RDONLY, &fd, 0, NULL, 0) == 0) {
            sysLv2FsRead(fd, buf, sizeof(buf) - 1, &got);
            sysLv2FsClose(fd);
            buf[got] = 0;
            for (p = buf; (p = strstr(p, "INDEX")) && n < MAX_TRACKS; p += 5) {
                char *q = p + 5;
                while (*q == ' ') q++;
                if (atoi(q) != 1) continue;                      /* only INDEX 01 starts a track */
                while (*q && *q != ' ') q++;
                while (*q == ' ') q++;
                int lba = msf(q);
                if (lba < 0) continue;
                t[n].adr_control = n ? 0x10 : 0x14;
                t[n].track_number = n + 1;
                t[n].track_start_addr = n ? lba : 0;
                n++;
            }
        }
    }
    if (!n) { t[0].adr_control = 0x14; t[0].track_number = 1; t[0].track_start_addr = 0; n = 1; }
    return n;
}

static int mount_ps3_image(const char *path)
{
    static char names[MAX_PARTS][420];
    static u32 list[MAX_PARTS];
    int n = iso_parts(path, names, list);
    s32 r = sc8_3(OP_MOUNT_PS3_DISCFILE, n, (u64)(u32)(u64) list);
    log_step("cobra: mount PS3 image (%d part%s) = %d", n, n == 1 ? "" : "s", r);
    if (r) return 0;
    usleep(2500);
    fake_insert();
    wait_for("/dev_bdvd/PS3_GAME/PARAM.SFO", 50);
    return 1;
}

static int mount_psx_image(const char *path)
{
    static Track t[MAX_TRACKS];
    static char file[420];
    int n = read_tracks(path, t);
    snprintf(file, sizeof(file), "%s", path);
    s32 r = sc8_4(OP_MOUNT_PSX_DISCFILE, (u64)(u32)(u64) file, n, (u64)(u32)(u64) t);
    log_step("cobra: mount PS1 image, %d track%s = %d", n, n == 1 ? "" : "s", r);
    if (r) return 0;
    usleep(2500);
    fake_insert();
    return 1;
}

static int mount_ps2_image(const char *path)
{
    static char names[MAX_PARTS][420];
    static u32 list[MAX_PARTS];
    static Track t[MAX_TRACKS];
    int n = iso_parts(path, names, list), nt = read_tracks(path, t);
    s32 r = sc8_5(OP_MOUNT_PS2_DISCFILE, n, (u64)(u32)(u64) list, nt, (u64)(u32)(u64) t);
    log_step("cobra: mount PS2 image (%d part%s, %d track%s) = %d", n, n == 1 ? "" : "s", nt, nt == 1 ? "" : "s", r);
    if (r) return 0;
    usleep(2500);
    fake_insert();
    return 1;
}

/* a PS3 game folder (GAMES/...): point /dev_bdvd at it */
static int map_folder(const char *path)
{
    s32 r = map_path("/dev_bdvd", path);
    log_step("cobra: map /dev_bdvd -> %s = %d", path, r);
    if (r) return 0;
    map_path("//dev_bdvd", path);
    map_path("/app_home", path);
    return 1;
}

/* ---------------------------------------------------------------- the one entry point */
int game_mount(const Game *g, const char *path, char *why, int why_len)
{
    why[0] = 0;
    if (!cobra_present()) { snprintf(why, why_len, "Cobra isn't running. Games are loaded by Cobra, part of your custom firmware or PS3HEN - check it's turned on."); return 0; }
    if (!exists(path)) { snprintf(why, why_len, "The game's file isn't there any more: %s", path); return 0; }
    game_unmount();
    usleep(200000);

    if (g->plat == PLAT_PS3) {
        if (is_dir(path)) return map_folder(path);
        if (mount_ps3_image(path)) return 1;
        snprintf(why, why_len, "Cobra couldn't mount the PS3 image.");
        return 0;
    }
    if (g->plat == PLAT_PS1) {
        if (mount_psx_image(path)) return 1;
        snprintf(why, why_len, "Cobra couldn't mount the PS1 image.");
        return 0;
    }
    /* PS2 */
    if (g->format == FMT_CLASSIC) { snprintf(why, why_len, "PS2 Classics (.BIN.ENC) aren't supported yet - use the ISO of the game instead."); return 0; }
    if (cobra_is_hen()) { snprintf(why, why_len, "PS3HEN can't play PS2 ISOs - this needs custom firmware (CFW)."); return 0; }
    if (mount_ps2_image(path)) return 1;
    snprintf(why, why_len, "Cobra couldn't mount the PS2 image.");
    return 0;
}

/* ---------------------------------------------------------------- drive tray and power */
void disc_eject(void)
{
    u32 dev = 0;
    u8 cmd[56];
    { lv2syscall4(600, BDVD_DRIVE, 0, (u64)(u32)(u64) &dev, 0); }   /* open the drive */
    memset(cmd, 0, sizeof(cmd));
    cmd[0x00] = 0x1B;          /* START STOP UNIT */
    cmd[0x01] = 0x01;          /* immediate */
    cmd[0x04] = 0x02;          /* eject */
    cmd[0x23] = 0x0C;
    { lv2syscall7(616, dev, 1, (u64)(u32)(u64) cmd, 56, 0, 0, 0); }
    { lv2syscall1(601, dev); }
    log_step("drive: eject");
}

static void power(u64 op) { lv2syscall4(379, op, 0, 0, 0); }
void power_restart(void) { log_step("power: restart"); power(0x200); }
void power_off(void)     { log_step("power: off"); power(0x1100); }

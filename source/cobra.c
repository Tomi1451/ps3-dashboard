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
#define BLANK_DISC "/dev_hdd0/tmp/psdb_disc.iso"     /* tiny stand-in disc for folder games */

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

/* A 128 KB "disc" with just the game's ID on it. Folder games are read from the HDD, but many
   (GTA V for one) check that a PS3 disc is in the drive and say the data is corrupted if not,
   so this stands in as the disc while /dev_bdvd points at the folder. The layout is the same
   minimal PS3 disc image Cobra-based loaders (webMAN MOD, IRISMAN) use for this. */
static int write_blank_disc(const char *title_id)
{
    u8 *buf = calloc(1, 128 * 1024);
    s32 fd; u64 wrote = 0;
    int ok = 0;
    if (!buf) return 0;
    buf[3] = 2;
    buf[0x17] = 0x3F;
    strcpy((char *)buf+0x800, "PlayStation3");
    memcpy(buf+0x810, title_id, 4);
    buf[0x814] = '-';
    memcpy(buf+0x815, title_id+4, 5);
    memset(buf+0x81A, ' ', 0x16);
    buf[0x8000] = 1;
    strcpy((char *)buf+0x8001, "CD001");
    buf[0x8006] = 1;
    memset(buf+0x8008, ' ', 0x20);
    memcpy(buf+0x8028, "PS3VOLUME", 9);
    memset(buf+0x8031, ' ', 0x17);
    buf[0x8050] = buf[0x8057] = 0x40;
    buf[0x8078] = buf[0x807B] = buf[0x807C] = buf[0x807F] = 1;
    buf[0x8081] = buf[0x8082] = 8;
    buf[0x8084] = buf[0x808B] = 0xA;
    buf[0x808C] = 0x14;
    buf[0x8097] = 0x15;
    buf[0x809C] = 0x22;
    buf[0x809E] = buf[0x80A5] = 0x18;
    buf[0x80A7] = buf[0x80AC] = 8;
    buf[0x80AE] = 0x6F;
    buf[0x80AF] = 7;
    buf[0x80B0] = 0x16;
    buf[0x80B1] = 2;
    buf[0x80B2] = 0x2B;
    buf[0x80B3] = buf[0x80B5] = 2;
    buf[0x80B8] = buf[0x80BB] = buf[0x80BC] = 1;
    memcpy(buf+0x80be, "PS3VOLUME", 9);
    memset(buf+0x80C7, ' ', 0x266);
    strcpy((char *)buf+0x832d, "2007111105051300");
    strcpy((char *)buf+0x833e, "0000000000000000");
    strcpy((char *)buf+0x834f, "0000000000000000");
    strcpy((char *)buf+0x8360, "0000000000000000");
    buf[0x8371] = 1;
    buf[0x8800] = 2;
    strcpy((char *)buf+0x8801, "CD001");
    buf[0x8806] = 1;
    buf[0x8829] = 'P';
    buf[0x882B] = 'S';
    buf[0x882D] = '3';
    buf[0x882F] = 'V';
    buf[0x8831] = 'O';
    buf[0x8833] = 'L';
    buf[0x8835] = 'U';
    buf[0x8837] = 'M';
    buf[0x8839] = 'E';
    buf[0x8850] = buf[0x8857] = 0x40;
    strcpy((char *)buf+0x8858, "%/@");
    buf[0x8878] = buf[0x887B] = buf[0x887C] = buf[0x887F] = 1;
    buf[0x8881] = buf[0x8882] = 8;
    buf[0x8884] = buf[0x888B] = 0xA;
    buf[0x888C] = 0x16;
    buf[0x8897] = 0x17;
    buf[0x889C] = 0x22;
    buf[0x889E] = buf[0x88A5] = 0x19;
    buf[0x88A7] = buf[0x88AC] = 8;
    buf[0x88AE] = 0x6F;
    buf[0x88AF] = 7;
    buf[0x88B0] = 0x16;
    buf[0x88B1] = 2;
    buf[0x88B2] = 0x2B;
    buf[0x88B3] = buf[0x88B5] = 2;
    buf[0x88B8] = buf[0x88BB] = buf[0x88BC] = 1;
    buf[0x88BF] = 'P';
    buf[0x88C1] = 'S';
    buf[0x88C3] = '3';
    buf[0x88C5] = 'V';
    buf[0x88C7] = 'O';
    buf[0x88C9] = 'L';
    buf[0x88CB] = 'U';
    buf[0x88CD] = 'M';
    buf[0x88CF] = 'E';
    strcpy((char *)buf+0x8B2D, "2007111105051300");
    strcpy((char *)buf+0x8B3E, "0000000000000000");
    strcpy((char *)buf+0x8B4F, "0000000000000000");
    strcpy((char *)buf+0x8b60, "0000000000000000");
    buf[0x8B71] = 1;
    buf[0x9000] = 0xFF;
    strcpy((char *)buf+0x9001, "CD001");
    buf[0xA000] = 1;
    buf[0xA002] = 0x18;
    buf[0xA006] = 1;
    buf[0xA800] = 1;
    buf[0xA805] = 0x18;
    buf[0xA807] = 1;
    buf[0xB000] = 1;
    buf[0xB002] = 0x19;
    buf[0xB006] = 1;
    buf[0xB800] = 1;
    buf[0xB805] = 0x19;
    buf[0xB807] = 1;
    buf[0xC000] = 0x28;
    buf[0xC002] = buf[0xC009] = 0x18;
    buf[0xC00B] = buf[0xC010] = 8;
    buf[0xC012] = 0x6F;
    buf[0xC013] = 7;
    buf[0xC014] = 0x16;
    buf[0xC015] = 2;
    buf[0xC016] = 0x2B;
    buf[0xC017] = buf[0xC019] = 2;
    buf[0xC01C] = buf[0xC01F] = buf[0xC020] = 1;
    buf[0xC028] = 0x28;
    buf[0xC02A] = buf[0xC031] = 0x18;
    buf[0xC033] = buf[0xC038] = 8;
    buf[0xC03A] = 0x6F;
    buf[0xC03B] = 7;
    buf[0xC03C] = 0x16;
    buf[0xC03D] = 2;
    buf[0xC03E] = 0x2B;
    buf[0xC03F] = buf[0xC041] = 2;
    buf[0xC044] = buf[0xC047] = buf[0xC048] = buf[0xC049] = 1;
    buf[0xC800] = 0x28;
    buf[0xC802] = buf[0xC809] = 0x19;
    buf[0xC80B] = buf[0xC810] = 8;
    buf[0xC812] = 0x6F;
    buf[0xC813] = 7;
    buf[0xC814] = 0x16;
    buf[0xC815] = 2;
    buf[0xC816] = 0x2B;
    buf[0xC817] = buf[0xC819] = 2;
    buf[0xC81C] = buf[0xC81F] = buf[0xC820] = 1;
    buf[0xC828] = 0x28;
    buf[0xC82A] = buf[0xC831] = 0x19;
    buf[0xC833] = buf[0xC838] = 8;
    buf[0xC83A] = 0x6F;
    buf[0xC83B] = 7;
    buf[0xC83C] = 0x16;
    buf[0xC83D] = 2;
    buf[0xC83E] = 0x2B;
    buf[0xC83F] = buf[0xC841] = 2;
    buf[0xC844] = buf[0xC847] = buf[0xC848] = buf[0xC849] = 1;

    if (sysLv2FsOpen(BLANK_DISC, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, 0666, NULL, 0) == 0) {
        ok = sysLv2FsWrite(fd, buf, 128 * 1024, &wrote) == 0 && wrote == 128 * 1024;
        sysLv2FsClose(fd);
    }
    free(buf);
    return ok;
}

/* the game's ID, from its folder name "Name [BLES01807]" or its PARAM.SFO */
static void folder_title_id(const Game *g, char *id)
{
    const char *b;
    strcpy(id, "TEST00000");
    if (g->id[0] && strlen(g->id) >= 9) { memcpy(id, g->id, 9); id[9] = 0; return; }
    b = strrchr(g->path, '[');
    if (b && strlen(b) >= 10 && b[10] == ']') { memcpy(id, b + 1, 9); id[9] = 0; }
}

/* a PS3 game folder (GAMES/...): point /dev_bdvd at it, with a stand-in disc in the drive */
static int map_folder(const Game *g, const char *path)
{
    char id[16];
    u32 real, eff, img;
    s32 r;
    folder_title_id(g, id);
    if (!write_blank_disc(id)) log_step("cobra: couldn't write %s", BLANK_DISC);
    r = map_path("/dev_bdvd", path);
    log_step("cobra: map /dev_bdvd -> %s = %d", path, r);
    if (r) return 0;
    map_path("//dev_bdvd", path);
    map_path("/app_home", path);
    disc_types(&real, &eff, &img);
    if (!real) {                                 /* no real disc: put the stand-in disc in */
        static char file[64];
        static u32 list[1];
        fake_eject();
        usleep(20000);
        strcpy(file, BLANK_DISC);
        list[0] = (u32)(u64) file;
        r = sc8_3(OP_MOUNT_PS3_DISCFILE, 1, (u64)(u32)(u64) list);
        log_step("cobra: stand-in disc for %s = %d", id, r);
        fake_insert();
        usleep(20000);
    }
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
        if (is_dir(path)) return map_folder(g, path);
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

/* copydisc.c - installs the PS3 disc in the drive as a folder game in /dev_hdd0/GAMES,
   right here in the dashboard. The disc's files are read through /dev_bdvd after asking
   lv2 to decrypt Blu-ray reads (syscall 36 on "/dev_bdvd" - the same call webMAN makes
   before copying a disc). PS3_UPDATE (the system update on the disc) is left out, as webMAN
   and multiMAN do. Works a few MB at a time from the main loop so the screen keeps moving. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ppu-lv2.h>
#include <lv2/sysfs.h>
#include <sys/file.h>
#include <sys/systime.h>

#include "copydisc.h"
#include "log.h"

#define MAX_FILES 20000
#define BUF_SIZE  (2 * 1024 * 1024)
#define S_ISDIR_PS3(m) (((m) & 0170000) == 0040000)

typedef struct { char *rel; u64 size; } Entry;

static struct {
    int    active, done, failed;
    char   src[64], dst[400], name[200], msg[220];
    Entry *files; int nfiles, cur;
    char **dirs;  int ndirs;
    u64    total, copied, cur_off;
    s32    in, out; int in_open, out_open;
    u8    *buf;
    int    made_dst;
} C;

static int bd_decrypt(const char *path) { lv2syscall1(36, (u64)(u32) path); return_to_user_prog(int); }
static int disk_free(const char *p, u64 *total, u64 *avail)
{ lv2syscall3(840, (u64)(u32) p, (u64)(u32) total, (u64)(u32) avail); return_to_user_prog(int); }
static u64 now_ms(void) { u64 s = 0, ns = 0; sysGetCurrentTime(&s, &ns); return s * 1000 + ns / 1000000; }

const char *cd_message(void) { return C.msg; }
int   cd_active(void)  { return C.active; }
int   cd_done(void)    { return C.done; }
int   cd_failed(void)  { return C.failed; }
const char *cd_output(void) { return C.dst; }
float cd_progress(void) { return C.total ? (float) ((double) C.copied / (double) C.total) : 0.0f; }
u64   cd_bytes_done(void)  { return C.copied; }
u64   cd_bytes_total(void) { return C.total; }

/* ---------------------------------------------------------------- listing */
static int add_file(const char *rel, u64 size)
{
    if (C.nfiles >= MAX_FILES) return 0;
    C.files[C.nfiles].rel = strdup(rel);
    C.files[C.nfiles].size = size;
    C.nfiles++;
    C.total += size;
    return 1;
}

static int add_dir(const char *rel)
{
    if (C.ndirs >= MAX_FILES) return 0;
    C.dirs[C.ndirs++] = strdup(rel);
    return 1;
}

static int walk(const char *rel)
{
    char full[600], child[600];
    s32 fd; sysFSDirent e; u64 rd;
    snprintf(full, sizeof(full), "%s%s", C.src, rel);
    if (sysLv2FsOpenDir(full, &fd) != 0) { log_step("copy: can't list %s", full); return 0; }
    while (sysLv2FsReadDir(fd, &e, &rd) == 0 && rd > 0) {
        if (!strcmp(e.d_name, ".") || !strcmp(e.d_name, "..")) continue;
        snprintf(child, sizeof(child), "%s/%s", rel, e.d_name);
        snprintf(full, sizeof(full), "%s%s", C.src, child);
        sysFSStat st;
        if (sysLv2FsStat(full, &st) != 0) continue;
        if (S_ISDIR_PS3(st.st_mode)) {
            if (!add_dir(child)) { sysLv2FsCloseDir(fd); return 0; }
            if (!strcmp(child, "/PS3_UPDATE")) continue;            /* keep the folder, skip the firmware update */
            if (!walk(child)) { sysLv2FsCloseDir(fd); return 0; }
        } else if (!add_file(child, st.st_size)) { sysLv2FsCloseDir(fd); return 0; }
    }
    sysLv2FsCloseDir(fd);
    return 1;
}

/* ---------------------------------------------------------------- tidy up */
static void close_files(void)
{
    if (C.in_open) { sysLv2FsClose(C.in); C.in_open = 0; }
    if (C.out_open) { sysLv2FsClose(C.out); C.out_open = 0; }
}

static void free_lists(void)
{
    int i;
    for (i = 0; i < C.nfiles; i++) free(C.files[i].rel);
    for (i = 0; i < C.ndirs; i++) free(C.dirs[i]);
    free(C.files); free(C.dirs); C.files = NULL; C.dirs = NULL;
    if (C.buf) { free(C.buf); C.buf = NULL; }
}

/* removes only what this copy made: its files, then its folders (deepest first) */
static void remove_partial(void)
{
    char p[600];
    int i;
    if (!C.made_dst) return;
    for (i = 0; i < C.nfiles; i++) { snprintf(p, sizeof(p), "%s%s", C.dst, C.files[i].rel); sysLv2FsUnlink(p); }
    for (i = C.ndirs - 1; i >= 0; i--) { snprintf(p, sizeof(p), "%s%s", C.dst, C.dirs[i]); sysLv2FsRmdir(p); }
    sysLv2FsRmdir(C.dst);
}

static int fail(const char *why, int code)
{
    snprintf(C.msg, sizeof(C.msg), "%s (code %08X)", why, (u32) code);
    log_step("copy: FAILED %s", C.msg);
    close_files();
    remove_partial();
    free_lists();
    C.active = 0; C.failed = 1;
    return 0;
}

/* ---------------------------------------------------------------- start */
int cd_begin(const char *title, const char *id)
{
    char clean[200];
    size_t j = 0;
    const char *s;
    int i, r;
    sysFSStat st;
    memset(&C, 0, sizeof(C));
    strcpy(C.src, "/dev_bdvd");

    for (s = title[0] ? title : (id[0] ? id : "PS3 disc"); *s && j + 1 < sizeof(clean); s++)
        if (!strchr("\\/:*?\"<>|", *s) && (unsigned char) *s >= 32) clean[j++] = *s;
    while (j && clean[j - 1] == ' ') j--;
    clean[j] = 0;
    if (id[0] && title[0]) snprintf(C.name, sizeof(C.name), "%s [%s]", clean, id); else snprintf(C.name, sizeof(C.name), "%s", clean);
    snprintf(C.dst, sizeof(C.dst), "/dev_hdd0/GAMES/%s", C.name);
    snprintf(C.msg, sizeof(C.msg), "%s", C.name);

    r = bd_decrypt("/dev_bdvd");
    log_step("copy: Blu-ray decrypt = %d", r);
    if (sysLv2FsStat("/dev_bdvd/PS3_GAME/PARAM.SFO", &st) != 0) return fail("No PS3 game disc found in the drive", 0);

    C.files = calloc(MAX_FILES, sizeof(Entry));
    C.dirs = calloc(MAX_FILES, sizeof(char *));
    C.buf = memalign(128, BUF_SIZE);
    if (!C.files || !C.dirs || !C.buf) return fail("Not enough memory", 0);
    if (!walk("")) return fail("Couldn't read the list of files on the disc", 0);
    log_step("copy: %d files in %d folders, %.2f GB", C.nfiles, C.ndirs, C.total / 1073741824.0);

    {
        u64 tot = 0, avail = 0;
        if (disk_free("/dev_hdd0", &tot, &avail) == 0 && avail < C.total + 256ULL * 1024 * 1024) {
            snprintf(C.msg, sizeof(C.msg), "Not enough free space: needs %.1f GB", C.total / 1073741824.0);
            return fail(C.msg, 0);
        }
    }
    if (sysLv2FsStat(C.dst, &st) == 0) return fail("This game is already installed in GAMES", 0);

    sysLv2FsMkdir("/dev_hdd0/GAMES", 0777);
    if (sysLv2FsMkdir(C.dst, 0777) != 0) return fail("Couldn't create the folder on the HDD", 0);
    C.made_dst = 1;
    for (i = 0; i < C.ndirs; i++) {
        char p[600];
        snprintf(p, sizeof(p), "%s%s", C.dst, C.dirs[i]);
        sysLv2FsMkdir(p, 0777);
    }
    C.active = 1;
    log_step("copy: /dev_bdvd -> %s", C.dst);
    return 1;
}

/* ---------------------------------------------------------------- work */
int cd_step(void)
{
    u64 t0 = now_ms();
    char sp[600], dp[600];
    if (!C.active) return 0;
    while (now_ms() - t0 < 40) {                       /* about 40 ms of copying per frame */
        if (C.cur >= C.nfiles) {
            close_files();
            free_lists();
            C.active = 0; C.done = 1;
            log_step("copy: finished, %.2f GB", C.copied / 1073741824.0);
            return 0;
        }
        Entry *f = &C.files[C.cur];
        if (!C.in_open) {
            snprintf(sp, sizeof(sp), "%s%s", C.src, f->rel);
            snprintf(dp, sizeof(dp), "%s%s", C.dst, f->rel);
            int r = sysLv2FsOpen(sp, SYS_O_RDONLY, &C.in, 0, NULL, 0);
            if (r) { snprintf(C.msg, sizeof(C.msg), "Couldn't read %s from the disc", f->rel); return fail(C.msg, r); }
            C.in_open = 1;
            r = sysLv2FsOpen(dp, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &C.out, 0666, NULL, 0);
            if (r) { snprintf(C.msg, sizeof(C.msg), "Couldn't write %s to the HDD", f->rel); return fail(C.msg, r); }
            C.out_open = 1;
            C.cur_off = 0;
        }
        u64 got = 0, w = 0;
        int r = sysLv2FsRead(C.in, C.buf, BUF_SIZE, &got);
        if (r) {
            snprintf(C.msg, sizeof(C.msg), "Read error in %s - the disc may be dirty or scratched", f->rel);
            return fail(C.msg, r);
        }
        if (got) {
            r = sysLv2FsWrite(C.out, C.buf, got, &w);
            if (r || w != got) return fail("Couldn't write to the HDD", r);
            C.cur_off += got; C.copied += got;
        }
        if (!got || C.cur_off >= f->size) {
            close_files();
            snprintf(dp, sizeof(dp), "%s%s", C.dst, f->rel);
            sysLv2FsChmod(dp, 0666);
            C.cur++;
        }
    }
    return 1;
}

void cd_cancel(void)
{
    if (!C.active) return;
    log_step("copy: cancelled at %.2f GB", C.copied / 1073741824.0);
    close_files();
    remove_partial();
    free_lists();
    C.active = 0; C.failed = 1;
    snprintf(C.msg, sizeof(C.msg), "Cancelled - the unfinished copy was removed.");
}

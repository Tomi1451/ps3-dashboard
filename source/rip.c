/* rip.c - makes a disc image from the PS1 / PS2 disc in the drive, reading the Blu-ray
   drive directly through the lv2 storage syscalls (the same ones SACD Ripper and prepISO use).

   PS2  (DVD or CD): plain 2048-byte sectors          -> /dev_hdd0/PS2ISO/<name>.iso
   PS1  (CD)       : every sector in full, 2352 bytes  -> /dev_hdd0/PSXISO/<name>/<name>.bin + .cue
                     (full sectors keep the video / music streams and the CD audio tracks
                      that a normal file copy loses; the .cue lists the tracks)

   Work is done a few sectors at a time from the main loop, so the screen keeps drawing
   and the user can cancel. Every step is logged. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ppu-lv2.h>
#include <lv2/sysfs.h>
#include <sys/file.h>

#include "rip.h"
#include "log.h"

#define BD_DEVICE 0x0101000000000006ULL

/* ------------------------------------------------------------ lv2 storage */
typedef struct {
    u8  pkt[32];
    u32 pktlen;
    u32 blocks;
    u32 block_size;
    u32 proto;
    u32 in_out;
    u32 unknown;
} __attribute__((packed)) atapi_cmd;

typedef struct {
    u8  name[7];
    u8  unknown01;
    u32 unknown02;
    u32 zero01;
    u32 unknown03;
    u32 unknown04;
    u8  zero02[16];
    u64 total_sectors;
    u32 sector_size;
    u32 unknown05;
    u8  writable;
    u8  unknown06[3];
    u32 unknown07;
} __attribute__((packed)) device_info;

enum { PROTO_PIO_IN = 1, PROTO_DMA = 3 };

static int st_open(u64 id, u32 *fd)        { lv2syscall4(600, id, 0, (u64)(u32) fd, 0); return_to_user_prog(int); }
static int st_close(u32 fd)                { lv2syscall1(601, fd); return_to_user_prog(int); }
static int st_read(u32 fd, u32 lba, u32 n, void *buf, u32 *got)
{ lv2syscall7(602, fd, 0, lba, n, (u64)(u32) buf, (u64)(u32) got, 0); return_to_user_prog(int); }
static int st_info(u64 id, device_info *di) { lv2syscall2(609, id, (u64)(u32) di); return_to_user_prog(int); }
static int st_atapi(u32 fd, atapi_cmd *c, void *buf)
{
    u64 tag;
    lv2syscall7(616, fd, 1, (u64)(u32) c, sizeof(atapi_cmd), (u64)(u32) buf, c->block_size, (u64)(u32) &tag);
    return_to_user_prog(int);
}
static int disc_auth(void)                 { lv2syscall2(864, 0x5004, 0x29); return_to_user_prog(int); }
static int disk_free(const char *p, u64 *total, u64 *avail)
{ lv2syscall3(840, (u64)(u32) p, (u64)(u32) total, (u64)(u32) avail); return_to_user_prog(int); }

static int proto = PROTO_DMA;                 /* switched to PIO if the drive refuses DMA */

static int atapi_in(u32 fd, const u8 *pkt, void *buf, u32 len)
{
    atapi_cmd c;
    int r;
    memset(&c, 0, sizeof(c));
    memcpy(c.pkt, pkt, 12);
    c.pktlen = 12; c.blocks = 1; c.block_size = len; c.in_out = 1; c.proto = proto;
    r = st_atapi(fd, &c, buf);
    if (r && proto == PROTO_DMA) {            /* some drives / firmwares want PIO */
        c.proto = PROTO_PIO_IN;
        if (st_atapi(fd, &c, buf) == 0) { proto = PROTO_PIO_IN; log_step("rip: ATAPI using PIO"); return 0; }
    }
    return r;
}

/* READ CD (0xBE): n full 2352-byte sectors from lba. audio=1 for CD-DA tracks */
static int read_cd_raw(u32 fd, u32 lba, u32 n, void *buf, int audio)
{
    u8 p[12] = { 0xBE, 0, lba >> 24, lba >> 16, lba >> 8, lba, n >> 16, n >> 8, n, audio ? 0x10 : 0xF8, 0, 0 };
    return atapi_in(fd, p, buf, n * 2352);
}

/* READ TOC (0x43) format 0, LBA addresses */
static int read_toc(u32 fd, u8 *buf, u32 len)
{
    u8 p[12] = { 0x43, 0, 0, 0, 0, 0, 0, len >> 8, len & 0xff, 0, 0, 0 };
    return atapi_in(fd, p, buf, len);
}

/* ------------------------------------------------------------ state */
#define CHUNK_DVD 64                 /* sectors per step: 128 KB */
#define CHUNK_CD  24                 /* 56 KB */
#define MAX_TRACKS 99

static struct {
    int  active, type, done, failed;
    u32  fd; int fd_open;
    s32  out; int out_open;
    u32  total, pos;
    int  sector;                     /* 2048 or 2352 */
    int  ntracks;
    u32  tstart[MAX_TRACKS + 1];     /* last entry = lead-out */
    u8   taudio[MAX_TRACKS];
    char path[512], cue[512], dir[400], fname[300];
    char msg[200];
    u8  *buf;
    int  retries;
    int  created, made_dir;          /* only ever delete what we made */
    char want_id[24];                /* game ID of the disc in the drive */
    int  id_checked;
} R;

/* "SCES_003.11" / "SCES-00311" -> "SCES00311" */
static void id_key(char *dst, const char *src)
{
    int j = 0;
    for (; *src && j < 15; src++)
        if ((*src >= 'A' && *src <= 'Z') || (*src >= '0' && *src <= '9')) dst[j++] = *src;
        else if (*src >= 'a' && *src <= 'z') dst[j++] = *src - 32;
    dst[j] = 0;
}

/* looks for SYSTEM.CNF's "cdrom:\\SCES_003.11;1" in what was just read; 1 = found (id filled) */
static int find_boot_id(const u8 *buf, u32 len, char *id)
{
    u32 i;
    for (i = 0; i + 24 < len; i++) {
        if (buf[i] != 'c' || memcmp(buf + i, "cdrom", 5)) continue;
        const u8 *p = buf + i + 5;
        int k = 0;
        while (k < 4 && *p != '\\' && *p != ':') { p++; k++; }
        while (*p == '\\' || *p == ':') p++;
        char raw[24]; int j = 0;
        while (j < 20 && p[j] && p[j] != ';' && p[j] > ' ') { raw[j] = p[j]; j++; }
        raw[j] = 0;
        id_key(id, raw);
        if (strlen(id) >= 8) return 1;
    }
    return 0;
}

const char *rip_message(void) { return R.msg; }
int  rip_active(void)         { return R.active; }
int  rip_done(void)           { return R.done; }
int  rip_failed(void)         { return R.failed; }
const char *rip_output(void)  { return R.path; }
float rip_progress(void)      { return R.total ? (float) R.pos / (float) R.total : 0.0f; }
u64  rip_bytes_done(void)     { return (u64) R.pos * R.sector; }
u64  rip_bytes_total(void)    { return (u64) R.total * R.sector; }

static void cleanup(int delete_output)
{
    if (R.out_open) { sysLv2FsClose(R.out); R.out_open = 0; }
    if (R.fd_open) { st_close(R.fd); R.fd_open = 0; }
    if (delete_output && R.created) {
        sysLv2FsUnlink(R.path);
        if (R.cue[0]) sysLv2FsUnlink(R.cue);
    }
    if (delete_output && R.made_dir) sysLv2FsRmdir(R.dir);
    if (R.buf) { free(R.buf); R.buf = NULL; }
    R.active = 0;
}

static int fail(const char *why, int code)
{
    snprintf(R.msg, sizeof(R.msg), "%s (code %08X)", why, (u32) code);
    log_step("rip: FAILED %s", R.msg);
    R.failed = 1;
    cleanup(1);
    return 0;
}

static int file_exists(const char *p) { sysFSStat st; return sysLv2FsStat(p, &st) == 0; }

/* characters the HDD / webMAN don't like are dropped */
static void clean_name(char *dst, size_t n, const char *src)
{
    size_t j = 0;
    for (; *src && j + 1 < n; src++)
        if (!strchr("\\/:*?\"<>|", *src) && (unsigned char) *src >= 32) dst[j++] = *src;
    while (j && dst[j - 1] == ' ') j--;
    dst[j] = 0;
}

int rip_begin(int type, const char *name, const char *id)
{
    device_info di;
    int r;
    char clean[160];
    memset(&R, 0, sizeof(R));
    R.type = type;
    R.out = -1;

    id_key(R.want_id, id);
    log_step("rip: expecting game ID '%s'", R.want_id[0] ? R.want_id : "(unknown - can't double-check)");
    clean_name(clean, sizeof(clean), name);
    if (!clean[0]) strcpy(clean, id[0] ? id : "Disc");
    if (id[0] && !strstr(clean, id)) snprintf(R.fname, sizeof(R.fname), "%s [%s]", clean, id);   /* ID kept for covers / game info */
    else snprintf(R.fname, sizeof(R.fname), "%s", clean);

    R.buf = memalign(128, CHUNK_DVD * 2048 > CHUNK_CD * 2352 ? CHUNK_DVD * 2048 : CHUNK_CD * 2352);
    if (!R.buf) return fail("Not enough memory", 0);

    r = st_info(BD_DEVICE, &di);
    log_step("rip: device info %d, sectors %llu, size %u", r, (unsigned long long) di.total_sectors, di.sector_size);
    r = st_open(BD_DEVICE, &R.fd);
    if (r) { disc_auth(); r = st_open(BD_DEVICE, &R.fd); }
    if (r) return fail("Couldn't open the disc drive", r);
    R.fd_open = 1;

    if (type == RIP_PS2) {
        R.sector = 2048;
        /* size from the ISO9660 volume descriptor (sector 16), else what the drive reports */
        r = st_read(R.fd, 16, 1, R.buf, &R.pos);
        if (r) { disc_auth(); r = st_read(R.fd, 16, 1, R.buf, &R.pos); }
        if (r) return fail("The drive won't read this disc", r);
        R.pos = 0;
        if (!memcmp(R.buf + 1, "CD001", 5)) R.total = R.buf[80] | R.buf[81] << 8 | R.buf[82] << 16 | (u32) R.buf[83] << 24;
        if (!R.total || (di.total_sectors && R.total > di.total_sectors)) R.total = (u32) di.total_sectors;
        if (!R.total) return fail("Couldn't find the size of the disc", 0);
        sysLv2FsMkdir("/dev_hdd0/PS2ISO", 0777);
        snprintf(R.path, sizeof(R.path), "/dev_hdd0/PS2ISO/%s.iso", R.fname);
    } else {
        u8 *t = R.buf;
        int i, n;
        R.sector = 2352;
        r = read_toc(R.fd, t, 804);
        if (r) { disc_auth(); r = read_toc(R.fd, t, 804); }
        if (r) return fail("Couldn't read the disc's track list", r);
        n = ((t[0] << 8 | t[1]) - 2) / 8;           /* descriptors incl. lead-out */
        for (i = 0; i < n && R.ntracks < MAX_TRACKS; i++) {
            const u8 *d = t + 4 + i * 8;
            u32 lba = (u32) d[4] << 24 | d[5] << 16 | d[6] << 8 | d[7];
            if (d[2] == 0xAA) { R.total = lba; continue; }
            R.tstart[R.ntracks] = lba;
            R.taudio[R.ntracks] = !(d[1] & 0x04);
            R.ntracks++;
        }
        R.tstart[R.ntracks] = R.total;
        log_step("rip: %d track(s), %u sectors", R.ntracks, R.total);
        if (!R.ntracks || !R.total) return fail("The disc's track list is empty", 0);
        /* raw read test on the first sector */
        r = read_cd_raw(R.fd, 0, 1, R.buf, R.taudio[0]);
        if (r) { disc_auth(); r = read_cd_raw(R.fd, 0, 1, R.buf, R.taudio[0]); }
        if (r) return fail("The drive won't read this disc in full (raw) mode", r);
        sysLv2FsMkdir("/dev_hdd0/PSXISO", 0777);
        snprintf(R.dir, sizeof(R.dir), "/dev_hdd0/PSXISO/%s", R.fname);
        if (file_exists(R.dir)) { R.dir[0] = 0; return fail("A game with that name is already there - pick another name", 0); }
        if (sysLv2FsMkdir(R.dir, 0777) == 0) R.made_dir = 1;
        snprintf(R.path, sizeof(R.path), "%s/%s.bin", R.dir, R.fname);
        snprintf(R.cue, sizeof(R.cue), "%s/%s.cue", R.dir, R.fname);
    }

    {   /* room for it? */
        u64 tot = 0, avail = 0;
        if (disk_free("/dev_hdd0", &tot, &avail) == 0 && avail < (u64) R.total * R.sector + 64ULL * 1024 * 1024) {
            snprintf(R.msg, sizeof(R.msg), "Not enough free space: needs %.1f GB", (double) R.total * R.sector / 1073741824.0);
            return fail(R.msg, 0);
        }
    }
    if (file_exists(R.path)) return fail("A game with that name is already there - pick another name", 0);

    r = sysLv2FsOpen(R.path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &R.out, 0666, NULL, 0);
    if (r) return fail("Couldn't create the file on the HDD", r);
    R.out_open = 1; R.created = 1;
    R.active = 1;
    snprintf(R.msg, sizeof(R.msg), "%s", R.fname);
    log_step("rip: %s -> %s (%u sectors of %d)", type == RIP_PS1 ? "PS1" : "PS2", R.path, R.total, R.sector);
    return 1;
}

static int track_of(u32 lba)
{
    int t;
    for (t = R.ntracks - 1; t > 0; t--) if (lba >= R.tstart[t]) return t;
    return 0;
}

static void write_cue(void)
{
    char out[4096];
    int n = 0, t;
    s32 fd; u64 w;
    n += snprintf(out + n, sizeof(out) - n, "FILE \"%s.bin\" BINARY\r\n", R.fname);
    for (t = 0; t < R.ntracks; t++) {
        u32 l = R.tstart[t];
        n += snprintf(out + n, sizeof(out) - n, "  TRACK %02d %s\r\n    INDEX 01 %02u:%02u:%02u\r\n", t + 1,
                      R.taudio[t] ? "AUDIO" : "MODE2/2352", l / 75 / 60, l / 75 % 60, l % 75);
    }
    if (sysLv2FsOpen(R.cue, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, 0666, NULL, 0) == 0) {
        sysLv2FsWrite(fd, out, n, &w);
        sysLv2FsClose(fd);
        sysLv2FsChmod(R.cue, 0666);
    }
}

int rip_step(void)
{
    int r;
    u32 n, got = 0;
    u64 w = 0;
    if (!R.active) return 0;
    if (R.pos >= R.total) {
        cleanup(0);
        sysLv2FsChmod(R.path, 0666);
        if (R.type == RIP_PS1) write_cue();
        R.done = 1;
        log_step("rip: finished, %u sectors", R.total);
        return 0;
    }
    if (R.type == RIP_PS2) {
        n = R.total - R.pos; if (n > CHUNK_DVD) n = CHUNK_DVD;
        r = st_read(R.fd, R.pos, n, R.buf, &got);
        if (r == 0 && got == 0) got = n;
    } else {
        n = R.total - R.pos; if (n > CHUNK_CD) n = CHUNK_CD;
        int t = track_of(R.pos);
        if (R.pos + n > R.tstart[t + 1]) n = R.tstart[t + 1] - R.pos;   /* don't cross a track */
        r = read_cd_raw(R.fd, R.pos, n, R.buf, R.taudio[t]);
        got = n;
    }
    if (r) {
        if (++R.retries <= 4) { log_step("rip: read error %08X at sector %u, retrying", (u32) r, R.pos); return 1; }
        snprintf(R.msg, sizeof(R.msg), "Read error at %u of %u - the disc may be dirty or scratched", R.pos, R.total);
        return fail(R.msg, r);
    }
    R.retries = 0;
    /* make sure this really is the disc in the drive (a game image left mounted by webMAN
       would be read instead): its SYSTEM.CNF must name the same game ID */
    if (!R.id_checked && R.want_id[0]) {
        char found[24];
        if (find_boot_id(R.buf, got * R.sector, found)) {
            R.id_checked = 1;
            log_step("rip: disc says %s, expected %s", found, R.want_id);
            if (strcmp(found, R.want_id)) {
                snprintf(R.msg, sizeof(R.msg), "Stopped: the drive gave a different game (%s) instead of %s. A mounted game image was probably in the way - try again.", found, R.want_id);
                return fail(R.msg, 0);
            }
        } else if (R.pos > 4000) {
            R.id_checked = 1;
            log_step("rip: couldn't find the boot file to double-check the game ID");
        }
    }
    r = sysLv2FsWrite(R.out, R.buf, (u64) got * R.sector, &w);
    if (r || w != (u64) got * R.sector) return fail("Couldn't write to the HDD", r);
    R.pos += got;
    return 1;
}

void rip_cancel(void)
{
    if (!R.active) return;
    log_step("rip: cancelled at %u of %u", R.pos, R.total);
    cleanup(1);
    snprintf(R.msg, sizeof(R.msg), "Cancelled - the unfinished file was removed.");
    R.failed = 1;
}

/* ftp.h - the dashboard's own FTP server (for the PC cover and game info tool) */
#ifndef FTP_H
#define FTP_H

enum { FTP_OFF = 0, FTP_STARTING, FTP_RUNNING, FTP_PORT_BUSY, FTP_FAILED };

void        ftp_start(void);
int         ftp_status(void);
const char *ftp_ip(void);        /* e.g. "192.168.1.20", "" when not on a network */

#endif

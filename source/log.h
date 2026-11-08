/* log.h - start-up log at /dev_hdd0/tmp/ps_dashboard.log (helps find problems on the console) */
#ifndef LOG_H
#define LOG_H
void log_reset(void);
void log_step(const char *fmt, ...);
#endif

/*
 * syslog.h
 *
 */
#ifndef SYSLOG_H
#define SYSLOG_H
#include <pce\pce.h>



// Logging
#define LOG_ERR   0
#define LOG_WARN  1
#define LOG_INFO  2
#define LOG_DBG   3




/*
 * Log
 *
 */
void Log(uib level, const char* pstr, ...);


/*
 * SyslogStartup
 *
 */
uib SyslogStartup(const char* pFilename);



#endif
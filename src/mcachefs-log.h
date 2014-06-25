/*
 * mcachefs-log.h
 *
 *  Created on: 21 oct. 2009
 *      Author: francois
 */

#ifndef MCACHEFSLOG_H_
#define MCACHEFSLOG_H_

// #define __MCACHEFS_USES_SYSLOG

#ifdef __MCACHEFS_USES_SYSLOG
#include <syslog.h>
#endif
/**
 * Logging facility
 */

#ifdef __MCACHEFS_USES_SYSLOG
#define Log(...)  syslog(LOG_DEBUG, __VA_ARGS__)
#define Info(...) syslog(LOG_INFO, __VA_ARGS__)
#define Err(...)  syslog(LOG_ERR, __VA_ARGS__)
#define Bug(...)  do { syslog(LOG_CRIT, __VA_ARGS__); ((char*)NULL)[0] = 0; } while(0)

#else
#define __Log(__prefix,__fmt,...) \
  do { \
    struct timeb tb; ftime(&tb); \
    struct tm tbm; localtime_r ( &(tb.time), &tbm ); \
    char tbuff[64]; strftime ( tbuff, 64, "%y%m%d:%H%M%S", &tbm ); \
    fprintf(mcachefs_config_log_fd(), __prefix "|%lx|%s:%d|" __FILE__ ":%d:%s|" __fmt, \
        pthread_self(), tbuff, tb.millitm, __LINE__,  __FUNCTION__, \
        ##__VA_ARGS__ ); \
    fflush(mcachefs_config_log_fd()); } \
  while(0)

#define Log(...) \
  do { \
    if(mcachefs_config_verbose() > 50 ) \
      __Log("LOG",__VA_ARGS__); \
   } while (0)

#define Info(...) __Log("INF",__VA_ARGS__)
#define Warn(...) __Log("WRN",__VA_ARGS__)
#define Err(...) __Log("ERR",__VA_ARGS__)
#define Bug(...) do { __Log("BUG",__VA_ARGS__); ((char*)NULL)[0] = 0; } while(0)
#endif



#endif /* MCACHEFSLOG_H_ */

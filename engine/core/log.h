// SkyEngine logging
#pragma once
#include <cstdio>
#include <cstdarg>

namespace sky {

enum LogLevel { LOG_DEBUG=0, LOG_INFO, LOG_WARN, LOG_ERROR };

inline LogLevel& log_level(){ static LogLevel l=LOG_INFO; return l; }
inline void set_log_level(LogLevel l){ log_level()=l; }

inline void log_msg(LogLevel lv, const char* tag, const char* fmt, ...) {
  if(lv<log_level()) return;
  const char* names[]={"D","I","W","E"};
  std::fprintf(stderr,"[%s][%s] ", names[lv], tag);
  va_list ap; va_start(ap,fmt);
  std::vfprintf(stderr,fmt,ap);
  va_end(ap);
  std::fprintf(stderr,"\n");
}

#define SKY_LOG_DEBUG(...) sky::log_msg(sky::LOG_DEBUG,"sky",__VA_ARGS__)
#define SKY_LOG_INFO(...)  sky::log_msg(sky::LOG_INFO,"sky",__VA_ARGS__)
#define SKY_LOG_WARN(...)  sky::log_msg(sky::LOG_WARN,"sky",__VA_ARGS__)
#define SKY_LOG_ERROR(...) sky::log_msg(sky::LOG_ERROR,"sky",__VA_ARGS__)

} // namespace sky

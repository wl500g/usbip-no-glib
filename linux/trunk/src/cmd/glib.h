#ifndef __G_LIB_H__
#define __G_LIB_H__

#include <syslog.h>

typedef enum
{
  /* log flags */
  G_LOG_FLAG_RECURSION          = 1 << 0,
  G_LOG_FLAG_FATAL              = 1 << 1,

  /* GLib log levels */
  G_LOG_LEVEL_ERROR             = 1 << 2,       /* always fatal */
  G_LOG_LEVEL_CRITICAL          = 1 << 3,
  G_LOG_LEVEL_WARNING           = 1 << 4,
  G_LOG_LEVEL_MESSAGE           = 1 << 5,
  G_LOG_LEVEL_INFO              = 1 << 6,
  G_LOG_LEVEL_DEBUG             = 1 << 7,

  G_LOG_LEVEL_MASK              = ~(G_LOG_FLAG_RECURSION | G_LOG_FLAG_FATAL)
} GLogLevelFlags;

#define g_error(...)	{					 \
			syslog (G_LOG_LEVEL_ERROR, __VA_ARGS__); \
			for (;;);				 \
			}
#define g_message(...)	syslog (G_LOG_LEVEL_MESSAGE, __VA_ARGS__)
#define g_critical(...)	syslog (G_LOG_LEVEL_CRITICAL,__VA_ARGS__)
#define g_warning(...)  syslog (G_LOG_LEVEL_WARNING, __VA_ARGS__)
#define g_debug(...)    syslog (G_LOG_LEVEL_DEBUG, __VA_ARGS__)

#endif

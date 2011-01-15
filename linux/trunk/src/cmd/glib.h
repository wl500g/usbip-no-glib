#ifndef __G_LIB_H__
#define __G_LIB_H__

#include <syslog.h>
#include <errno.h>

#define	FALSE	(0)
#define	TRUE	(!FALSE)

typedef char   gchar;
typedef short  gshort;
typedef long   glong;
typedef int    gint;
typedef gint   gboolean;

typedef unsigned char   guchar;
typedef unsigned short  gushort;
typedef unsigned long   gulong;
typedef unsigned int    guint;

typedef float   gfloat;
typedef double  gdouble;

typedef void* gpointer;
typedef const void *gconstpointer;

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
#define g_warning(...)	syslog (G_LOG_LEVEL_WARNING, __VA_ARGS__)
#define g_debug(...)	syslog (G_LOG_LEVEL_DEBUG, __VA_ARGS__)

#define g_strerror(x)	strerror(x)
#define gai_strerror(x)	strerror(x)

typedef void GMainContext;
typedef void GMainLoop;

GMainLoop *g_main_loop_new        (GMainContext *context,
			    	   gboolean      is_running);
void       g_main_loop_run        (GMainLoop    *loop);
void       g_main_loop_quit       (GMainLoop    *loop);
GMainLoop *g_main_loop_ref        (GMainLoop    *loop);
void       g_main_loop_unref      (GMainLoop    *loop);
gboolean   g_main_loop_is_running (GMainLoop    *loop);
GMainContext *g_main_loop_get_context (GMainLoop    *loop);

typedef enum
{
  G_IO_IN,
  G_IO_OUT,
  G_IO_PRI,
  G_IO_ERR,
  G_IO_HUP,
  G_IO_NVAL,
} GIOCondition;

typedef void GIOChannel;

#endif

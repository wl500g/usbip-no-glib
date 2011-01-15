#include <stdio.h>
#include <unistd.h>
#include "glib-stub.h"

struct _GMainContext
{
	gint ref_count;
};

struct _GMainLoop
{
	gint ref_count;
	GMainContext *context;
	gboolean is_running;
};

struct _GIOChannel
{
	gint ref_count;
	GIOFuncs *funcs;
	int fd;
};

struct _GIOFuncs
{
	GIOStatus (*io_read) (void);
	GIOStatus (*io_write) (void);
	GIOStatus (*io_seek) (void);
	GIOStatus (*io_close) (void);
//	GSource* (*io_create_watch) (void);
	void (*io_free) (void);
	GIOStatus (*io_set_flags) (void);
	GIOFlags (*io_get_flags) (void);
};

GMainLoop _mainloop;
GMainContext _maincontext;

GMainLoop *
g_main_loop_new(GMainContext *context, gboolean is_running)
{
	_mainloop.context = (context == NULL) ? &_maincontext : context;
	_mainloop.is_running = is_running;
	return &_mainloop;
}

void
g_main_loop_run(GMainLoop *loop)
{
	loop->is_running = TRUE;
	while (loop->is_running)
	    sleep(1);
}

void
g_main_loop_quit(GMainLoop *loop)
{
	loop->is_running = FALSE;
}

GIOChannel _channel;

GIOChannel *
g_io_channel_unix_new(int fd)
{
	_channel.fd = fd;
	return &_channel;
}

gint
g_io_channel_unix_get_fd(GIOChannel *channel)
{
	return channel->fd;
}

guint
g_io_add_watch(GIOChannel *channel, GIOCondition condition, GIOFunc func, gpointer user_data)
{
	return 0;
}

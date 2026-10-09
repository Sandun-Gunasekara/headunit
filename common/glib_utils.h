#pragma once

#include <glib.h>
#include <functional>

extern GMainContext* run_on_thread_main_context;

void run_on_main_thread(std::function<bool()>&& f);
void run_on_main_thread_delay(guint milliseconds, std::function<bool()>&& f);

// Quits loop from any thread. The quit is queued on the main thread, so it works even if the
// loop has not started running yet (g_main_loop_quit() before g_main_loop_run() is lost).
void quit_main_loop_async(GMainLoop* loop);

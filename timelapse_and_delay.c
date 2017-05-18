/*
 * Copyright 2015 Luis de Bethencourt <luis.bg@samsung.com>
 * Copyright 2017 Victor Jaquez <vjaquez@igalia.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gst/gst.h>

#include "gst-play-kb.h"

typedef struct
{
  GstElement *pipeline;
  GstElement *overlay;
  GMainLoop *loop;
  guint index;

  gchar *logfname;
  GIOChannel *channel;
  gchar *last_line;
  guint ioid;
  guint io2id;

  gboolean hwaccel;
  gchar *device;
  gint delay;
} App;

static gboolean try_open_channel (gpointer user_data);

static void
restore_terminal (void)
{
  gst_play_kb_set_key_handler (NULL, NULL);
}

static void
keyboard_cb (const gchar * key_input, gpointer user_data)
{
  App *app = user_data;
  gchar key = '\0';

  /* only want to switch/case on single char, not first char of string */
  if (key_input[0] != '\0' && key_input[1] == '\0')
    key = g_ascii_tolower (key_input[0]);

  switch (key) {
    case 'q':
      gst_element_send_event (app->pipeline, gst_event_new_eos ());
      break;
    default:
      break;
  }
}

static void
io_channel_free (App * app)
{
  if (app->ioid != 0)
    g_source_remove (app->ioid);
  app->ioid = 0;
  if (app->io2id != 0)
    g_source_remove (app->io2id);
  app->io2id = 0;
  if (app->channel)
    g_io_channel_unref (app->channel);
  app->channel = NULL;
  g_clear_pointer (&app->last_line, g_free);
}

static gboolean
read_last_line (GIOChannel * channel, GIOCondition condition, gpointer user_data)
{
  GError *error = NULL;
  App *app = user_data;
  GIOStatus status;
  gchar *tmp, *pos, *str = NULL;

  g_assert (condition == G_IO_IN);

  do {
    status = g_io_channel_read_to_end (channel, &str, NULL, &error);
    if (error) {
      g_printerr ("Error reading IRC log: %s\n", error->message);
      g_error_free (error);
      /* TODO: close, remove and try_open_channel() again */
    }
  } while (status == G_IO_STATUS_AGAIN);

  if (!str || str[0] == '\0')
    goto bail;

  tmp = app->last_line;
  if (tmp) {
    app->last_line = g_strconcat (tmp, str, NULL);
    g_free (tmp);
  } else {
    app->last_line = g_strdup (str);
  }

  while (TRUE) {
    pos = strchr (app->last_line, '\n');
    if (!pos)
      goto bail;

    *pos = '\0';
    if (app->overlay)
      g_object_set (app->overlay, "text", app->last_line, NULL);
    tmp = g_strdup (pos + 1);
    g_free (app->last_line);
    app->last_line = tmp;
  }

bail:
  g_free (str);
  return G_SOURCE_CONTINUE;
}

static gboolean
restore_channel (GIOChannel * channel, GIOCondition condition, gpointer user_data)
{
  App *app = user_data;

  io_channel_free (app);
  g_timeout_add_seconds (1, try_open_channel, &app);
  return G_SOURCE_REMOVE;
}

static gboolean
try_open_channel (gpointer user_data)
{
  App *app = user_data;
  GIOChannel *channel;
  GError *error = NULL;

  if (app->channel)
    return G_SOURCE_REMOVE;

  channel = g_io_channel_new_file (app->logfname, "r", NULL);
  if (!channel)
    return G_SOURCE_CONTINUE;

  g_io_channel_seek_position (channel, 0, G_SEEK_END, &error);
  if (error) {
    g_printerr ("Error seeking IRC log: %s\n", error->message);
    g_error_free (error);
    g_io_channel_unref (channel);
    return G_SOURCE_CONTINUE;
  }

  app->ioid = g_io_add_watch (channel, G_IO_IN, read_last_line, app);
  app->io2id = g_io_add_watch (channel, G_IO_ERR | G_IO_HUP | G_IO_NVAL,
      restore_channel, app);
  app->channel = channel;
  return G_SOURCE_REMOVE;
}

static inline void
print_error (GstObject * src, GError * error, gchar * debug)
{
  gst_object_default_error (src, error, debug);
  if (error)
    g_error_free (error);
  g_free (debug);
}

static gboolean
bus_msg (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  App *app = user_data;
  GError *error = NULL;
  gchar *debug = NULL;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error (msg, &error, &debug);
      print_error (GST_MESSAGE_SRC (msg), error, debug);
      g_main_loop_quit (app->loop);
      break;
    case GST_MESSAGE_WARNING:
      gst_message_parse_warning (msg, &error, &debug);
      print_error (GST_MESSAGE_SRC (msg), error, debug);
      break;
    case GST_MESSAGE_EOS:
      g_main_loop_quit (app->loop);
      break;
    default:
      break;
  }

  return TRUE;
}

static void
build_pipeline_sw (GstBin * bin, GstElement * src, GstElement * q0,
    GstElement * overlay, GstElement * rate, GstElement * fsink)
{
  GstElement *conv, *tee, *vsink, *q1, *enc;

  /*
     v4l2src ! videoconvert ! tee name=t ! \
       queue max-size-buffers=0 max-size-time=0 max-size-bytes=0 min-threshold-time=3000000000 ! \
       textoverlay ! xvimagesink sync=false \
       t. ! queue ! videorate max-rate=1 ! jpegenc ! multifilesink
  */

  conv = gst_element_factory_make ("videoconvert", NULL);
  g_assert (conv);
  tee = gst_element_factory_make ("tee", NULL);
  g_assert (tee);

  /* render bin */
  vsink = gst_element_factory_make ("xvimagesink", NULL);
  g_assert (vsink);

  /* jpeg bin */
  q1 = gst_element_factory_make ("queue", NULL);
  g_assert (q1);
  enc = gst_element_factory_make ("jpegenc", NULL);
  g_assert (enc);

  gst_bin_add_many (bin, src, conv, tee, q0, overlay, vsink, q1, rate, enc,
      fsink, NULL);

  g_assert (gst_element_link_many (src, conv, tee, NULL));
  g_assert (gst_element_link_many (tee, q0, overlay, vsink, NULL));
  g_assert (gst_element_link_many (tee, q1, rate, enc, fsink, NULL));

  g_object_set (vsink, "sync", FALSE, NULL);
}

static void
build_pipeline_hw_accel (GstBin * bin, GstElement * src, GstElement * q0,
    GstElement * overlay, GstElement * rate, GstElement * fsink)
{
  GstElement *caps, *tee, *dec, *vsink, *q1;

  /*
     v4l2src ! image/jpeg ! tee name=t ! \
       queue max-size-buffers=0 max-size-time=0 max-size-bytes=0 min-threshold-time=3000000000 ! \
       vaapijpegdec ! textoverlay ! vaapisink sync=false \
       t. ! queue ! videorate max-rate=1 ! multifilesink
  */

  caps = gst_element_factory_make ("capsfilter", NULL);
  g_assert (caps);
  tee = gst_element_factory_make ("tee", NULL);
  g_assert (tee);

  /* render bin */
  dec = gst_element_factory_make ("vaapijpegdec", NULL);
  g_assert (dec);
  vsink = gst_element_factory_make ("vaapisink", NULL);
  g_assert (vsink);

  /* jpeg bin */
  q1 = gst_element_factory_make ("queue", NULL);
  g_assert (q1);

  gst_bin_add_many (bin, src, caps, tee, q0, dec, overlay, vsink, q1, rate,
      fsink, NULL);

  g_assert (gst_element_link_many (src, caps, tee, NULL));
  g_assert (gst_element_link_many (tee, q0, dec, overlay, vsink, NULL));
  g_assert (gst_element_link_many (tee, q1, rate, fsink, NULL));

  g_object_set (vsink, "sync", FALSE, "fullscreen", TRUE, NULL);
  g_object_set (caps, "caps", gst_caps_from_string ("image/jpeg, height=720"), NULL);
}

static void
build_pipeline (App * app)
{
  GstElement *src, *queue, *overlay, *rate, *fsink;

  src = gst_element_factory_make ("v4l2src", NULL);
  g_assert (src);
  queue = gst_element_factory_make ("queue", NULL);
  g_assert (queue);
  overlay = gst_element_factory_make ("textoverlay", NULL);
  g_assert (overlay);
  rate = gst_element_factory_make ("videorate", NULL);
  g_assert (rate);
  fsink = gst_element_factory_make ("multifilesink", NULL);
  g_assert (fsink);

  if (app->hwaccel)
    build_pipeline_hw_accel (GST_BIN (app->pipeline), src, queue, overlay, rate,
        fsink);
  else
    build_pipeline_sw (GST_BIN (app->pipeline), src, queue, overlay, rate,
        fsink);

  if (app->device)
    g_object_set (src, "device", app->device, NULL);

  g_object_set (queue, "max-size-time", 0, "max-size-bytes", 0,
      "max-size-buffers", 0, "min-threshold-time", app->delay * GST_SECOND, NULL);

  g_object_set (overlay, "font-desc", "Arial Bold 22", "text", "starting", NULL);
  g_object_set (rate, "max-rate", 1, NULL);
  g_object_set (fsink, "location", "images/frame%032d.jpg", "index",
      app->index, NULL);

  app->overlay = overlay;
}

static gboolean
setup_images_dir (App * app)
{
  GError *error = NULL;
  GDir *dir;
  const gchar *fname;
  gchar *str, *end;
  gdouble num, index = 0;

  if (g_mkdir_with_parents ("images", 0775) != 0) {
    g_printerr ("Cannot create directory for images: %s\n", g_strerror (errno));
    return FALSE;
  }

  dir = g_dir_open ("images", 0, &error);
  if (error) {
    g_printerr ("Cannot open directory for images: %s\n", error->message);
    g_error_free (error);
    return FALSE;
  }

  while ((fname = g_dir_read_name (dir))) {
    if (!(g_str_has_prefix (fname, "frame")
          && g_str_has_suffix (fname, ".jpg")))
      continue;
    str = g_strdup (fname + 5);       /* offset "frame" */
    end = g_strstr_len (str, -1, ".jpg");
    if (end) {
      *end = '\0';
      errno = 0;
      num = strtol (str, &end, 10);
      if (errno == 0 && *end == '\0')
        index = MAX (index, num);
    }
    g_free (str);
  }

  g_dir_close (dir);

  app->index = index + 1;

  return TRUE;
}

int
main (int argc, char ** argv)
{
  App app;
  GstBus *bus;
  guint busid;
  gboolean hwaccel = FALSE;
  gint delay = 3;
  gchar *device = NULL;
  gchar **logfiles = NULL;
  GError *error = NULL;
  GOptionContext *ctx;
  GOptionEntry options[] = {
    {"hw-accel", 'a', 0, G_OPTION_ARG_NONE, &hwaccel,
        "Use hardware accelearted pipeline (VAAPI)", NULL},
    {"device", 'd', 0, G_OPTION_ARG_FILENAME, &device,
        "V4L2 source device", NULL},
    {"delay", 'w', 0, G_OPTION_ARG_INT, &delay,
        "Video sink delay (seconds)", NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &logfiles, NULL,
        NULL},
    {NULL, 0, 0, 0, NULL, NULL, NULL}
  };

  setlocale (LC_ALL, "");

  ctx = g_option_context_new ("logfile");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
    g_print ("Error initializing: %s\n", error->message);
    g_option_context_free (ctx);
    g_clear_error (&error);
    exit (-1);
  }
  g_option_context_free (ctx);

  /* images directory */
  if (!setup_images_dir (&app))
    exit (-1);

  /* gstreamer pipeline */
  app.pipeline = gst_pipeline_new ("pipeline");
  g_assert (app.pipeline);
  app.hwaccel = hwaccel;
  app.device = device;
  if (delay <= 0)
    app.delay = 3;
  else
    app.delay = delay;
  build_pipeline (&app);

  bus = gst_element_get_bus (app.pipeline);
  busid = gst_bus_add_watch (bus, bus_msg, &app);
  gst_object_unref (bus);

  /* irc log */
  app.ioid = 0;
  app.last_line = NULL;
  app.channel = NULL;
  app.logfname = logfiles ? logfiles[0] : NULL;
  if (app.logfname)
    g_timeout_add_seconds (1, try_open_channel, &app);

  app.loop = g_main_loop_new (NULL, FALSE);

  if (gst_play_kb_set_key_handler (keyboard_cb, &app)) {
    g_print ("Press 'q' to quit…\n");
    atexit (restore_terminal);
  }

  gst_element_set_state (app.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (app.loop);
  gst_element_set_state (app.pipeline, GST_STATE_NULL);

  io_channel_free (&app);

  g_source_remove (busid);

  g_main_loop_unref (app.loop);
  gst_object_unref (app.pipeline);

  g_free (device);
  g_strfreev (logfiles);

  return 0;
}

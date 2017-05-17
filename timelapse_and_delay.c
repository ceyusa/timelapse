/*
 * Copyright 2015 Luis de Bethencourt <luis.bg@samsung.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <gst/gst.h>

typedef struct
{
  GstElement *pipeline;
  GstElement *overlay;
  GMainLoop *loop;
  gchar *logfname;
} App;

static void
event_loop (App * app)
{
  FILE *fp;
  gchar text[BUFSIZ];
  GstBus *bus;
  GstMessage *message = NULL;

  bus = gst_element_get_bus (GST_ELEMENT (app->pipeline));

  while (TRUE) {
    message = gst_bus_pop (bus);
    if (message != NULL) {
      switch (message->type) {
        case GST_MESSAGE_EOS:
          gst_message_unref (message);
          return;
        case GST_MESSAGE_ERROR:{
          GError *gerror;
          gchar *debug;

          gst_message_parse_error (message, &gerror, &debug);
          gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
          gst_message_unref (message);
          g_error_free (gerror);
          g_free (debug);
          return;
        }
        case GST_MESSAGE_WARNING:{
          GError *gerror;
          gchar *debug;

          gst_message_parse_warning (message, &gerror, &debug);
          gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
          gst_message_unref (message);
          g_error_free (gerror);
          g_free (debug);
          break;
        }
        default:
          gst_message_unref (message);
          break;
      }
    }

    fp = fopen (app->logfname, "r");
    if (fp) {
      fgets (text, BUFSIZ - 1, fp);
      fclose(fp);

      g_object_set (app->overlay, "text", text, NULL);
    }

    g_usleep (100000);
  }
}

static void
build_pipeline (App * app)
{
  GstElement *src, *conv, *tee, *q0, *overlay, *vsink, *q1, *rate, *enc, *fsink;

  /*
     v4l2src ! videoconvert ! tee name=t ! \
       queue max-size-buffers=0 max-size-time=0 max-size-bytes=0 min-threshold-time=3000000000 ! \
       textoverlay ! xvimagesink sync=false \
       t. ! queue ! videorate max-rate=1 ! jpegenc ! fakesink
  */

  src = gst_element_factory_make ("v4l2src", NULL);
  g_assert (src);
  conv = gst_element_factory_make ("videoconvert", NULL);
  g_assert (conv);
  tee = gst_element_factory_make ("tee", NULL);
  g_assert (tee);

  /* render bin */
  q0 = gst_element_factory_make ("queue", NULL);
  g_assert (q0);
  overlay = gst_element_factory_make ("textoverlay", "overlay");
  g_assert (overlay);
  vsink = gst_element_factory_make ("xvimagesink", NULL);
  g_assert (vsink);

  /* jpeg bin */
  q1 = gst_element_factory_make ("queue", NULL);
  g_assert (q1);
  rate = gst_element_factory_make ("videorate", NULL);
  g_assert (rate);
  enc = gst_element_factory_make ("jpegenc", NULL);
  g_assert (enc);
  fsink = gst_element_factory_make ("multifilesink", NULL);
  g_assert (fsink);

  gst_bin_add_many (GST_BIN (app->pipeline), src, conv, tee, q0, overlay,
      vsink, q1, rate, enc, fsink, NULL);

  g_assert (gst_element_link_many (src, conv, tee, NULL));
  g_assert (gst_element_link_many (tee, q0, overlay, vsink, NULL));
  g_assert (gst_element_link_many (tee, q1, rate, enc, fsink, NULL));

  g_object_set (q0, "max-size-time", 0, "max-size-bytes", 0,
      "max-size-buffers", 0, "min-threshold-time", 3 * GST_SECOND, NULL);
  g_object_set (overlay, "text", "starting", NULL);
  g_object_set (vsink, "sync", FALSE, NULL);
  g_object_set (rate, "max-rate", 1, NULL);
  g_object_set (fsink, "location", "img/frame%032d.jpg", NULL);

  app->overlay = overlay;
}

int
main (int argc, char ** argv)
{
  App app;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s irc_log\n", argv[0]);
    exit (-1);
  }

  app.logfname = argv[1];
  app.pipeline = gst_pipeline_new ("pipeline");
  g_assert (app.pipeline);

  build_pipeline (&app);

  gst_element_set_state (app.pipeline, GST_STATE_PLAYING);
  event_loop (&app);
  gst_element_set_state (app.pipeline, GST_STATE_NULL);

  gst_object_unref (app.pipeline);

  return 0;
}

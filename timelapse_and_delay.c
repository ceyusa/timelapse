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


static void
event_loop (GstElement * pipe, gchar * file_location, GstElement * overlay)
{
  FILE *fp;
  gchar *text;
  GstBus *bus;
  GstMessage *message = NULL;

  text = (gchar *) malloc (512 * sizeof (gchar));
  bus = gst_element_get_bus (GST_ELEMENT (pipe));

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

    fp = fopen (file_location, "r");
    if (!fp)
      g_print ("unable to open file %s", file_location);
    else {
      fgets (text, 256, fp);
      fclose(fp);

      g_object_set (overlay, "text", text, NULL);
    }

    g_usleep (100000);
  }
}

int
main (int argc, char *argv[])
{
  GstElement *bin, *videosrc, *conv, *enc, *q0, *tee;
  GstElement *q1, *dec, *overlay, *videosink;
  GstElement *q2, *rate, *filesink;
  // GstBin *src, *delay, *timelapse;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: ./timelapse_and_delay tweets_file\n");
    exit (-1);
  }

  g_print ("Constructing pipeline\n");

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new ("pipeline");
  g_assert (bin);

  /* Create the jpegs */
  // src = (GstBin *) gst_bin_new (NULL);
  // g_assert (src);
  videosrc = gst_element_factory_make ("v4l2src", "webcam");
  conv = gst_element_factory_make ("videoconvert", "conv0");
  enc = gst_element_factory_make ("jpegenc", "enc");
  q0 = gst_element_factory_make ("queue", "q0");
  tee = gst_element_factory_make ("tee", "split");
  g_assert (videosrc);
  g_assert (conv);
  g_assert (enc);
  g_assert (q0);
  g_assert (tee);

  /* Delay */
  q1 = gst_element_factory_make ("queue", "q1");
  dec = gst_element_factory_make ("jpegdec", "dec");
  overlay = gst_element_factory_make ("textoverlay", "overlay");
  videosink = gst_element_factory_make ("xvimagesink", "videosink");
  g_assert (q1);
  g_assert (dec);
  g_assert (videosink);

  /* Time lapse */
  q2 = gst_element_factory_make ("queue", "q2");
  rate = gst_element_factory_make ("videorate", "rate");
  filesink = gst_element_factory_make ("multifilesink", "filesink");
  g_assert (q2);
  g_assert (rate);
  g_assert (filesink);

  /* add objects to the main pipeline */
  gst_bin_add_many (GST_BIN (bin), videosrc, conv, enc, q0, tee, q1, dec,
                    overlay, videosink, q2, rate, filesink, NULL);

  /* set all properties */
  g_object_set (q0, "max-size-time", 0,
                "max-size-bytes", 0,
                "max-size-buffers", 0,
                "min-threshold-time", 3000000000,NULL);
  g_object_set (overlay, "text", "starting", NULL);
  g_object_set (videosink, "sync", 0, NULL);
  g_object_set (rate, "max-rate", 1, NULL);
  g_object_set (filesink, "location", "img/frame%032d.jpg", NULL);

  /* link the elements. */
  gst_element_link_many (videosrc, conv, enc, q0, tee, NULL);
  gst_element_link_many (tee, q1, dec, overlay, videosink, NULL);
  gst_element_link_many (tee, q2, rate, filesink, NULL);

  g_print ("Start rolling\n");

  /* start playing */
  gst_element_set_state (bin, GST_STATE_PLAYING);

  /* Run event loop listening for bus messages until EOS or ERROR */
  event_loop (bin, argv[1], overlay);

  g_print ("Finished - stopping pipeline\n");

  /* stop the bin */
  gst_element_set_state (bin, GST_STATE_NULL);

  /* Unreffing the bin will clean up all its children too */
  gst_object_unref (bin);

  return 0;
}

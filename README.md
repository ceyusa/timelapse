# timelapse

This repository contains a set of tools used to create time-lapse videos.

## timelapse_and_delay

Small GStreamer-based utility which grabs a frame per second from a
V4L2 camera, encodes them in JPEG, and it stores them in the directory
"images" (relative to the directory where you run the command).

Also, thes program launches a video render on screen which shows the
video stream with some delay (3 seconds by default).

### Usage:

  timelapse_and_delay [OPTION...] logfile

  -a, --hw-accel                    Use hardware accelearted pipeline (VAAPI)
  -d, --device                      V4L2 source device
  -w, --delay                       Video sink delay (seconds)

There are two types of pipelines: one using only software-based
element and another using V4L2/VAAPI. By default uses the
software-based pipeline.

`logfile` is a text file, monitored by the program, and if a new line
is appended in it, the program will show that text line as a text
overlay on the video renderer. But they *are not* composed in the
stored JPEG images.

## irc_channel_logger.py

It is a python script that register an IRC bot in the GStreamer
channel and logs out all the messages in the channel to a file named
`irc_messages`. This file can be used as `logfile` in
`timelapse_and_delay`.

## timelapse_generator

Missing utility to transform a directory of JPEG images into a video.


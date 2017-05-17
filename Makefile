CFLAGS := -O0 -ggdb -Wall -Wextra -Wno-unused-parameter
override CFLAGS += -Wmissing-prototypes -ansi -std=gnu99 -D_GNU_SOURCE

GST_CFLAGS := $(shell pkg-config --cflags gstreamer-1.0)
GST_LIBS := $(shell pkg-config --libs gstreamer-1.0)

SOURCES = timelapse_and_delay.c
OBJECTS = $(patsubst %.c, %.o, $(SOURCES))

all:

timelapse_and_delay: $(OBJECTS)
timelapse_and_delay: override CFLAGS += $(GST_CFLAGS)
timelapse_and_delay: override LDFLAGS += $(GST_LIBS)
targets += timelapse_and_delay

all: $(targets)

%.o:: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -o $@ -c $<

clean:
	rm -f $(OBJECTS) $(targets) *.d

-include *.d

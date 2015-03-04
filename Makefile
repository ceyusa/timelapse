LDFLAGS += `pkg-config --libs glib-2.0 gstreamer-1.0`
CFLAGS += `pkg-config --cflags glib-2.0 gstreamer-1.0`

SOURCES = $(wildcard *.c)
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

all: timelapse_and_delay

player: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS)

%.o: %.c
	$(CC) -c $< $(CFLAGS) -o $@

clean:
	rm -f $(wildcard *.o) timelapse_and_delay


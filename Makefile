CC := gcc
PA_FLAGS := $$(pkg-config --cflags --libs alsa jack portaudio-2.0)

ersatz-jjy: ersatz-jjy.c
	$(CC) --std=c11 -Wall -Werror $(PA_FLAGS) -lm ersatz-jjy.c -o ersatz-jjy

all: ersatz-jjy

clean:
	rm -rf ersatz-jjy

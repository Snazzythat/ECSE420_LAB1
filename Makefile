# CMAKE FILE
CC = gcc
CFLAGS =
LIBS = -lpthread
TARGETS = convolve pool rectify

default: clean $(TARGETS)
all: default

.PHONY: clean

convolve:
	gcc -w -o convolve convolve.c lodepng.c  $(LIBS)

rectify:
	gcc -w -o rectify rectify.c lodepng.c  $(LIBS)

pool:
	gcc -w -o pool pool.c lodepng.c  $(LIBS)

clean:
	-rm -f $(TARGETS)
	-rm -f *.o

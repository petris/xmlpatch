CFLAGS=$(shell pkg-config --cflags libxml-2.0 libxmlpatch) -std=gnu99 -O2 -g -Wall

OBJECTS=main.o

xmlpatch: $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(shell pkg-config --libs libxml-2.0 libxmlpatch)

xmlpatch.static: $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(shell pkg-config --libs --static libxml-2.0 libxmlpatch) -static


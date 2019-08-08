HEADERS = mcachefs.h
OBJECTS = mcachefs.o mcachefs-util.o mcachefs-metadata.o mcachefs-file.o mcachefs-file-ts.o
OBJECTS += mcachefs-vops.o mcachefs-journal.o mcachefs-mutex.o mcachefs-transfer.o mcachefs-cleanup-backing.o
OBJECTS += mcachefs-io.o mcachefs-lowlevel.o mcachefs-hash.o
OBJECTS += mcachefs-config.o
CC = gcc

CFLAGS += -g -O0
#CFLAGS += -DDEBUG -D__MCACHEFS_MUTEX_DEBUG -DPARANOID
# CFLAGS += -O3 -pg -g -fprofile-arcs
# CFLAGS += -O3
CFLAGS += -I. -I. -I/usr/include/fuse -Wall -W -D_FILE_OFFSET_BITS=64
LCFLAGS += -ldl -lpthread -lrt -lfuse

TARGET = mcachefs

all: $(TARGET) $(HEADERS)


$(TARGET): $(OBJECTS) $(HEADERS)
	$(CC) -o $(TARGET) $(CFLAGS) $(OBJECTS) $(LCFLAGS)

clean:
	rm -rf $(TARGET) $(OBJECTS) err lerr gmon.out

.c.o: $(HEADERS) $<
	$(CC) -c $(CFLAGS) $(MODULECOMPILEFLAGS) -o $@ $<

install: all
	cp mcachefs /usr/bin/mcachefs

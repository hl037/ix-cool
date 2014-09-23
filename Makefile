
SHELL = /bin/sh
CC    = gcc

FLAGS        = -pedantic -Wall -Wextra
LDFLAGS      = -lconfig
CFLAGS       =  
DEBUGFLAGS   = -O0 -D _DEBUG
RELEASEFLAGS = -O3 -D NDEBUG -fwhole-program

TARGET  = ix-coold
SOURCES = $(shell echo *.c)
OBJECTS = $(SOURCES:.c=.o)

PREFIX = $(DESTDIR)/usr
BINDIR = $(PREFIX)/bin
SYSTEMD_LIB = usr/lib/systemd

all: $(TARGET)

$(TARGET): $(OBJECTS) 
	$(CC) $(FLAGS) $(CFLAGS) $(DEBUGFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJECTS)

release: $(SOURCES)
	$(CC) $(FLAGS) $(CFLAGS) $(RELEASEFLAGS) $(LDFLAGS) -o $(TARGET) $(SOURCES)

profile: CFLAGS += -pg
profile: $(TARGET)


install: release
	install -D $(TARGET) $(BINDIR)/$(TARGET)
	install -D ix-coold.service $(DESTDIR)/$(SYSTEMD_LIB)/system
	
uninstall:
	-rm $(BINDIR)/$(TARGET)


clean:
	-rm -f $(OBJECTS)
	-rm -f gmon.out

distclean: clean
	-rm -f $(TARGET)


.SECONDEXPANSION:
	$(foreach OBJ,$(OBJECTS),$(eval $(OBJ)_DEPS = $(shell gcc -MM $(OBJ:.o=.c) | sed s/.*://)))
	%.o: %.c $$($$@_DEPS)
	$(CC) $(FLAGS) $(CFLAGS) $(DEBUGFLAGS) -c -o $@ $<

.PHONY : all profile release \
	install install-strip uninstall clean distclean

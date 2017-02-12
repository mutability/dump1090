#
# When building a package or installing otherwise in the system, make
# sure that the variable PREFIX is defined, e.g. make PREFIX=/usr/local
#
PROGNAME=dump1090

ifndef DUMP1090_VERSION
DUMP1090_VERSION=$(shell git describe --always --tags --match=v*)
endif

ifdef PREFIX
BINDIR=$(PREFIX)/bin
SHAREDIR=$(PREFIX)/share/$(PROGNAME)
EXTRACFLAGS=-DHTMLPATH=\"$(SHAREDIR)\"
endif

CPPFLAGS+=-DMODES_DUMP1090_VERSION=\"$(DUMP1090_VERSION)\"
CFLAGS+=-O2 -g -Wall -Werror -W
LIBS=-lpthread -lm
LIBS_RTL=`pkg-config --libs librtlsdr libusb-1.0`
CC=gcc

UNAME := $(shell uname)

ifeq ($(UNAME), Linux)
LIBS+=-lrt
CFLAGS+=-std=c11 -D_DEFAULT_SOURCE
endif
ifeq ($(UNAME), Darwin)
UNAME_R := $(shell uname -r)
ifeq ($(shell expr "$(UNAME_R)" : '1[012345]\.'),3)
CFLAGS+=-std=c11 -DMISSING_GETTIME -DMISSING_NANOSLEEP
COMPAT+=compat/clock_gettime/clock_gettime.o compat/clock_nanosleep/clock_nanosleep.o
else
# Darwin 16 (OS X 10.12) supplies clock_gettime() and clockid_t
CFLAGS+=-std=c11 -DMISSING_NANOSLEEP -DCLOCKID_T
COMPAT+=compat/clock_nanosleep/clock_nanosleep.o
endif
endif

ifeq ($(UNAME), OpenBSD)
CFLAGS+= -DMISSING_NANOSLEEP
COMPAT+= compat/clock_nanosleep/clock_nanosleep.o
endif

all: dump1090 view1090

%.o: %.c *.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EXTRACFLAGS) -c $< -o $@

dump1090.o: CFLAGS += `pkg-config --cflags librtlsdr libusb-1.0`

dump1090: dump1090.o anet.o interactive.o mode_ac.o mode_s.o net_io.o crc.o demod_2400.o stats.o cpr.o icao_filter.o track.o util.o convert.o $(COMPAT)
	$(CC) -g -o $@ $^ $(LIBS) $(LIBS_RTL) $(LDFLAGS)

view1090: view1090.o anet.o interactive.o mode_ac.o mode_s.o net_io.o crc.o stats.o cpr.o icao_filter.o track.o util.o $(COMPAT)
	$(CC) -g -o $@ $^ $(LIBS) $(LDFLAGS)

faup1090: faup1090.o anet.o mode_ac.o mode_s.o net_io.o crc.o stats.o cpr.o icao_filter.o track.o util.o $(COMPAT)
	$(CC) -g -o $@ $^ $(LIBS) $(LDFLAGS)

clean:
	rm -f *.o compat/clock_gettime/*.o compat/clock_nanosleep/*.o dump1090 view1090 faup1090 cprtests crctests

test: cprtests
	./cprtests

cprtests: cpr.o cprtests.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EXTRACFLAGS) -g -o $@ $^ -lm

crctests: crc.c crc.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EXTRACFLAGS) -g -DCRCDEBUG -o $@ $<

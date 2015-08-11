DUMP1090_VERSION ?= v1.14-67-g650e58a
#-$(shell git describe --tags)

INC_RTL ?= -I../../../rtl-sdr/include
LIB_RTL ?= -L../../../rtl-sdr/libs -lrtlsdr

OPT = -O3 -flto
REL = -s

CC     := gcc
CFLAGS += -Wall -Werror -std=c99 -I.
LIBS    = -lws2_32
DUMPVER = -DMODES_DUMP1090_VERSION=\"$(DUMP1090_VERSION)\"

SHARED_O = anet.o mode_ac.o mode_s.o net_io.o crc.o stats.o cpr.o icao_filter.o track.o util.o
DUMP1090_O = dump1090.o interactive.o demod_2000.o demod_2400.o convert.o $(SHARED_O)
VIEW1090_O = view1090.o interactive.o $(SHARED_O)
FAUP1090_O = faup1090.o $(SHARED_O)

.PHONY: clean

all: dump1090 view1090

%.o: ../%.c ../*.h
	$(CC) $(OPT) $(CFLAGS) -c $< -o $@

dump1090.o: CFLAGS += $(DUMPVER) $(INC_RTL)
view1090.o: CFLAGS += $(DUMPVER)
faup1090.o: CFLAGS += $(DUMPVER)
net_io.o:   CFLAGS += $(DUMPVER)

dump1090: $(DUMP1090_O)
	$(CC) $(OPT) $(REL) -o $@ $^ $(LIBS) $(LIB_RTL)

view1090: $(VIEW1090_O)
	$(CC) $(OPT) $(REL) -o $@ $^ $(LIBS)

faup1090: $(FAUP1090_O)
	$(CC) $(OPT) $(REL) -o $@ $^ $(LIBS)

clean:
	@if exist *.exe del /q /f *.exe > nul 2>&1
	@if exist *.o del /q /f *.o > nul 2>&1

test: cprtests
	./cprtests

cprtests: cpr.o cprtests.o
	$(CC) $(OPT) $(REL) $(CFLAGS) -o $@ $^

crctests: crc.c crc.h
	$(CC) $(OPT) $(REL) $(CFLAGS) -DCRCDEBUG -o $@ $<

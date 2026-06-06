CC=m68k-amigaos-gcc
CFLAGS=-noixemul -m68020 -O2 -fomit-frame-pointer -Wall -Wno-pointer-sign
LDFLAGS=-noixemul -s
TARGET=AmigaAMP-VUMeter
OBJS=AmigaAMP-VUMeter.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS) -lamiga

AmigaAMP-VUMeter.o: AmigaAMP-VUMeter.c TrackInfo.h
	$(CC) $(CFLAGS) -c AmigaAMP-VUMeter.c -o AmigaAMP-VUMeter.o

clean:
	rm -f $(OBJS) $(TARGET)

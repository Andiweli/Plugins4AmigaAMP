CC = m68k-amigaos-gcc
CFLAGS = -O3 -m68020-60 -fomit-frame-pointer -fno-common -noixemul
LDFLAGS = -noixemul
TARGET = AmigaAMP-Isometric
OBJS = AmigaAMP-Isometric.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

AmigaAMP-Isometric.o: AmigaAMP-Isometric.c TrackInfo.h
	$(CC) $(CFLAGS) -c AmigaAMP-Isometric.c -o AmigaAMP-Isometric.o

clean:
	rm -f $(OBJS) $(TARGET)

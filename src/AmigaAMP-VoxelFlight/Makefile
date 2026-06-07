CC=m68k-amigaos-gcc
CFLAGS=-noixemul -m68020 -O2 -fomit-frame-pointer -Wall -Wno-pointer-sign
LDFLAGS=-noixemul -s
TARGET=AmigaAMP-VoxelFlight
OBJS=AmigaAMP-VoxelFlight.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS) -lamiga

AmigaAMP-VoxelFlight.o: AmigaAMP-VoxelFlight.c TrackInfo.h
	$(CC) $(CFLAGS) -c AmigaAMP-VoxelFlight.c -o AmigaAMP-VoxelFlight.o

clean:
	rm -f $(OBJS) $(TARGET)

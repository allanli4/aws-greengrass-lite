CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
INCLUDES = -I../../../build/_deps/ggl_sdk-src/include
LIBS = -L../../../build -lggl-sdk -lpthread
TARGET = system-monitor
SOURCE = main.c

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET) $(SOURCE) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: clean
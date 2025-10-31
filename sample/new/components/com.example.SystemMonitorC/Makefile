CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
INCLUDES = -I../../aws-greengrass-sdk-lite/include
LIBS = -L../../aws-greengrass-sdk-lite/build -lggl-sdk -lpthread
TARGET = system-monitor
SOURCE = main.c

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET) $(SOURCE) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: clean
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
INCLUDES = -I/home/ubuntu/aws-greengrass-lite/build/_deps/ggl_sdk-src/include
LIBS = -L/home/ubuntu/aws-greengrass-lite/build/ggl-sdk -lggl-sdk -lpthread
TARGET = device-agent
SOURCE = main.c

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET) $(SOURCE) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: clean
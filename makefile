CC = g++
CFLAGS = -g3

TARGET1 = oss
TARGET2 = user_proc

all: $(TARGET1) $(TARGET2)

oss: oss.o
	$(CC) $(CFLAGS) oss.o -o oss

user_proc: user_proc.o
	$(CC) $(CFLAGS) user_proc.o -o user_proc

oss.o: oss.cpp
	$(CC) $(CFLAGS) -c oss.cpp

user_proc.o: user_proc.cpp
	$(CC) $(CFLAGS) -c user_proc.cpp

clean:
	rm -f *.o oss user_proc

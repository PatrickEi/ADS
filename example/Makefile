
CC = g++
LIB_NAME = AdsLib-$(shell uname).a

all: example.bin

example.bin: $(LIB_NAME)
	$(CC) example.cpp $< -std=c++11 -lpthread -o $@
	
test: example.bin
	./$<

clean:
	rm -f *.o *.bin


VPATH = AdsLib
LIBS = -lpthread
CC = g++
LIB_NAME = AdsLib-$(shell uname).a

ifeq ($(shell uname),Darwin)
	CC = clang
	LIBS += -lc++
endif

all: AdsLibTest.bin

.cpp.o:
	$(CC) -Wall -pedantic -c -g -std=c++11 $< -o $@ -I AdsLib/

$(LIB_NAME): AdsDef.o AdsLib.o AmsConnection.o AmsPort.o AmsRouter.o Log.o NotificationDispatcher.o Sockets.o Frame.o
	ar rvs $@ $?

AdsLibTest.bin: $(LIB_NAME)
	$(CC) AdsLibTest/main.cpp $< -I AdsLib/ -I ../ -std=c++11 $(LIBS) -o $@
	
test: AdsLibTest.bin
	./$<

release: $(LIB_NAME) AdsLib.h AdsDef.h
	cp $? example/
	cp $? ../TwinSAFE_App/AdsLib/

clean:
	rm -f *.a *.o *.bin

uncrustify:
	uncrustify --no-backup -c tools/uncrustify.cfg AdsLib*/*.h AdsLib*/*.cpp

prepare-hooks:
	rm -f .git/hooks/pre-commit
	ln -Fv tools/pre-commit.uncrustify .git/hooks/pre-commit
	chmod a+x .git/hooks/pre-commit

.PHONY: clean uncrustify prepare-hooks

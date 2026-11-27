# Project: gxde-lsg
# Makefile created by Red Panda C++ 2.26

CPP      = g++
CC       = gcc
OBJ      = container.o utils.o dbuswire.o dbusbridge.o main.o
LINKOBJ  = container.o utils.o dbuswire.o dbusbridge.o main.o
CLEANOBJ  =  container.o utils.o dbuswire.o dbusbridge.o main.o gxde-lsg
LIBS     =  -static
INCS     =  
CXXINCS  =  
BIN      = gxde-lsg
CXXFLAGS = $(CXXINCS)  -g3 -pipe -Wall -Wextra -D__DEBUG__
CFLAGS   = $(INCS)  -g3 -pipe -Wall -Wextra -D__DEBUG__
RM       = rm -rf

.PHONY: all all-before all-after clean clean-custom

all: all-before $(BIN) all-after

clean: clean-custom
	-$(RM) $(CLEANOBJ) > /dev/null 2>&1

$(BIN): $(OBJ)
	$(CPP) $(LINKOBJ) -o $(BIN) $(LIBS)

container.o: container.cpp utils.h container.h
	$(CPP) -c container.cpp -o container.o $(CXXFLAGS) 

utils.o: utils.cpp utils.h container.h
	$(CPP) -c utils.cpp -o utils.o $(CXXFLAGS) 

dbuswire.o: dbuswire.cpp dbuswire.h
	$(CPP) -c dbuswire.cpp -o dbuswire.o $(CXXFLAGS) 

dbusbridge.o: dbusbridge.cpp dbusbridge.h dbuswire.h
	$(CPP) -c dbusbridge.cpp -o dbusbridge.o $(CXXFLAGS) 

main.o: main.cpp utils.h container.h dbusbridge.h
	$(CPP) -c main.cpp -o main.o $(CXXFLAGS) 

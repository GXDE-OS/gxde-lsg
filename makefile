# Project: gxde-lsg
# Makefile created by Red Panda C++ 2.26

CPP      = g++
CC       = gcc
OBJ      = utils.o container.o main.o
LINKOBJ  = utils.o container.o main.o
CLEANOBJ  =  utils.o container.o main.o gxde-lsg
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

utils.o: utils.cpp utils.h
	$(CPP) -c utils.cpp -o utils.o $(CXXFLAGS) 

container.o: container.cpp container.h utils.h
	$(CPP) -c container.cpp -o container.o $(CXXFLAGS) 

main.o: main.cpp container.h utils.h
	$(CPP) -c main.cpp -o main.o $(CXXFLAGS) 

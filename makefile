# Project: gxde-lsg
# Makefile created by Red Panda C++ 2.26

CPP      = g++
CC       = gcc
OBJ      = main.o container.o utils.o
LINKOBJ  = main.o container.o utils.o
CLEANOBJ  =  main.o container.o utils.o gxde-lsg
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

main.o: main.cpp container.h utils.h
	$(CPP) -c main.cpp -o main.o $(CXXFLAGS) 

container.o: container.cpp container.h utils.h
	$(CPP) -c container.cpp -o container.o $(CXXFLAGS) 

utils.o: utils.cpp container.h utils.h
	$(CPP) -c utils.cpp -o utils.o $(CXXFLAGS) 

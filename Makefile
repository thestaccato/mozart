CXX      := g++
PKGS     := ncursesw libvlc
CXXFLAGS := -std=c++20 -O2 -flto -Wall -Wextra $(shell pkg-config --cflags $(PKGS))
LIBS     := $(shell pkg-config --libs $(PKGS))
OBJS     := main.o player.o

all: mozart

mozart: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

main.o: main.cpp player.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

player.o: player.cpp player.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f mozart *.o

.PHONY: all clean

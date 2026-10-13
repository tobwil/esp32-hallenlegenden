# Simulator für den Mac: make -C sim && sim/hlsim
CXX ?= clang++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -I../game $(shell sdl2-config --cflags)
LDLIBS = $(shell sdl2-config --libs)
SRC = main.cpp $(wildcard ../game/*.cpp)

hlsim: $(SRC) $(wildcard ../game/*.h)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDLIBS)

clean:
	rm -f hlsim

# Host build of the HUD logic unit tests (uses the native compiler, not the m3-toolchain)
CXX = g++
CXXFLAGS = -std=c++11 -Wall -g

hud_logic_test: hud_logic_test.cpp ../hud_logic.cpp ../hud_logic.h
	$(CXX) $(CXXFLAGS) -o $@ hud_logic_test.cpp ../hud_logic.cpp

test: hud_logic_test
	./hud_logic_test

clean:
	rm -f hud_logic_test

.DEFAULT_GOAL := test
.PHONY: test clean

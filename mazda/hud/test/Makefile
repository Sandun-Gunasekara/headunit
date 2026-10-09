# Host build of the HUD logic unit tests (uses the native compiler, not the m3-toolchain)
CXX = g++
CXXFLAGS = -std=c++11 -Wall -g

hud_logic_test: hud_logic_test.cpp ../hud_logic.cpp ../hud_logic.h
	$(CXX) $(CXXFLAGS) -o $@ hud_logic_test.cpp ../hud_logic.cpp

# Scripted drive through the real HUD thread into tools/hud-sim (needs dbus-c++ and the generated dbus proxies)
hud_replay: hud_replay.cpp ../hud.cpp ../hud_logic.cpp ../hud.h ../hud_logic.h ../../dbus/generated_cmu.h
	$(CXX) $(CXXFLAGS) -pthread -I../../../hu -I../../../common -o $@ hud_replay.cpp ../hud.cpp ../hud_logic.cpp \
		$(shell pkg-config --cflags --libs dbus-c++-1)

../../dbus/generated_cmu.h: ../../dbus/cmu_interfaces.xml
	cd ../../dbus && dbusxx-xml2cpp cmu_interfaces.xml --proxy=generated_cmu.h

test: hud_logic_test
	./hud_logic_test

clean:
	rm -f hud_logic_test hud_replay

.DEFAULT_GOAL := test
.PHONY: test clean

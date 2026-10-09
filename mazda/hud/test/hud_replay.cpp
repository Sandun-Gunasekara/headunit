// Plays a scripted drive through the real HUD thread (mazda/hud/hud.cpp) into the HUD simulator
// (tools/hud-sim), so turns and distance countdowns can be checked without driving.
//   make -C mazda/hud/test hud_replay && ./mazda/hud/test/hud_replay [seconds per update]
#include <stdio.h>
#include <stdlib.h>
#include <condition_variable>
#include <thread>
#include <dbus-c++/dbus.h>

#include <stdarg.h>
#include "../hud.h"

// Stand-in for hu_log() from hu/hu_uti.cpp, which would pull in the whole AA stack
int hu_log(int prio, const char* tag, const char* func, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("  [hud] %s: ", func);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    return 0;
}

// AA enum values (hu.proto)
enum { SIDE_LEFT = 1, SIDE_RIGHT = 2, SIDE_UNSPECIFIED = 3 };
enum { EV_DEPART = 1, EV_TURN = 4, EV_ROUNDABOUT_ENTER_AND_EXIT = 13, EV_DESTINATION = 19 };
enum { UNIT_METERS = 1, UNIT_KILOMETERS = 3 };

static int step_ms = 1000;

static void turn(const char* name, int side, int event, int number = 0, int angle = 0)
{
    printf("turn: %s\n", name);
    std::lock_guard<std::mutex> lock(hudmutex);
    navi_apply_turn(navi_data, name, side, event, number, angle);
}

// Counts down like AA does, one distance message per update
static void drive(int from_meters, int to_meters, int step_meters)
{
    for (int m = from_meters; m >= to_meters; m -= step_meters) {
        {
            std::lock_guard<std::mutex> lock(hudmutex);
            if (m >= 1000)
                navi_apply_distance(navi_data, m, m / 10, (uint64_t)(m / 100) * 100, UNIT_KILOMETERS);
            else
                navi_apply_distance(navi_data, m, m / 10, (uint64_t)m * 1000, UNIT_METERS);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    }
}

int main(int argc, char* argv[])
{
    if (argc > 1)
        step_ms = (int)(atof(argv[1]) * 1000);

    DBus::_init_threading();
    DBus::BusDispatcher dispatcher;
    DBus::default_dispatcher = &dispatcher;

    hud_start();
    if (!hud_installed()) {
        fprintf(stderr, "HUD simulator not reachable, start tools/hud-sim/hud_sim.py first\n");
        return 1;
    }

    std::condition_variable quitcv;
    std::mutex quitmutex;
    std::thread hud_thread([&quitcv, &quitmutex](){ hud_thread_func(quitcv, quitmutex); });

    turn("Head south on Galwarusa Rd", SIDE_UNSPECIFIED, EV_DEPART);
    drive(300, 200, 50);
    turn("Turn left onto Horaketiya Rd", SIDE_LEFT, EV_TURN);
    drive(200, 0, 50);
    turn("Turn right onto Super Terrace", SIDE_RIGHT, EV_TURN);
    drive(1500, 900, 300);
    drive(800, 0, 200);
    turn("At the roundabout, take the 2nd exit", SIDE_RIGHT, EV_ROUNDABOUT_ENTER_AND_EXIT, 2, 90);
    drive(400, 0, 100);
    turn("Destination will be on the right", SIDE_RIGHT, EV_DESTINATION);
    drive(100, 0, 50);
    printf("navigation stopped\n");
    {
        std::lock_guard<std::mutex> lock(hudmutex);
        navi_apply_stop(navi_data);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    quitcv.notify_all();
    hud_thread.join();
    return 0;
}

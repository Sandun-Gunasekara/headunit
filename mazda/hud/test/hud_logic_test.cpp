// Unit tests for the dbus-free HUD logic. Build and run on a desktop machine:
//   make -C mazda/hud/test
#include <stdio.h>
#include "../hud_logic.h"

static int failures = 0;

#define EXPECT_EQ(actual, expected) do { \
    long long a_ = (long long)(actual), e_ = (long long)(expected); \
    if (a_ != e_) { \
        printf("FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #actual, a_, e_); \
        failures++; \
    } \
} while (0)

// AA enum values
enum { SIDE_UNSET = 0, SIDE_LEFT = 1, SIDE_RIGHT = 2, SIDE_UNSPECIFIED = 3 };
enum { EV_UNKNOWN = 0, EV_DEPART = 1, EV_TURN = 4, EV_ROUNDABOUT_ENTER = 11, EV_ROUNDABOUT_EXIT = 12,
       EV_ROUNDABOUT_ENTER_AND_EXIT = 13, EV_DESTINATION = 19 };
enum { UNIT_UNSET = 0, UNIT_METERS = 1, UNIT_KILOMETERS10 = 2, UNIT_KILOMETERS = 3, UNIT_MILES = 5, UNIT_FEET = 6 };

static void test_turn_icons()
{
    // Captured from Android Auto 17.7: "Head south", turn_side 3, turn_event 1 (depart)
    EXPECT_EQ(hud_turn_icon(EV_DEPART, SIDE_UNSPECIFIED, 0), NaviTurns::FLAG);
    EXPECT_EQ(hud_turn_icon(EV_TURN, SIDE_LEFT, 0), NaviTurns::LEFT);
    EXPECT_EQ(hud_turn_icon(EV_TURN, SIDE_RIGHT, 0), NaviTurns::RIGHT);
    EXPECT_EQ(hud_turn_icon(EV_DESTINATION, SIDE_RIGHT, 0), NaviTurns::DESTINATION_RIGHT);

    // Used to index turns[event][-1] (out of bounds) after navigation stopped
    EXPECT_EQ(hud_turn_icon(EV_UNKNOWN, SIDE_UNSET, -1), 0);
    EXPECT_EQ(hud_turn_icon(EV_TURN, SIDE_UNSET, 0), 0);
    // Turn events/sides newer than the table must not read out of bounds
    EXPECT_EQ(hud_turn_icon(20, SIDE_LEFT, 0), 0);
    EXPECT_EQ(hud_turn_icon(1000, SIDE_LEFT, 0), 0);
    EXPECT_EQ(hud_turn_icon(-1, SIDE_LEFT, 0), 0);
    EXPECT_EQ(hud_turn_icon(EV_DEPART, 7, 0), NaviTurns::FLAG);
}

static void test_roundabouts()
{
    // Unchanged behaviour for enter-and-exit
    EXPECT_EQ(hud_turn_icon(EV_ROUNDABOUT_ENTER_AND_EXIT, SIDE_RIGHT, 90), 37 + 3);
    EXPECT_EQ(hud_turn_icon(EV_ROUNDABOUT_ENTER_AND_EXIT, SIDE_LEFT, 90), 49 + 3);
    // 345+ degrees is the same exit as 0, it used to overflow into the other side's icons
    EXPECT_EQ(hud_turn_icon(EV_ROUNDABOUT_ENTER_AND_EXIT, SIDE_RIGHT, 350), 37);
    EXPECT_EQ(hud_turn_icon(EV_ROUNDABOUT_ENTER_AND_EXIT, SIDE_LEFT, 359), 49);
    // Enter/exit get an icon when AA says which exit, otherwise none
    EXPECT_EQ(hud_turn_icon(EV_ROUNDABOUT_ENTER, SIDE_RIGHT, 180), 37 + 6);
    EXPECT_EQ(hud_turn_icon(EV_ROUNDABOUT_EXIT, SIDE_LEFT, 270), 49 + 9);
    EXPECT_EQ(hud_turn_icon(EV_ROUNDABOUT_ENTER, SIDE_RIGHT, 0), 0);
}

static void test_distance()
{
    int32_t d;
    HudDistanceUnit u;
    hud_convert_distance(450, 450000, UNIT_METERS, d, u);
    EXPECT_EQ(d, 4500); EXPECT_EQ(u, HudDistanceUnit::METERS);
    hud_convert_distance(2340, 2300, UNIT_KILOMETERS, d, u);   // 2.3 km
    EXPECT_EQ(d, 23); EXPECT_EQ(u, HudDistanceUnit::KILOMETERS);
    hud_convert_distance(184000, 184000, UNIT_KILOMETERS10, d, u);
    EXPECT_EQ(d, 1840); EXPECT_EQ(u, HudDistanceUnit::KILOMETERS);
    hud_convert_distance(800, 500, UNIT_MILES, d, u);
    EXPECT_EQ(d, 5); EXPECT_EQ(u, HudDistanceUnit::MILES);
    hud_convert_distance(150, 500000, UNIT_FEET, d, u);
    EXPECT_EQ(d, 5000); EXPECT_EQ(u, HudDistanceUnit::FEET);
    // No display unit: fall back to the raw meters
    hud_convert_distance(347, 0, UNIT_UNSET, d, u);
    EXPECT_EQ(d, 3500); EXPECT_EQ(u, HudDistanceUnit::METERS);
    hud_convert_distance(12345, 0, UNIT_UNSET, d, u);
    EXPECT_EQ(d, 123); EXPECT_EQ(u, HudDistanceUnit::KILOMETERS);
    // Captured from Android Auto 17.7 while stationary
    hud_convert_distance(0, 0, UNIT_METERS, d, u);
    EXPECT_EQ(d, 0); EXPECT_EQ(u, HudDistanceUnit::METERS);
    // The HUD field is 16 bit
    hud_convert_distance(0, 100000000, UNIT_FEET, d, u);
    EXPECT_EQ(d, 65535);
}

static void test_navi_updates()
{
    NaviData data;
    EXPECT_EQ(data.changed, false);

    navi_apply_turn(data, "Head south", SIDE_UNSPECIFIED, EV_DEPART, 0, 0);
    EXPECT_EQ(data.changed, true);
    data.changed = false;

    // AA resends identical turn messages every second; they must not trigger a HUD update
    navi_apply_turn(data, "Head south", SIDE_UNSPECIFIED, EV_DEPART, 0, 0);
    EXPECT_EQ(data.changed, false);

    navi_apply_distance(data, 120, 30, 120000, UNIT_METERS);
    EXPECT_EQ(data.changed, true);
    EXPECT_EQ(data.distance, 1200);
    data.changed = false;
    navi_apply_distance(data, 120, 30, 120000, UNIT_METERS);
    EXPECT_EQ(data.changed, false);

    navi_apply_turn(data, "Turn left onto Galwarusa Rd", SIDE_LEFT, EV_TURN, 0, 0);
    EXPECT_EQ(data.changed, true);
    EXPECT_EQ(hud_turn_icon(data.turn_event, data.turn_side, data.turn_angle), NaviTurns::LEFT);
    data.changed = false;

    navi_apply_stop(data);
    EXPECT_EQ(data.changed, true);
    EXPECT_EQ(hud_turn_icon(data.turn_event, data.turn_side, data.turn_angle), 0);
    EXPECT_EQ(data.distance, 0);
    EXPECT_EQ(data.event_name.size(), 0);
}

static void test_msg_id()
{
    // The HUD drops a message whose id equals the previous one, so consecutive sends must always differ
    uint8_t id = 0;
    for (int i = 0; i < 50; i++) {
        uint8_t next = navi_next_msg_id(id);
        if (next == id || next < 1 || next > 7) {
            printf("FAIL msg id %u -> %u\n", id, next);
            failures++;
        }
        id = next;
    }
    EXPECT_EQ(navi_next_msg_id(0), 1);
    EXPECT_EQ(navi_next_msg_id(7), 1);
}

int main()
{
    test_turn_icons();
    test_roundabouts();
    test_distance();
    test_navi_updates();
    test_msg_id();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("All HUD logic tests passed\n");
    return 0;
}

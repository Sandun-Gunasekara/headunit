#ifndef MZD_HUD_LOGIC_H
#define MZD_HUD_LOGIC_H

// Pure HUD logic (no dbus), so it can be unit tested on a desktop machine.

#include <stdint.h>
#include <string>

enum HudDistanceUnit: uint8_t {
    METERS = 1,
    MILES = 2,
    KILOMETERS = 3,
    YARDS = 4,
    FEET = 5
};

struct NaviData {
  std::string event_name;
  int32_t turn_side = 0;
  int32_t turn_event = 0;
  int32_t turn_number = -1;
  int32_t turn_angle = -1;
  int32_t distance = 0; // distance * 10, encoded like that to store one digit after decimal dot in int type
  HudDistanceUnit distance_unit = HudDistanceUnit::METERS;
  int32_t time_until = 0;
  uint8_t previous_msg = 0; // message counter, the HUD ignores a message with the same counter as the last one
  bool changed = false;
};

enum NaviTurns: uint32_t {
  STRAIGHT = 1,
  LEFT = 2,
  RIGHT = 3,
  SLIGHT_LEFT = 4,
  SLIGHT_RIGHT = 5,
  DESTINATION  = 8,
  DESTINATION_LEFT = 33,
  DESTINATION_RIGHT = 34,
  SHARP_LEFT = 11,
  SHARP_RIGHT = 9,
  U_TURN_LEFT = 13,
  U_TURN_RIGHT = 10,
  FLAG = 12,
  FLAG_LEFT = 35,
  FLAG_RIGHT = 36,
  FORK_LEFT = 15,
  FORK_RIGHT = 14,
  MERGE_LEFT = 16,
  MERGE_RIGHT = 17,
  OFF_RAMP_LEFT = 7,
  OFF_RAMP_RIGHT = 30
};

// HUD direction icon for an AA turn (NAVTurnMessage values). 0 means no icon.
uint32_t hud_turn_icon(int32_t turn_event, int32_t turn_side, int32_t turn_angle);

// Converts an AA distance (NAVDistanceMessage values, display_distance_unit 0 when not set)
// to the HUD encoding (distance * 10, clamped to the HUD's 16 bit field).
void hud_convert_distance(int32_t distance_meters, uint64_t display_distance, int32_t display_distance_unit,
                          int32_t& hud_distance, HudDistanceUnit& hud_unit);

// Update navi_data from AA navigation messages. They only mark it changed, the HUD thread sends it.
void navi_apply_turn(NaviData& data, const std::string& event_name, int32_t turn_side, int32_t turn_event,
                     int32_t turn_number, int32_t turn_angle);
void navi_apply_distance(NaviData& data, int32_t distance_meters, int32_t time_until, uint64_t display_distance,
                         int32_t display_distance_unit);
void navi_apply_stop(NaviData& data);

// Next HUD message counter, cycles 1..7.
uint8_t navi_next_msg_id(uint8_t previous_msg);

#endif

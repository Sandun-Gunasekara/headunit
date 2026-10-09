#include "hud_logic.h"

// AA NAVTurnMessage::TURN_EVENT values used below
enum {
  TURN_ROUNDABOUT_ENTER = 11,
  TURN_ROUNDABOUT_EXIT = 12,
  TURN_ROUNDABOUT_ENTER_AND_EXIT = 13,
};

// AA NAVDistanceMessage::DISPLAY_DISTANCE_UNIT values
enum {
  UNIT_METERS = 1,
  UNIT_KILOMETERS10 = 2,
  UNIT_KILOMETERS = 3,
  UNIT_MILES10 = 4,
  UNIT_MILES = 5,
  UNIT_FEET = 6,
};

// Indexed by AA turn event, then by side: left, right, unspecified
static const uint8_t turns[][3] = {
  {0,0,0}, //TURN_UNKNOWN
  {NaviTurns::FLAG_LEFT,NaviTurns::FLAG_RIGHT,NaviTurns::FLAG}, //TURN_DEPART
  {NaviTurns::STRAIGHT,NaviTurns::STRAIGHT,NaviTurns::STRAIGHT}, //TURN_NAME_CHANGE
  {NaviTurns::SLIGHT_LEFT,NaviTurns::SLIGHT_RIGHT,NaviTurns::STRAIGHT}, //TURN_SLIGHT_TURN
  {NaviTurns::LEFT,NaviTurns::RIGHT,0}, //TURN_TURN
  {NaviTurns::SHARP_LEFT,NaviTurns::SHARP_RIGHT,0}, //TURN_SHARP_TURN
  {NaviTurns::U_TURN_LEFT, NaviTurns::U_TURN_RIGHT,0}, //TURN_U_TURN
  {NaviTurns::LEFT,NaviTurns::RIGHT,NaviTurns::STRAIGHT}, //TURN_ON_RAMP
  {NaviTurns::OFF_RAMP_LEFT,NaviTurns::OFF_RAMP_RIGHT,NaviTurns::STRAIGHT}, //TURN_OFF_RAMP
  {NaviTurns::FORK_LEFT, NaviTurns::FORK_RIGHT, 0}, //TURN_FORK
  {NaviTurns::MERGE_LEFT, NaviTurns::MERGE_RIGHT, 0}, //TURN_MERGE
  {0,0,0},  //TURN_ROUNDABOUT_ENTER (handled by roundabout())
  {0,0,0}, // TURN_ROUNDABOUT_EXIT (handled by roundabout())
  {0,0,0}, //TURN_ROUNDABOUT_ENTER_AND_EXIT (handled by roundabout())
  {NaviTurns::STRAIGHT,NaviTurns::STRAIGHT,NaviTurns::STRAIGHT}, //TURN_STRAIGHT
  {0,0,0}, //unused?
  {0,0,0}, //TURN_FERRY_BOAT
  {0,0,0}, //TURN_FERRY_TRAIN
  {0,0,0}, //unused??
  {NaviTurns::DESTINATION_LEFT, NaviTurns::DESTINATION_RIGHT, NaviTurns::DESTINATION} //TURN_DESTINATION
};

static const int32_t turn_count = sizeof(turns) / sizeof(turns[0]);

// The HUD has 12 roundabout exit icons (30 degrees apart) for each driving side
static uint32_t roundabout(int32_t degrees, int32_t turn_side){
  if (degrees < 0) {
    degrees = 0;
  }
  uint32_t nearest = ((degrees + 15) / 30) % 12; // 345..360 degrees is the same exit as 0
  uint32_t offset = turn_side == 1 ? 49 : 37; // turn_side 1 is TURN_LEFT
  return(nearest + offset);
}

uint32_t hud_turn_icon(int32_t turn_event, int32_t turn_side, int32_t turn_angle)
{
  if (turn_event == TURN_ROUNDABOUT_ENTER_AND_EXIT) {
    return roundabout(turn_angle, turn_side);
  }
  if (turn_event == TURN_ROUNDABOUT_ENTER || turn_event == TURN_ROUNDABOUT_EXIT) {
    // Only show an exit icon when AA tells us which exit
    return turn_angle > 0 ? roundabout(turn_angle, turn_side) : 0;
  }
  if (turn_event < 0 || turn_event >= turn_count) {
    return 0;
  }
  //Google starts at 1 (TURN_LEFT), anything else (unset, unspecified) uses the generic icon
  int32_t side_index = (turn_side == 1 || turn_side == 2) ? turn_side - 1 : 2;
  return turns[turn_event][side_index];
}

void hud_convert_distance(int32_t distance_meters, uint64_t display_distance, int32_t display_distance_unit,
                          int32_t& hud_distance, HudDistanceUnit& hud_unit)
{
  // display_distance is the value shown on the phone * 1000, the HUD wants it * 10
  int64_t now_distance;
  switch (display_distance_unit) {
      case UNIT_METERS:
        now_distance = display_distance / 100;
        hud_unit = HudDistanceUnit::METERS;
        break;
      case UNIT_KILOMETERS10:
      case UNIT_KILOMETERS:
        now_distance = display_distance / 100;
        hud_unit = HudDistanceUnit::KILOMETERS;
        break;
      case UNIT_MILES10:
      case UNIT_MILES:
        now_distance = display_distance / 100;
        hud_unit = HudDistanceUnit::MILES;
        break;
      case UNIT_FEET:
        now_distance = display_distance / 100;
        hud_unit = HudDistanceUnit::FEET;
        break;
      default: //not sure, use SI
        if (distance_meters > 1000) {
            now_distance = distance_meters / 100;
            hud_unit = HudDistanceUnit::KILOMETERS;
        } else {
            now_distance = (((distance_meters + 5) / 10) * 10) * 10;
            hud_unit = HudDistanceUnit::METERS;
        }
  }
  if (now_distance < 0) {
    now_distance = 0;
  } else if (now_distance > UINT16_MAX) {
    now_distance = UINT16_MAX;
  }
  hud_distance = (int32_t)now_distance;
}

void navi_apply_turn(NaviData& data, const std::string& event_name, int32_t turn_side, int32_t turn_event,
                     int32_t turn_number, int32_t turn_angle)
{
  if (data.event_name != event_name ||
      data.turn_event != turn_event ||
      data.turn_side != turn_side ||
      data.turn_number != turn_number ||
      data.turn_angle != turn_angle) {
    data.event_name = event_name;
    data.turn_event = turn_event;
    data.turn_side = turn_side;
    data.turn_number = turn_number;
    data.turn_angle = turn_angle;
    data.changed = true;
  }
}

void navi_apply_distance(NaviData& data, int32_t distance_meters, int32_t time_until, uint64_t display_distance,
                         int32_t display_distance_unit)
{
  int32_t now_distance;
  HudDistanceUnit now_unit;
  hud_convert_distance(distance_meters, display_distance, display_distance_unit, now_distance, now_unit);

  if (now_distance != data.distance || now_unit != data.distance_unit) {
    data.distance = now_distance;
    data.distance_unit = now_unit;
    data.changed = true;
  }
  if (data.time_until != time_until) {
    data.time_until = time_until;
    data.changed = true;
  }
}

void navi_apply_stop(NaviData& data)
{
  data.event_name = "";
  data.turn_event = 0;
  data.turn_side = 0;
  data.turn_number = -1;
  data.turn_angle = -1;
  data.distance = 0;
  data.time_until = 0;
  data.changed = true;
}

uint8_t navi_next_msg_id(uint8_t previous_msg)
{
  return previous_msg >= 7 ? 1 : previous_msg + 1;
}

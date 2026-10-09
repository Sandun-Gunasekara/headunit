#include "hud.h"

#include <dbus/dbus.h>
#include <dbus-c++/dbus.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <functional>
#include <condition_variable>
#include <signal.h>
#include <thread>

#include "../dbus/generated_cmu.h"

#define LOGTAG "mazda-hud"

#include "hu_uti.h"

#define SERVICE_BUS_ADDRESS "unix:path=/tmp/dbus_service_socket"
#define HMI_BUS_ADDRESS "unix:path=/tmp/dbus_hmi_socket"

std::mutex hudmutex;

static HUDSettingsClient *hud_client = NULL;
static NaviClient *vbsnavi_client = NULL;
static TMCClient *tmc_client = NULL;

NaviData navi_data;

// Not every CMU firmware has SetHUD_Display_Msg2 (56.00.511 answers UnknownMethod). The arrow and
// distance from SetHUDDisplayMsgReq work without it, so stop calling it rather than treating every
// update as failed.
static bool msg2_supported = true;

bool hud_send_raw(uint32_t icon, uint16_t distance, uint8_t unit, uint8_t counter, const std::string& text)
{
  ::DBus::Struct< uint32_t, uint16_t, uint8_t, uint16_t, uint8_t, uint8_t > hudDisplayMsg;
  hudDisplayMsg._1 = icon;
  hudDisplayMsg._2 = distance;
  hudDisplayMsg._3 = unit;
  hudDisplayMsg._4 = 0; //Speed limit (Not Used)
  hudDisplayMsg._5 = 0; //Speed limit units (Not used)
  hudDisplayMsg._6 = counter;

  // Log what the HUD is told whenever it changes (not for counter-only resends)
  static uint32_t last_icon = 0xFFFFFFFF;
  static uint16_t last_distance = 0;
  static uint8_t last_unit = 0;
  static std::string last_text;
  if (icon != last_icon || distance != last_distance || unit != last_unit || text != last_text)
  {
    logw("HUD: icon %u distance %u unit %u counter %u '%s'", icon, distance, unit, counter, text.c_str());
    last_icon = icon;
    last_distance = distance;
    last_unit = unit;
    last_text = text;
  }

  try
  {
    vbsnavi_client->SetHUDDisplayMsgReq(hudDisplayMsg);
  }
  catch(DBus::Error& error)
  {
    loge("DBUS: hud_send failed %s: %s\n", error.name(), error.message());
    return false;
  }

  if (msg2_supported)
  {
    ::DBus::Struct< std::string, uint8_t > guidancePointData;
    guidancePointData._1 = text;
    guidancePointData._2 = counter;
    try
    {
      tmc_client->SetHUD_Display_Msg2(guidancePointData);
    }
    catch(DBus::Error& error)
    {
      if (strcmp(error.name(), "org.freedesktop.DBus.Error.UnknownMethod") == 0)
      {
        logw("HUD: this CMU has no SetHUD_Display_Msg2, street names won't be shown on the HUD");
        msg2_supported = false;
      }
      else
      {
        // Only the street name was lost, the arrow and distance were delivered
        loge("DBUS: SetHUD_Display_Msg2 failed %s: %s\n", error.name(), error.message());
      }
    }
  }
  return true;
}

static bool hud_send(const NaviData& data)
{
  return hud_send_raw(hud_turn_icon(data.turn_event, data.turn_side, data.turn_angle), data.distance,
                      data.distance_unit, data.previous_msg, data.event_name);
}

void hud_thread_func(QuitSignal& quit){
  // Keep running for the whole AA session: a failed dbus call or the HUD service not being
  // ready yet must not stop HUD updates until the phone reconnects.
  bool was_installed = true;
  bool showing_guidance = false;
  while (true)
  {
    if (quit.wait_for(std::chrono::milliseconds(1000)))
    {
      break;
    }

    bool installed = hud_installed();
    if (installed != was_installed)
    {
      logw("HUD %s", installed ? "available" : "not available, waiting for it");
      was_installed = installed;
    }
    if (!installed)
    {
      continue;
    }

    NaviData snapshot;
    {
      std::lock_guard<std::mutex> lock(hudmutex);
      // Only send changes: the HUD keeps showing the last message until it gets a new one
      if (!navi_data.changed)
      {
        continue;
      }
      navi_data.previous_msg = navi_next_msg_id(navi_data.previous_msg);
      navi_data.changed = false;
      snapshot = navi_data;
    }

    // Send outside the lock so a slow dbus call never blocks the AA thread
    if (hud_send(snapshot))
    {
      showing_guidance = snapshot.turn_event != 0 || snapshot.distance != 0;
    }
    else
    {
      // Try again on the next tick
      std::lock_guard<std::mutex> lock(hudmutex);
      navi_data.changed = true;
    }
  }

  // Phone disconnected mid route: clear the HUD instead of leaving the last arrow on it
  if (showing_guidance && hud_installed())
  {
    NaviData snapshot;
    {
      std::lock_guard<std::mutex> lock(hudmutex);
      navi_apply_stop(navi_data);
      navi_data.previous_msg = navi_next_msg_id(navi_data.previous_msg);
      navi_data.changed = false;
      snapshot = navi_data;
    }
    hud_send(snapshot);
  }
}

void hud_start()
{
  if (hud_client != NULL)
    return;
    
  try
  {
    DBus::Connection service_bus(SERVICE_BUS_ADDRESS, false);
    service_bus.register_bus();
    DBus::Connection hmiBus(HMI_BUS_ADDRESS, false);
    hmiBus.register_bus();
    hud_client = new HUDSettingsClient(hmiBus, "/com/jci/navi2IHU", "com.jci.navi2IHU");
    vbsnavi_client = new NaviClient(service_bus, "/com/jci/vbs/navi", "com.jci.vbs.navi");
    tmc_client = new TMCClient(service_bus, "/com/jci/vbs/navi", "com.jci.vbs.navi");
  }
  catch(DBus::Error& error)
  {
    loge("DBUS: Failed to connect to SERVICE bus %s: %s\n", error.name(), error.message());
    hud_stop();
    return;
  }
  //logv("HUD dbus connections established\n");
  return;
}

void hud_stop()
{
  delete hud_client;
  hud_client = nullptr;

  delete vbsnavi_client;
  vbsnavi_client = nullptr;

  delete tmc_client;
  tmc_client = nullptr;
}

bool hud_installed()
{
  if (hud_client == NULL)
      return(false);

  try
  {
      return(hud_client->GetHUDIsInstalled());
  }
  catch(DBus::Error& error)
  {
      loge("DBUS: GetHUDIsInstalled failed %s: %s\n", error.name(), error.message());
      return(false);
  }
}

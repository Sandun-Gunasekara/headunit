#include "hud.h"

#include <dbus/dbus.h>
#include <dbus-c++/dbus.h>
#include <stdint.h>
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

static bool hud_send(const NaviData& data)
{
  ::DBus::Struct< uint32_t, uint16_t, uint8_t, uint16_t, uint8_t, uint8_t > hudDisplayMsg;
  hudDisplayMsg._1 = hud_turn_icon(data.turn_event, data.turn_side, data.turn_angle);
  hudDisplayMsg._2 = data.distance;
  hudDisplayMsg._3 = data.distance_unit;
  hudDisplayMsg._4 = 0; //Speed limit (Not Used)
  hudDisplayMsg._5 = 0; //Speed limit units (Not used)
  hudDisplayMsg._6 = data.previous_msg;

  ::DBus::Struct< std::string, uint8_t > guidancePointData;
  guidancePointData._1 = data.event_name;
  guidancePointData._2 = data.previous_msg;

  logd("HUD send: icon %u distance %u unit %u msg %u '%s'", hudDisplayMsg._1, hudDisplayMsg._2,
       hudDisplayMsg._3, hudDisplayMsg._6, data.event_name.c_str());
  try
  {
    vbsnavi_client->SetHUDDisplayMsgReq(hudDisplayMsg);
    tmc_client->SetHUD_Display_Msg2(guidancePointData);
  }
  catch(DBus::Error& error)
  {
    loge("DBUS: hud_send failed %s: %s\n", error.name(), error.message());
    return false;
  }
  return true;
}

void hud_thread_func(std::condition_variable& quitcv, std::mutex& quitmutex){
  // Keep running for the whole AA session: a failed dbus call or the HUD service not being
  // ready yet must not stop HUD updates until the phone reconnects.
  bool was_installed = true;
  bool showing_guidance = false;
  while (true)
  {
    {
        std::unique_lock<std::mutex> lk(quitmutex);
        if (quitcv.wait_for(lk, std::chrono::milliseconds(1000)) == std::cv_status::no_timeout)
        {
            break;
        }
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

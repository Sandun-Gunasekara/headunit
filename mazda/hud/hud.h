#ifndef MZD_HUD_H
#define MZD_HUD_H

#include <stdint.h>
#include <string>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <dbus/dbus.h>
#include <dbus-c++/dbus.h>

#include "../dbus/generated_cmu.h"

#include "hud_logic.h"
#include "quit_signal.h"

void hud_start();
void hud_stop();
bool hud_installed();
void hud_thread_func(QuitSignal& quit);
// Sends one message straight to the HUD (used by the HUD test mode). Returns false if it failed.
bool hud_send_raw(uint32_t icon, uint16_t distance, uint8_t unit, uint8_t counter, const std::string& text);

// Navigation state shared between the AA thread (writer) and the HUD thread (reader), guarded by hudmutex
extern NaviData navi_data;
extern std::mutex hudmutex;

class HUDSettingsClient : public com::jci::navi2IHU::HUDSettings_proxy,
                     public DBus::ObjectProxy
{
public:
    HUDSettingsClient(DBus::Connection &connection, const char *path, const char *name)
        : DBus::ObjectProxy(connection, path, name)
    {
    }

    virtual void HUDInstalledChanged(const bool& hUDInstalled) override {}
    virtual void SetHUDSettingFailed(const int32_t& hUDSettingType, const int32_t& err) override {}
    virtual void HUDControlAllowed(const bool& bAllowed) override {}
    virtual void HUDSettingChanged(const int32_t& hUDSettingType, const int32_t& value) override {}
};

class NaviClient : public com::jci::vbs::navi_proxy,
                     public DBus::ObjectProxy
{
public:
    NaviClient(DBus::Connection &connection, const char *path, const char *name)
        : DBus::ObjectProxy(connection, path, name)
    {
    }

    virtual void FuelTypeResp(const uint8_t& fuelType) override {}
    virtual void HUDResp(const uint8_t& hudStatus) override {}
    virtual void TSRResp(const uint8_t& tsrStatus) override {}
    virtual void GccConfigMgmtResp(const ::DBus::Struct< std::vector< uint8_t > >& vin_Character) override {}
    virtual void TSRFeatureMode(const uint8_t& tsrMode) override {}
};

  class TMCClient : public com::jci::vbs::navi::tmc_proxy,
                       public DBus::ObjectProxy
  {
  public:
      TMCClient(DBus::Connection &connection, const char *path, const char *name)
          : DBus::ObjectProxy(connection, path, name)
      {
      }

      virtual void ServiceListResponse(const ::DBus::Struct< uint8_t, std::vector< uint8_t >, std::vector< uint8_t >, std::vector< uint8_t >, std::vector< uint8_t >, std::vector< uint8_t > >& providerList) override {}
      virtual void ResponseToTMCSelection(const uint8_t& rdstmcOperation, const uint8_t& tmcSearchMode, const uint8_t& countryCode, const uint8_t& locationTableNumber, const uint8_t& serviceIdentifier, const uint8_t& quality, const uint8_t& receptionStatus) override {}
};


#endif

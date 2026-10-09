#define LOGTAG "hu_usb"
#include "hu_uti.h"  // Utilities
#include "hu_usb.h"
#include <vector>
#include <algorithm>
#include <chrono>
#include <string.h>
#include <sys/select.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>


#include <libusb.h>

#ifndef LIBUSB_LOG_LEVEL_NONE
#define LIBUSB_LOG_LEVEL_NONE     0
#endif
#ifndef LIBUSB_LOG_LEVEL_ERROR
#define LIBUSB_LOG_LEVEL_ERROR    1
#endif
#ifndef LIBUSB_LOG_LEVEL_WARNING
#define LIBUSB_LOG_LEVEL_WARNING  2
#endif
#ifndef LIBUSB_LOG_LEVEL_INFO
#define LIBUSB_LOG_LEVEL_INFO     3
#endif
#ifndef LIBUSB_LOG_LEVEL_DEBUG
#define LIBUSB_LOG_LEVEL_DEBUG    4
#endif

static unsigned char AAP_VAL_MAN[] =  "Android";
static unsigned char AAP_VAL_MOD[] =  "Android Auto";    // "Android Open Automotive Protocol"
static unsigned char AAP_VAL_DESC[] =  "Android Auto";
static unsigned char AAP_VAL_VER[] =  "2.0.1";
static unsigned char AAP_VAL_URI[] =  "https://github.com/gartnera/headunit";
static unsigned char AAP_VAL_SERIAL[] =  "HU-AAAAAA001";

#define ACC_IDX_MAN    0   // Manufacturer
#define ACC_IDX_MOD    1   // Model
#define ACC_IDX_DESC   2  // Model
#define ACC_IDX_VER    3   // Model
#define ACC_IDX_URI    4   // Model
#define ACC_IDX_SERIAL 5   // Model

#define ACC_REQ_GET_PROTOCOL        51
#define ACC_REQ_SEND_STRING         52
#define ACC_REQ_START               53

#define VEN_ID_GOOGLE           0x18D1
#define DEV_ID_OAP              0x2D00
#define DEV_ID_OAP_WITH_ADB     0x2D01

#define USB_DIR_IN              0x80
#define USB_DIR_OUT             0x00
#define USB_TYPE_VENDOR         0x40

struct usbvpid {
  uint16_t vendor;
  uint16_t product;
};

const char * iusb_error_get (int error) {
  #if CMU
   switch (error)
      {
      case LIBUSB_SUCCESS:
              return "Success";
      case LIBUSB_ERROR_IO:
              return "Input/output error";
      case LIBUSB_ERROR_INVALID_PARAM:
              return "Invalid parameter";
      case LIBUSB_ERROR_ACCESS:
              return "Access denied (insufficient permissions)";
      case LIBUSB_ERROR_NO_DEVICE:
              return "No such device (it may have been disconnected)";
      case LIBUSB_ERROR_NOT_FOUND:
              return "Entity not found";
      case LIBUSB_ERROR_BUSY:
              return "Resource busy";
      case LIBUSB_ERROR_TIMEOUT:
              return "Operation timed out";
      case LIBUSB_ERROR_OVERFLOW:
              return "Overflow";
      case LIBUSB_ERROR_PIPE:
              return "Pipe error";
      case LIBUSB_ERROR_INTERRUPTED:
              return "System call interrupted (perhaps due to signal)";
      case LIBUSB_ERROR_NO_MEM:
              return "Insufficient memory";
      case LIBUSB_ERROR_NOT_SUPPORTED:
              return "Operation not supported or unimplemented on this platform";
      case LIBUSB_ERROR_OTHER:
              return "Other error";
      }
      return "Unknown error";
  #else
  return libusb_strerror((libusb_error)error);
  #endif
}

int HUTransportStreamUSB::Write(const byte * buf, int len, int tmo) {

  byte* copy_buf = (byte*)malloc(len);
  memcpy(copy_buf, buf, len);

  libusb_transfer *transfer = libusb_alloc_transfer(0);
  libusb_fill_bulk_transfer(transfer, iusb_dev_hndl, iusb_ep_out,
    copy_buf, len, &libusb_callback_send_tramp, this, 0);

  int iusb_state = libusb_submit_transfer(transfer);
  if (iusb_state < 0)
  {
    loge("  Failed: libusb_submit_transfer: %d (%s)", iusb_state, iusb_error_get (iusb_state));
    libusb_free_transfer(transfer);
    return -1;
  }
  else
  {
    logd(" libusb_submit_transfer for %d bytes", len);
  }
  return len;
}

static int iusb_control_transfer (libusb_device_handle * usb_hndl, uint8_t req_type, uint8_t req_val, uint16_t val, uint16_t idx, byte * buf, uint16_t len, unsigned int tmo) {

  if (ena_log_verbo)
    logd ("Start usb_hndl: %p  req_type: %d  req_val: %d  val: %d  idx: %d  buf: %p  len: %d  tmo: %d", usb_hndl, req_type, req_val, val, idx, buf, len, tmo);

  int usb_err = libusb_control_transfer (usb_hndl, req_type, req_val, val, idx, buf, len, tmo);
  if (usb_err < 0) {
    //this is too spammy while detecting devices
    //loge ("Error usb_err: %d (%s)  usb_hndl: %p  req_type: %d  req_val: %d  val: %d  idx: %d  buf: %p  len: %d  tmo: %d", usb_err, iusb_error_get (usb_err), usb_hndl, req_type, req_val, val, idx, buf, len, tmo);
    return (-1);
  }
  if (ena_log_verbo)
    logd ("Done usb_err: %d  usb_hndl: %p  req_type: %d  req_val: %d  val: %d  idx: %d  buf: %p  len: %d  tmo: %d", usb_err, usb_hndl, req_type, req_val, val, idx, buf, len, tmo);
  return (0);
}

// Watches udev for USB devices being added
class UsbAddMonitor
{
  struct udev* udev_ctx = nullptr;
  struct udev_monitor* mon = nullptr;
public:
  UsbAddMonitor()
  {
    udev_ctx = udev_new();
    if (udev_ctx)
      mon = udev_monitor_new_from_netlink(udev_ctx, "udev");
    if (!mon)
    {
      loge("udev monitor unavailable, falling back to rescanning");
      return;
    }
    if (udev_monitor_filter_add_match_subsystem_devtype(mon, "usb", "usb_device") != 0 ||
        udev_monitor_enable_receiving(mon) != 0)
    {
      loge("udev monitor setup failed, falling back to rescanning");
      udev_monitor_unref(mon);
      mon = nullptr;
    }
  }
  ~UsbAddMonitor()
  {
    if (mon)
      udev_monitor_unref(mon);
    if (udev_ctx)
      udev_unref(udev_ctx);
  }

  // Returns true if a USB device was added within timeout_ms
  bool wait_for_add(int timeout_ms)
  {
    if (!mon)
    {
      ms_sleep(timeout_ms);
      return false;
    }
    int fd = udev_monitor_get_fd(mon);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true)
    {
      long left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
      if (left <= 0)
        return false;

      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(fd, &fds);
      timeval tv;
      tv.tv_sec = left / 1000;
      tv.tv_usec = (left % 1000) * 1000;
      int ret = select(fd + 1, &fds, NULL, NULL, &tv);
      if (ret < 0)
      {
        if (errno == EINTR)
          continue;
        loge("udev select failed errno: %d (%s)", errno, strerror(errno));
        ms_sleep(left);
        return false;
      }
      if (ret == 0)
        return false;

      struct udev_device* dev = udev_monitor_receive_device(mon);
      if (!dev)
        continue;
      const char* action = udev_device_get_action(dev);
      logw("udev device %sed | node:%s", action ? action : "?", udev_device_get_devnode(dev) ? udev_device_get_devnode(dev) : "?");
      bool added = action && strcmp(action, "add") == 0;
      udev_device_unref(dev);
      if (added)
        return true;
    }
  }
};

//based on http://source.android.com/devices/accessories/aoa.html
// Asks a device to switch to Android accessory mode. Returns true if it accepted.
bool HUTransportStreamUSB::switch_to_accessory_mode(libusb_device* device, bool verbose)
{
    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device, &desc) < 0)
    {
        loge("Error getting descriptor");
        return false;
    }
    if (desc.idVendor == VEN_ID_GOOGLE && (desc.idProduct == DEV_ID_OAP || desc.idProduct == DEV_ID_OAP_WITH_ADB))
    {
        // Already in accessory mode (find_oap_device couldn't open it yet), don't restart it
        return false;
    }
    if (verbose)
        logw("Opening device 0x%04x : 0x%04x", desc.idVendor, desc.idProduct);
    libusb_device_handle* handle = nullptr;
    if (libusb_open(device, &handle) < 0)
    {
        if (verbose)
            loge("Error opening device 0x%04x : 0x%04x", desc.idVendor, desc.idProduct);
        return false;
    }

    bool started = false;
    uint16_t oap_proto_ver = 0;
    if (iusb_control_transfer(handle, USB_DIR_IN | USB_TYPE_VENDOR, ACC_REQ_GET_PROTOCOL, 0, 0, (byte*)&oap_proto_ver, sizeof(uint16_t), 1000) >= 0
        && le16toh(oap_proto_ver) >= 1)
    {
        oap_proto_ver = le16toh(oap_proto_ver);
        logw("Device 0x%04x : 0x%04x responded with protocol ver %u", desc.idVendor, desc.idProduct, oap_proto_ver);
        struct { int idx; unsigned char* val; size_t len; const char* name; } strings[] = {
            { ACC_IDX_MAN, AAP_VAL_MAN, sizeof(AAP_VAL_MAN), "ACC_IDX_MAN" },
            { ACC_IDX_MOD, AAP_VAL_MOD, sizeof(AAP_VAL_MOD), "ACC_IDX_MOD" },
            { ACC_IDX_DESC, AAP_VAL_DESC, sizeof(AAP_VAL_DESC), "ACC_IDX_DESC" },
            { ACC_IDX_VER, AAP_VAL_VER, sizeof(AAP_VAL_VER), "ACC_IDX_VER" },
            { ACC_IDX_URI, AAP_VAL_URI, sizeof(AAP_VAL_URI), "ACC_IDX_URI" },
            { ACC_IDX_SERIAL, AAP_VAL_SERIAL, sizeof(AAP_VAL_SERIAL), "ACC_IDX_SERIAL" },
        };
        started = true;
        for (auto& str : strings)
        {
            if (iusb_control_transfer(handle, USB_DIR_OUT | USB_TYPE_VENDOR, ACC_REQ_SEND_STRING, 0, str.idx, str.val, str.len, 1000) < 0)
            {
                loge("Error sending %s to device 0x%04x : 0x%04x", str.name, desc.idVendor, desc.idProduct);
                started = false;
                break;
            }
        }
        if (started && iusb_control_transfer(handle, USB_DIR_OUT | USB_TYPE_VENDOR, ACC_REQ_START, 0, 0, nullptr, 0, 1000) < 0)
        {
            loge("Error sending ACC_REQ_START to device 0x%04x : 0x%04x", desc.idVendor, desc.idProduct);
            started = false;
        }
    }

    libusb_close(handle);
    return started;
}

libusb_device_handle* HUTransportStreamUSB::find_oap_device()
{
    libusb_device_handle* handle = libusb_open_device_with_vid_pid(iusb_ctx, VEN_ID_GOOGLE, DEV_ID_OAP);
    if (!handle)
    {
        //try with ADB
        handle = libusb_open_device_with_vid_pid(iusb_ctx, VEN_ID_GOOGLE, DEV_ID_OAP_WITH_ADB);
    }
    return handle;
}

HUTransportStreamUSB::HUTransportStreamUSB()
{

}

HUTransportStreamUSB::~HUTransportStreamUSB()
{
  if (iusb_state != hu_STATE_STOPPED)
  {
    Stop();
  }
}

int HUTransportStreamUSB::Stop() {
  iusb_state = hu_STATE_STOPPIN;
  logd ("  SET: iusb_state: %d (%s)", iusb_state, state_get (iusb_state));


  close(readfd);
  close(pipe_write_fd);
  readfd = -1;
  pipe_write_fd = -1;

  close(errorfd);
  close(error_write_fd);
  errorfd = -1;
  error_write_fd = -1;

  if (abort_usb_thread_pipe_write_fd >= 0)
  {
    write(abort_usb_thread_pipe_write_fd, &abort_usb_thread_pipe_write_fd, 1);
  }

  if (usb_recv_thread.joinable())
  {
    usb_recv_thread.join();
  }
  close(abort_usb_thread_pipe_write_fd);
  close(abort_usb_thread_pipe_read_fd);
  abort_usb_thread_pipe_write_fd = -1;
  abort_usb_thread_pipe_read_fd = -1;

  iusb_ep_in = -1;
  iusb_ep_out = -1;

  if (iusb_dev_hndl != NULL)
  {
    int usb_err = libusb_release_interface (iusb_dev_hndl, 0);          // Can get a crash inside libusb_release_interface()
    if (usb_err != 0)
      loge ("Done libusb_release_interface usb_err: %d (%s)", usb_err, iusb_error_get (usb_err));
    else
      logd ("Done libusb_release_interface usb_err: %d (%s)", usb_err, iusb_error_get (usb_err));

    libusb_reset_device (iusb_dev_hndl);

    libusb_close (iusb_dev_hndl);
    logd ("Done libusb_close");
    iusb_dev_hndl = NULL;
  }

  if (iusb_ctx)
  {
    libusb_exit (iusb_ctx); // Put here or can get a crash from pulling cable
    iusb_ctx = nullptr;
  }

  iusb_state = hu_STATE_STOPPED;
  logd ("  SET: iusb_state: %d (%s)", iusb_state, state_get (iusb_state));
  return 0;
}


void HUTransportStreamUSB::usb_recv_thread_main()
{
  pthread_setname_np(pthread_self(), "usb_recv_thread_main");

  timeval zero_tv;
  memset(&zero_tv, 0, sizeof(zero_tv));

  while(poll(usb_thread_event_fds.data(), usb_thread_event_fds.size(), -1) >= 0)
  {
    //wakeup, something happened
    if (usb_thread_event_fds[0].revents == usb_thread_event_fds[0].events)
    {
        logw("Requested to exit");
        break;
    }
    int iusb_state = libusb_handle_events_timeout_completed(iusb_ctx, &zero_tv, nullptr);
    if (iusb_state)
    {
      break;
    }
  }
  logw("libusb_handle_events_completed: %d (%s)", iusb_state, state_get (iusb_state));

  logw("USB thread exit");

  //Wake up the reader if required
  int errData = -1;
  write(error_write_fd, &errData, sizeof(errData));

}

void HUTransportStreamUSB::libusb_callback(libusb_transfer *transfer)
{
  logd("libusb_callback %d %d %d", transfer->status, LIBUSB_TRANSFER_COMPLETED, LIBUSB_TRANSFER_OVERFLOW);
  libusb_transfer_status recv_last_status = transfer->status;
  if (recv_last_status == LIBUSB_TRANSFER_COMPLETED || recv_last_status == LIBUSB_TRANSFER_OVERFLOW)
  {
    if (recv_last_status == LIBUSB_TRANSFER_OVERFLOW)
    {
      logw("LIBUSB_TRANSFER_OVERFLOW");
      recv_temp_buffer.resize(recv_temp_buffer.size() * 2);
      start_usb_recv();
    }
    else
    {
      size_t bytesToWrite = transfer->actual_length;
      unsigned char *buffer = transfer->buffer;

      ssize_t ret = 0;
      while (bytesToWrite > 0)
      {
        ret = write(pipe_write_fd, buffer, bytesToWrite);
        if (ret < 0)
          break;
        logd("Wrote %d of %d bytes", ret, transfer->actual_length);
        buffer += ret;
        bytesToWrite -= ret;
      }

      if (ret < 0)
      {
        loge("libusb_callback: write failed");
        write(abort_usb_thread_pipe_write_fd, &abort_usb_thread_pipe_write_fd, 1);
      }
      else
      {
        start_usb_recv();
      }
    }
  }
  else
  {
    loge("libusb_callback: abort");
    write(abort_usb_thread_pipe_write_fd, &abort_usb_thread_pipe_write_fd, 1);
  }
  libusb_free_transfer(transfer);
}

void HUTransportStreamUSB::libusb_callback_tramp(libusb_transfer *transfer)
{
  reinterpret_cast<HUTransportStreamUSB*>(transfer->user_data)->libusb_callback(transfer);
}


void HUTransportStreamUSB::libusb_callback_send(libusb_transfer *transfer)
{
  logd("libusb_callback_send %d %d %d", transfer->status, LIBUSB_TRANSFER_COMPLETED, LIBUSB_TRANSFER_OVERFLOW);
  libusb_transfer_status recv_last_status = transfer->status;
  if (recv_last_status != LIBUSB_TRANSFER_COMPLETED)
  {
    loge("libusb_callback_send: abort");
    write(abort_usb_thread_pipe_write_fd, &abort_usb_thread_pipe_write_fd, 1);
  }
  free(transfer->buffer);
  libusb_free_transfer(transfer);
}

void HUTransportStreamUSB::libusb_callback_send_tramp(libusb_transfer *transfer)
{
  reinterpret_cast<HUTransportStreamUSB*>(transfer->user_data)->libusb_callback_send(transfer);
}


int HUTransportStreamUSB::start_usb_recv()
{
    libusb_transfer *transfer = libusb_alloc_transfer(0);
    libusb_fill_bulk_transfer(transfer, iusb_dev_hndl, iusb_ep_in,
      recv_temp_buffer.data(), recv_temp_buffer.size(), &libusb_callback_tramp, this, 0);

    int iusb_state = libusb_submit_transfer(transfer);
    if (iusb_state < 0)
    {
      loge("  Failed: libusb_submit_transfer: %d (%s)", iusb_state, iusb_error_get (iusb_state));
      libusb_free_transfer(transfer);
    }
    else
    {
      logd(" libusb_submit_transfer for %d bytes", recv_temp_buffer.size());
    }
    return iusb_state;
}

// ---- Adaptor recovery -------------------------------------------------------------------------
// Some wireless AA adaptors drop off USB when the phone goes out of range and never come back by
// themselves; only unplugging them helps. We remember which hub port the AA device was on, and if
// that port is still empty a while after a session ended, switch the port's power off and on again
// (the same as replugging). Only that one port is touched, only while nothing is connected to it,
// and only on hubs that can switch ports individually.

struct AdaptorPort
{
  std::string hub;   // sysfs name of the hub, e.g. "2-1" or "usb2" for a root hub
  int port = 0;      // port number on that hub
};
static AdaptorPort adaptor_port;

static bool read_sysfs_int(const std::string& path, int& value)
{
  FILE* f = fopen(path.c_str(), "r");
  if (!f)
    return false;
  bool ok = fscanf(f, "%d", &value) == 1;
  fclose(f);
  return ok;
}

// Finds the sysfs name ("2-1.2") of the USB device with this bus number and address
static std::string sysfs_name_of(int bus, int address)
{
  DIR* dir = opendir("/sys/bus/usb/devices");
  if (!dir)
    return "";
  std::string found;
  while (struct dirent* entry = readdir(dir))
  {
    std::string name = entry->d_name;
    if (name[0] == '.' || name.find(':') != std::string::npos)   // skip interfaces like "2-1.2:1.0"
      continue;
    int b = 0, d = 0;
    if (read_sysfs_int("/sys/bus/usb/devices/" + name + "/busnum", b) &&
        read_sysfs_int("/sys/bus/usb/devices/" + name + "/devnum", d) && b == bus && d == address)
    {
      found = name;
      break;
    }
  }
  closedir(dir);
  return found;
}

// "2-1.2" -> hub "2-1" port 2; "2-1" -> root hub "usb2" port 1
static bool split_port_path(const std::string& name, AdaptorPort& out)
{
  size_t sep = name.find_last_of(".-");
  if (name.empty() || sep == std::string::npos || sep + 1 >= name.size())
    return false;
  out.port = atoi(name.c_str() + sep + 1);
  out.hub = name[sep] == '.' ? name.substr(0, sep) : "usb" + name.substr(0, sep);
  return out.port > 0;
}

static void remember_adaptor_port(libusb_device* device)
{
  std::string name = sysfs_name_of(libusb_get_bus_number(device), libusb_get_device_address(device));
  AdaptorPort found;
  if (!split_port_path(name, found))
    return;
  if (found.hub != adaptor_port.hub || found.port != adaptor_port.port)
    logw("AA device is on USB %s port %d", found.hub.c_str(), found.port);
  adaptor_port = found;
}

static libusb_device_handle* open_hub(libusb_context* ctx, const std::string& hub)
{
  int bus = 0, address = 0;
  if (!read_sysfs_int("/sys/bus/usb/devices/" + hub + "/busnum", bus) ||
      !read_sysfs_int("/sys/bus/usb/devices/" + hub + "/devnum", address))
    return nullptr;
  libusb_device** devices = nullptr;
  ssize_t count = libusb_get_device_list(ctx, &devices);
  libusb_device_handle* handle = nullptr;
  for (ssize_t i = 0; i < count; i++)
  {
    if (libusb_get_bus_number(devices[i]) == bus && libusb_get_device_address(devices[i]) == address)
    {
      if (libusb_open(devices[i], &handle) < 0)
        handle = nullptr;
      break;
    }
  }
  if (count >= 0)
    libusb_free_device_list(devices, 1);
  return handle;
}

// Power-cycles the adaptor's port if it is empty. Returns true if it did.
static bool power_cycle_adaptor_port(libusb_context* ctx)
{
  const uint8_t to_hub = 0xA0, from_port = 0xA3, to_port = 0x23;
  const uint16_t PORT_POWER = 8;
  libusb_device_handle* hub = open_hub(ctx, adaptor_port.hub);
  if (!hub)
  {
    loge("Adaptor recovery: can't open hub %s", adaptor_port.hub.c_str());
    return false;
  }
  bool cycled = false;
  unsigned char hub_desc[16] = {0};
  unsigned char status[4] = {0};
  if (libusb_control_transfer(hub, to_hub, LIBUSB_REQUEST_GET_DESCRIPTOR, 0x29 << 8, 0, hub_desc, sizeof(hub_desc), 1000) < 5 ||
      (hub_desc[3] & 3) != 1)
  {
    logw("Adaptor recovery: hub %s can't switch ports individually, not touching it", adaptor_port.hub.c_str());
  }
  else if (libusb_control_transfer(hub, from_port, LIBUSB_REQUEST_GET_STATUS, 0, adaptor_port.port, status, 4, 1000) != 4)
  {
    loge("Adaptor recovery: can't read hub %s port %d status", adaptor_port.hub.c_str(), adaptor_port.port);
  }
  else if (status[0] & 0x01)
  {
    logw("Adaptor recovery: something is connected to hub %s port %d, leaving it alone", adaptor_port.hub.c_str(), adaptor_port.port);
  }
  else
  {
    logw("Adaptor recovery: hub %s port %d is empty, switching its power off and on", adaptor_port.hub.c_str(), adaptor_port.port);
    libusb_control_transfer(hub, to_port, LIBUSB_REQUEST_CLEAR_FEATURE, PORT_POWER, adaptor_port.port, nullptr, 0, 1000);
    ms_sleep(2000);
    int ret = libusb_control_transfer(hub, to_port, LIBUSB_REQUEST_SET_FEATURE, PORT_POWER, adaptor_port.port, nullptr, 0, 1000);
    if (ret < 0)
      loge("Adaptor recovery: switching hub %s port %d back on failed: %d", adaptor_port.hub.c_str(), adaptor_port.port, ret);
    cycled = true;
  }
  libusb_close(hub);
  return cycled;
}

int HUTransportStreamUSB::Start(bool waitForDevice) {

  if (iusb_state == hu_STATE_STARTED) {
    logd ("CHECK: iusb_state: %d (%s)", iusb_state, state_get (iusb_state));
    return (0);
  }

  iusb_state = hu_STATE_STARTIN;
  logd ("  SET: iusb_state: %d (%s)", iusb_state, state_get (iusb_state));

  if (libusb_init(&iusb_ctx) < 0)
  {
      loge ("Error libusb_init usb_err failed");
      Stop();
      return (-1);
  }

  libusb_set_debug(iusb_ctx, LIBUSB_LOG_LEVEL_INFO);

  // Listen for USB devices being added before scanning, so a device that (re)appears while
  // we scan, e.g. a phone switching to accessory mode after ACC_REQ_START, can't be missed.
  UsbAddMonitor usb_monitor;
  bool verbose = true;
  bool logged_waiting = false;
  // Adaptor recovery (see power_cycle_adaptor_port): first try 30 s after the session ended, then
  // every 2 minutes, at most 10 times
  auto waiting_since = std::chrono::steady_clock::now();
  auto next_recovery = waiting_since + std::chrono::seconds(30);
  int recovery_attempts = 0;

  //See if there is a OAP device already
  while ((iusb_dev_hndl = find_oap_device()) == nullptr)
  {
    logd("Scanning USB devices");
    libusb_device** devices = nullptr;
    ssize_t dev_count = libusb_get_device_list(iusb_ctx, &devices);
    if (dev_count < 0)
    {
      loge ("Error libusb_get_device_list usb_err: %d (%s)", dev_count, iusb_error_get (dev_count));
      Stop();
      return (-1);
    }

    bool tried_any = false;
    for (ssize_t i = 0; i < dev_count && !tried_any; i++)
    {
        tried_any = switch_to_accessory_mode(devices[i], verbose);
    }

    //unref the devices
    libusb_free_device_list(devices, 1);

    if (tried_any)
    {
        //Try right away just incase
        if ((iusb_dev_hndl = find_oap_device()) == nullptr)
        {
            logw("Waiting for the device to reconnect in accessory mode");
            //Give it some time to reconnect, then scan again even if we saw nothing
            usb_monitor.wait_for_add(5000);
            verbose = true;
        }
    }
    else
    {
        if (waitForDevice)
        {
            // A phone (or AA dongle) can be plugged in but not answer yet, e.g. while it or the
            // CMU is still starting up. It won't be plugged in again, so keep rescanning.
            if (!logged_waiting)
            {
                logw("Nothing found, waiting (rescanning every 2s)");
                logged_waiting = true;
            }
            verbose = usb_monitor.wait_for_add(2000);
            if (!verbose && adaptor_port.port > 0 && recovery_attempts < 10 &&
                std::chrono::steady_clock::now() >= next_recovery)
            {
                recovery_attempts++;
                power_cycle_adaptor_port(iusb_ctx);
                next_recovery = std::chrono::steady_clock::now() + std::chrono::seconds(120);
            }
        }
        else
        {
            loge ("Can't find any OAP devices");
            Stop();
            return (-1);
        }
    }
  }

  logw("Found OAP Device");
  remember_adaptor_port(libusb_get_device(iusb_dev_hndl));

  // Right after a device reappears in accessory mode its interface may not exist yet
  // (LIBUSB_ERROR_NOT_FOUND); give it a moment instead of tearing the whole session down.
  int usb_err = libusb_claim_interface (iusb_dev_hndl, 0);
  for (int attempt = 0; usb_err == LIBUSB_ERROR_NOT_FOUND && attempt < 10; attempt++) {
    ms_sleep (100);
    usb_err = libusb_claim_interface (iusb_dev_hndl, 0);
  }
  if (usb_err) {
    loge ("Error libusb_claim_interface usb_err: %d (%s)", usb_err, iusb_error_get (usb_err));
    Stop();
    return (-1);
  }
  logw ("OK libusb_claim_interface usb_err: %d (%s)", usb_err, iusb_error_get (usb_err));

  libusb_device* got_device = libusb_get_device(iusb_dev_hndl);

  //OAP uses config 0 for normal operation
  struct libusb_config_descriptor * config = nullptr;
  usb_err = libusb_get_config_descriptor (got_device, 0, &config);
  if (usb_err != 0) {
    loge ("Error libusb_get_config_descriptor usb_err: %d (%s)  errno: %d (%s)", usb_err, iusb_error_get (usb_err), errno, strerror (errno));
    Stop();
    return (-1);
  }

  int num_int = config->bNumInterfaces;                               // Get number of interfaces
  logw ("Done get_config_descriptor config: %p  num_int: %d", config, num_int);

  for (int idx = 0; idx < num_int && (iusb_ep_in < 0 || iusb_ep_out < 0); idx ++)
  {                              // For all interfaces...
    const libusb_interface& inter = config->interface[idx];
    int num_altsetting = inter.num_altsetting;
    logd ("num_altsetting: %d", num_altsetting);
    for (int j = 0; j < inter.num_altsetting && (iusb_ep_in < 0 || iusb_ep_out < 0); j ++)
    {                    // For all alternate settings...
      const libusb_interface_descriptor& interdesc = inter.altsetting[j];
      int num_int = interdesc.bInterfaceNumber;
      logd ("num_int: %d", num_int);
      int num_eps = interdesc.bNumEndpoints;
      logd ("num_eps: %d", num_eps);
      for (int k = 0; k < num_eps && (iusb_ep_in < 0 || iusb_ep_out < 0); k ++)
      {                                // For all endpoints...
        const libusb_endpoint_descriptor& epdesc = interdesc.endpoint[k];
        if (epdesc.bDescriptorType == LIBUSB_DT_ENDPOINT &&
            (epdesc.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK)
        {          // 5
            int ep_add = epdesc.bEndpointAddress;
            if (ep_add & LIBUSB_ENDPOINT_DIR_MASK)
            {
              if (iusb_ep_in < 0) {
                iusb_ep_in = ep_add;                                   // Set input endpoint
                logw ("iusb_ep_in: 0x%02x", iusb_ep_in);
              }
            }
            else
            {
              if (iusb_ep_out < 0) {
                iusb_ep_out = ep_add;                                  // Set output endpoint
                logw ("iusb_ep_out: 0x%02x", iusb_ep_out);
              }
            }
        }
      }
    }
  }
  libusb_free_config_descriptor (config);
  if (iusb_ep_in < 0 || iusb_ep_out < 0)
  {
      loge ("Error can't find endpoints");
      Stop();
      return (-1);
  }

  int pipefd[2] = {-1,-1};
  if (pipe(pipefd) < 0)
  {
    loge("Pipe create failed");
    return -1;
  }
  readfd = pipefd[0];
  pipe_write_fd = pipefd[1];

  if (pipe(pipefd) < 0)
  {
    loge("Error pipe create failed");
    return -1;
  }
  errorfd = pipefd[0];
  error_write_fd = pipefd[1];

  if (pipe(pipefd) < 0)
  {
    loge("Error pipe create failed");
    return -1;
  }
  abort_usb_thread_pipe_read_fd = pipefd[0];
  abort_usb_thread_pipe_write_fd = pipefd[1];
  //Add entry for our cancel fd
  pollfd abort_poll;
  abort_poll.fd = abort_usb_thread_pipe_read_fd;
  abort_poll.events = POLLIN;
  abort_poll.revents = 0;
  usb_thread_event_fds.push_back(abort_poll);


  const libusb_pollfd** existing_poll_fds = libusb_get_pollfds(iusb_ctx);
  for (auto cur_poll_fd_ptr = existing_poll_fds; *cur_poll_fd_ptr; cur_poll_fd_ptr++)
  {
      auto cur_poll_fd = *cur_poll_fd_ptr;
      pollfd new_poll;
      new_poll.fd = cur_poll_fd->fd;
      new_poll.events = cur_poll_fd->events;
      new_poll.revents = 0;

      usb_thread_event_fds.push_back(new_poll);
  }
#if LIBUSB_API_VERSION >= 0x01000104
  libusb_free_pollfds(existing_poll_fds);
#endif

  libusb_set_pollfd_notifiers(iusb_ctx, &libusb_callback_pollfd_added_tramp, &libusb_callback_pollfd_removed_tramp, this);

  usb_recv_thread = std::thread([this]{ this->usb_recv_thread_main(); });

  recv_temp_buffer.resize(16384);
  start_usb_recv();

  iusb_state = hu_STATE_STARTED;
  logd ("  SET: iusb_state: %d (%s)", iusb_state, state_get (iusb_state));
  return (0);
}

void HUTransportStreamUSB::libusb_callback_pollfd_added(int fd, short events)
{
    pollfd new_poll;
    new_poll.fd = fd;
    new_poll.events = events;
    new_poll.revents = 0;

    usb_thread_event_fds.push_back(new_poll);
}

void HUTransportStreamUSB::libusb_callback_pollfd_added_tramp(int fd, short events, void* user_data)
{
    reinterpret_cast<HUTransportStreamUSB*>(user_data)->libusb_callback_pollfd_added(fd, events);
}

void HUTransportStreamUSB::libusb_callback_pollfd_removed(int fd)
{
    usb_thread_event_fds.erase(std::remove_if(usb_thread_event_fds.begin(),
                                              usb_thread_event_fds.end(),
                                              [fd](pollfd& p) { return p.fd == fd; }),
                                usb_thread_event_fds.end());
}

void HUTransportStreamUSB::libusb_callback_pollfd_removed_tramp(int fd, void* user_data)
{
    reinterpret_cast<HUTransportStreamUSB*>(user_data)->libusb_callback_pollfd_removed(fd);
}

// Prints the USB devices and, for hubs, whether ports can be powered individually and each
// port's status. Diagnostic only ("headunit usbinfo"), it changes nothing.
void hu_usb_print_info()
{
  libusb_context* ctx = nullptr;
  if (libusb_init(&ctx) < 0)
  {
    printf("usbinfo: libusb_init failed\n");
    return;
  }
  libusb_device** devices = nullptr;
  ssize_t count = libusb_get_device_list(ctx, &devices);
  for (ssize_t i = 0; i < count; i++)
  {
    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(devices[i], &desc) < 0)
      continue;
    printf("device bus %u address %u: %04x:%04x class %u\n",
           libusb_get_bus_number(devices[i]), libusb_get_device_address(devices[i]),
           desc.idVendor, desc.idProduct, desc.bDeviceClass);
    if (desc.bDeviceClass != LIBUSB_CLASS_HUB)
      continue;

    libusb_device_handle* handle = nullptr;
    if (libusb_open(devices[i], &handle) < 0)
    {
      printf("  hub: cannot open\n");
      continue;
    }
    unsigned char hub_desc[16] = {0};
    // GET_DESCRIPTOR (hub class): bmRequestType 0xA0, descriptor type 0x29
    int len = libusb_control_transfer(handle, 0xA0, LIBUSB_REQUEST_GET_DESCRIPTOR, 0x29 << 8, 0,
                                      hub_desc, sizeof(hub_desc), 1000);
    if (len >= 5)
    {
      int ports = hub_desc[2];
      int characteristics = hub_desc[3] | (hub_desc[4] << 8);
      const char* power[] = {"ganged (all ports together)", "individual per port", "none", "none"};
      printf("  hub: %d ports, power switching: %s\n", ports, power[characteristics & 3]);
      for (int port = 1; port <= ports; port++)
      {
        unsigned char status[4] = {0};
        // GET_STATUS (port): bmRequestType 0xA3
        if (libusb_control_transfer(handle, 0xA3, LIBUSB_REQUEST_GET_STATUS, 0, port, status, 4, 1000) == 4)
        {
          int s = status[0] | (status[1] << 8);
          printf("  port %d: %s%s%s\n", port, (s & 0x0001) ? "connected " : "empty ",
                 (s & 0x0002) ? "enabled " : "", (s & 0x0100) ? "powered" : "not-powered");
        }
      }
    }
    else
    {
      printf("  hub: descriptor read failed (%d)\n", len);
    }
    libusb_close(handle);
  }
  if (count >= 0)
    libusb_free_device_list(devices, 1);
  libusb_exit(ctx);
}

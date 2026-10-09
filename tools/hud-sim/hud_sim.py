#!/usr/bin/env python3
"""Fake Mazda CMU HUD dbus services, so the HUD code in mazda/hud can run on a desktop.

Starts two private dbus buses on the same socket paths the car uses, serves the three
HUD methods the headunit calls, and shows what the HUD would display on a web page
(default http://localhost:6081) with buttons to inject the failures seen in the car.

    python3 tools/hud-sim/hud_sim.py [--port 6081]

Then run the desktop headunit built with `make HUD_SIM=1`.
"""
import argparse
import json
import os
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

HERE = os.path.dirname(os.path.abspath(__file__))
ICON_DIR = os.path.join(HERE, '..', '..', 'mazda', 'hud', 'icons')

SERVICE_SOCKET = '/tmp/dbus_service_socket'
HMI_SOCKET = '/tmp/dbus_hmi_socket'

BUS_CONFIG = """<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:path={socket}</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
"""

UNITS = {1: 'm', 2: 'mi', 3: 'km', 4: 'yd', 5: 'ft'}

ICON_NAMES = {
    0: 'none', 1: 'straight', 2: 'left', 3: 'right', 4: 'slight left', 5: 'slight right',
    7: 'off ramp left', 8: 'destination', 9: 'sharp right', 10: 'u-turn right', 11: 'sharp left',
    12: 'flag', 13: 'u-turn left', 14: 'fork right', 15: 'fork left', 16: 'merge left',
    17: 'merge right', 30: 'off ramp right', 33: 'destination left', 34: 'destination right',
    35: 'flag left', 36: 'flag right',
}
for i in range(12):
    ICON_NAMES[37 + i] = 'roundabout exit %d deg (side 37)' % (i * 30)
    ICON_NAMES[49 + i] = 'roundabout exit %d deg (side 49)' % (i * 30)


class HudState:
    def __init__(self):
        self.lock = threading.Lock()
        self.installed = True
        self.fail_next = 0
        self.ignore_same_counter = True
        self.display = {'icon': 0, 'distance': 0, 'unit': 1, 'counter': 0, 'text': '', 'updated': None}
        self.last_counter = None
        self.stats = {'display_calls': 0, 'text_calls': 0, 'ignored': 0, 'failed': 0, 'installed_calls': 0}
        self.last_call = None
        self.log = []

    def add_log(self, kind, detail):
        self.log.append({'time': time.strftime('%H:%M:%S'), 'kind': kind, 'detail': detail})
        del self.log[:-40]

    def snapshot(self):
        with self.lock:
            d = dict(self.display)
            d['icon_name'] = ICON_NAMES.get(d['icon'], 'unknown code (HUD shows no navigation)')
            d['distance_text'] = '%g %s' % (d['distance'] / 10.0, UNITS.get(d['unit'], '?unit %d' % d['unit']))
            d['has_photo'] = os.path.exists(os.path.join(ICON_DIR, '%04d.jpg' % d['icon']))
            return {
                'installed': self.installed,
                'fail_next': self.fail_next,
                'ignore_same_counter': self.ignore_same_counter,
                'display': d,
                'stats': dict(self.stats),
                'seconds_since_call': None if self.last_call is None else round(time.time() - self.last_call, 1),
                'log': list(reversed(self.log)),
            }


STATE = HudState()


class HudSettings(dbus.service.Object):
    """com.jci.navi2IHU on the HMI bus"""

    @dbus.service.method('com.jci.navi2IHU.HUDSettings', in_signature='', out_signature='b')
    def GetHUDIsInstalled(self):
        with STATE.lock:
            STATE.stats['installed_calls'] += 1
            return STATE.installed


class VbsNavi(dbus.service.Object):
    """com.jci.vbs.navi on the service bus"""

    def _maybe_fail(self, method):
        if STATE.fail_next > 0:
            STATE.fail_next -= 1
            STATE.stats['failed'] += 1
            STATE.add_log('FAILED', '%s: injected dbus error' % method)
            raise dbus.exceptions.DBusException('Injected failure', name='com.jci.Error.Simulated')

    @dbus.service.method('com.jci.vbs.navi', in_signature='(uqyqyy)', out_signature='y')
    def SetHUDDisplayMsgReq(self, msg):
        icon, distance, unit, _speed, _speed_unit, counter = [int(v) for v in msg]
        with STATE.lock:
            STATE.last_call = time.time()
            STATE.stats['display_calls'] += 1
            self._maybe_fail('SetHUDDisplayMsgReq')
            detail = 'icon %d (%s), distance %g %s, counter %d' % (
                icon, ICON_NAMES.get(icon, 'unknown'), distance / 10.0, UNITS.get(unit, '?'), counter)
            # Assumption from upstream fix #185: the HUD drops a message that repeats the previous counter
            if STATE.ignore_same_counter and counter == STATE.last_counter:
                STATE.stats['ignored'] += 1
                STATE.add_log('IGNORED', detail + ' (same counter as last message)')
                return dbus.Byte(0)
            STATE.last_counter = counter
            STATE.display.update(icon=icon, distance=distance, unit=unit, counter=counter,
                                 updated=time.strftime('%H:%M:%S'))
            STATE.add_log('display', detail)
        return dbus.Byte(0)

    @dbus.service.method('com.jci.vbs.navi.tmc', in_signature='(sy)', out_signature='y')
    def SetHUD_Display_Msg2(self, msg):
        text, counter = str(msg[0]), int(msg[1])
        with STATE.lock:
            STATE.last_call = time.time()
            STATE.stats['text_calls'] += 1
            self._maybe_fail('SetHUD_Display_Msg2')
            STATE.display['text'] = text
            STATE.add_log('text', '"%s", counter %d' % (text, counter))
        return dbus.Byte(0)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, code, body, ctype):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == '/':
            with open(os.path.join(HERE, 'index.html'), 'rb') as f:
                self._send(200, f.read(), 'text/html; charset=utf-8')
        elif self.path == '/state':
            self._send(200, json.dumps(STATE.snapshot()).encode(), 'application/json')
        elif self.path.startswith('/icons/'):
            name = os.path.basename(self.path)
            path = os.path.join(ICON_DIR, name)
            if os.path.exists(path):
                with open(path, 'rb') as f:
                    self._send(200, f.read(), 'image/jpeg')
            else:
                self._send(404, b'not found', 'text/plain')
        else:
            self._send(404, b'not found', 'text/plain')

    def do_POST(self):
        if self.path != '/control':
            self._send(404, b'not found', 'text/plain')
            return
        body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', 0))) or b'{}')
        with STATE.lock:
            if 'installed' in body:
                STATE.installed = bool(body['installed'])
                STATE.add_log('control', 'HUD installed = %s' % STATE.installed)
            if 'fail_next' in body:
                STATE.fail_next = int(body['fail_next'])
                STATE.add_log('control', 'fail next %d HUD calls' % STATE.fail_next)
            if 'ignore_same_counter' in body:
                STATE.ignore_same_counter = bool(body['ignore_same_counter'])
                STATE.add_log('control', 'ignore repeated counter = %s' % STATE.ignore_same_counter)
            if body.get('clear_log'):
                STATE.log = []
        self._send(200, json.dumps(STATE.snapshot()).encode(), 'application/json')


class ThreadingHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True


def start_bus(socket_path, workdir):
    if os.path.exists(socket_path):
        os.unlink(socket_path)
    conf = os.path.join(workdir, os.path.basename(socket_path) + '.conf')
    with open(conf, 'w') as f:
        f.write(BUS_CONFIG.format(socket=socket_path))
    proc = subprocess.Popen(['dbus-daemon', '--config-file=' + conf, '--nofork'])
    for _ in range(50):
        if os.path.exists(socket_path):
            return proc
        time.sleep(0.1)
    sys.exit('dbus-daemon for %s did not start' % socket_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port', type=int, default=6081)
    parser.add_argument('--workdir', default='/tmp')
    args = parser.parse_args()

    buses = [start_bus(SERVICE_SOCKET, args.workdir), start_bus(HMI_SOCKET, args.workdir)]

    DBusGMainLoop(set_as_default=True)
    service_bus = dbus.bus.BusConnection('unix:path=' + SERVICE_SOCKET)
    hmi_bus = dbus.bus.BusConnection('unix:path=' + HMI_SOCKET)
    service_name = dbus.service.BusName('com.jci.vbs.navi', service_bus)
    hmi_name = dbus.service.BusName('com.jci.navi2IHU', hmi_bus)
    VbsNavi(service_name, '/com/jci/vbs/navi')
    HudSettings(hmi_name, '/com/jci/navi2IHU')

    server = ThreadingHTTPServer(('0.0.0.0', args.port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print('HUD simulator: http://localhost:%d (buses %s, %s)' % (args.port, SERVICE_SOCKET, HMI_SOCKET), flush=True)

    try:
        GLib.MainLoop().run()
    finally:
        for proc in buses:
            proc.terminate()


if __name__ == '__main__':
    main()

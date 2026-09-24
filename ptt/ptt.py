#!/usr/bin/env python3
"""Push-to-talk for Animates under Wine/niri.

Hold the PTT key (default Right Alt/AltGr, env PTT_KEY) >= 180 ms without pressing another key -> Ani listens;
release -> input closes. A short tap opens a text prompt (ani-ctl prompt). Reads keyboards via evdev (user must be in group
'input'), never grabs them, and talks to the app's WebView2 over the Chrome
DevTools port 9222.

  ptt.py           run the daemon
  ptt.py --test    open listening for 5 s once, then close
"""
import json
import os
import select
import subprocess
import sys
import time
import urllib.request

import evdev
import websocket
from evdev import ecodes

CDP = "http://127.0.0.1:9222/json"
HOLD_S = 0.18
OPEN_JS = "window.animateHost.requestOpenInput('listening')"
CLOSE_JS = "window.animateHost.closeInput()"
OPEN_PAGES = ("Context Menu", "Agent Bridge")
CLOSE_PAGES = ("Input", "Context Menu", "Agent Bridge")
ANI_CTL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bin", "ani-ctl")
PTT_KEY = getattr(ecodes, os.environ.get("PTT_KEY", "KEY_RIGHTALT"))


def log(*a):
    print(*a, flush=True)


def evaluate(js, titles):
    try:
        with urllib.request.urlopen(CDP, timeout=1) as r:
            pages = json.load(r)
    except OSError:
        log("animates not running (port 9222 closed)")
        return False
    by_title = {p.get("title"): p for p in pages if p.get("type") == "page"}
    for t in titles:
        page = by_title.get(t)
        if not page or "webSocketDebuggerUrl" not in page:
            continue
        try:
            ws = websocket.create_connection(page["webSocketDebuggerUrl"], timeout=2, suppress_origin=True)
            ws.send(json.dumps({"id": 1, "method": "Runtime.evaluate", "params": {"expression": js}}))
            res = json.loads(ws.recv())
            ws.close()
        except (OSError, websocket.WebSocketException) as e:
            log(f"{t}: {e}")
            continue
        if "exceptionDetails" in res.get("result", {}):
            log(f"{t}: JS error {res['result']['exceptionDetails'].get('text')}")
            continue
        log(f"{t}: {js}")
        return True
    log(f"no usable page among {list(by_title)}")
    return False


prompt_proc = None


def open_text_prompt():
    # The app's own input window can't be shown under Wine-Wayland, so use a
    # native zenity dialog (ani-ctl prompt) that submits via SUBMIT_TEXT.
    global prompt_proc
    if prompt_proc and prompt_proc.poll() is None:
        return
    try:
        urllib.request.urlopen(CDP, timeout=1).close()
    except OSError:
        return  # Ani not running
    prompt_proc = subprocess.Popen([ANI_CTL, "prompt"])
    log("text prompt opened")


def keyboards():
    devs = {}
    for path in evdev.list_devices():
        try:
            d = evdev.InputDevice(path)
        except OSError:
            continue
        if PTT_KEY in d.capabilities().get(ecodes.EV_KEY, []):
            devs[d.fd] = d
        else:
            d.close()
    return devs


def run():
    devs = keyboards()
    log("watching:", ", ".join(d.name for d in devs.values()))
    pressed_at = None   # Right Alt down, waiting for hold threshold
    listening = False
    last_scan = time.monotonic()
    while True:
        timeout = None
        if pressed_at is not None and not listening:
            timeout = max(0.0, pressed_at + HOLD_S - time.monotonic())
        r, _, _ = select.select(list(devs), [], [], 5.0 if timeout is None else timeout)

        if pressed_at is not None and not listening and time.monotonic() >= pressed_at + HOLD_S:
            listening = evaluate(OPEN_JS, OPEN_PAGES)
            if not listening:
                pressed_at = None

        for fd in r:
            try:
                events = list(devs[fd].read())
            except OSError:  # unplugged
                devs.pop(fd).close()
                continue
            for ev in events:
                if ev.type != ecodes.EV_KEY or ev.value == 2:
                    continue
                if ev.code == PTT_KEY:
                    if ev.value == 1:
                        pressed_at = time.monotonic()
                    else:
                        if listening:
                            evaluate(CLOSE_JS, CLOSE_PAGES)
                        elif pressed_at is not None:  # short tap -> text chat
                            open_text_prompt()
                        pressed_at, listening = None, False
                elif ev.value == 1 and pressed_at is not None and not listening:
                    pressed_at = None  # AltGr used as modifier (e.g. AltGr+Q = @)

        if time.monotonic() - last_scan > 10:  # pick up hotplugged keyboards
            last_scan = time.monotonic()
            known = {d.path for d in devs.values()}
            for fd, d in keyboards().items():
                if d.path in known:
                    d.close()
                else:
                    devs[fd] = d
                    log("added:", d.name)


if __name__ == "__main__":
    if "--test" in sys.argv:
        if evaluate(OPEN_JS, OPEN_PAGES):
            time.sleep(5)
            evaluate(CLOSE_JS, CLOSE_PAGES)
    else:
        run()

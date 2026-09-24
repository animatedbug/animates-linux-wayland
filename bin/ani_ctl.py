#!/usr/bin/env python3
"""Control Animates over its DevTools port (fallback when the in-app menus misbehave).

  ani-ctl mode transparent|background|pip|enlarged|holographic
  ani-ctl say TEXT    send a chat message to Ani
  ani-ctl prompt      ask for a message in a native dialog, then send it
  ani-ctl chat        open the app's own text input (invisible under Wine-Wayland)
  ani-ctl talk        open voice input (listening)
  ani-ctl close       close the input
"""
import json
import subprocess
import sys
import urllib.request
import uuid

import websocket

MODES = ("transparent", "background", "pip", "enlarged", "holographic")


def evaluate(js, titles=("Context Menu", "Agent Bridge")):
    pages = json.load(urllib.request.urlopen("http://127.0.0.1:9222/json", timeout=2))
    page = next((p for t in titles for p in pages if p.get("title") == t), None)
    if not page:
        sys.exit("Animates page not found – is Ani running?")
    ws = websocket.create_connection(page["webSocketDebuggerUrl"], timeout=3, suppress_origin=True)
    ws.send(json.dumps({"id": 1, "method": "Runtime.evaluate", "params": {"expression": js}}))
    res = json.loads(ws.recv())
    ws.close()
    if "exceptionDetails" in res.get("result", {}):
        sys.exit(f"JS error: {res['result']['exceptionDetails'].get('text')}")


def say(text):
    payload = json.dumps({"text": text, "attachments": [], "clientTurnId": str(uuid.uuid4())})
    evaluate(f"window.animateHost.sendToUnity(window.animateHost.MSG.SUBMIT_TEXT,{payload})")


def main(args):
    if len(args) == 2 and args[0] == "mode" and args[1] in MODES:
        if args[1] == "pip":
            evaluate("window.animateHost.sendToUnity(window.animateHost.MSG.ENTER_PIP)")
        else:
            evaluate(f"window.animateHost.sendToUnity(window.animateHost.MSG.SET_VIEW_MODE,{{mode:'{args[1]}'}})")
    elif len(args) >= 2 and args[0] == "say":
        say(" ".join(args[1:]))
    elif args == ["prompt"]:
        r = subprocess.run(["zenity", "--entry", "--title=Chat with Ani", "--text=Message to Ani:",
                            "--width=480"], capture_output=True, text=True)
        if r.returncode == 0 and r.stdout.strip():
            say(r.stdout.strip())
    elif args == ["chat"]:
        evaluate("window.animateHost.requestOpenInput('text')")
    elif args == ["talk"]:
        evaluate("window.animateHost.requestOpenInput('listening')")
    elif args == ["close"]:
        evaluate("window.animateHost.closeInput()", ("Input", "Context Menu", "Agent Bridge"))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])

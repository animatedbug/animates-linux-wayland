#!/usr/bin/env python3
"""Keep Animates' windows on the active niri workspace (niri has no sticky windows).

Listens to `niri msg -j event-stream`; whenever a workspace gets focus, moves every
window with app-id `animates.exe` there without stealing focus.
"""
import json
import subprocess
import sys

APP_ID = "animates.exe"


def niri_action(*args):
    subprocess.run(["niri", "msg", "action", *args], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def follow():
    workspaces = {}  # id -> workspace dict
    windows = {}     # id -> window dict

    def pull_along(ws_id):
        ws = workspaces.get(ws_id)
        if not ws:
            return
        for w in windows.values():
            if w["app_id"] == APP_ID and w["workspace_id"] != ws_id:
                niri_action("move-window-to-workspace", "--window-id", str(w["id"]),
                            "--focus", "false", str(ws["idx"]))

    proc = subprocess.Popen(["niri", "msg", "-j", "event-stream"], stdout=subprocess.PIPE, text=True)
    for line in proc.stdout:
        try:
            ev = json.loads(line)
        except json.JSONDecodeError:
            continue
        if "WorkspacesChanged" in ev:
            workspaces = {w["id"]: w for w in ev["WorkspacesChanged"]["workspaces"]}
        elif "WindowsChanged" in ev:
            windows = {w["id"]: w for w in ev["WindowsChanged"]["windows"]}
        elif "WindowOpenedOrChanged" in ev:
            w = ev["WindowOpenedOrChanged"]["window"]
            windows[w["id"]] = w
            focused = next((i for i, ws in workspaces.items() if ws["is_focused"]), None)
            if w["app_id"] == APP_ID and focused is not None:
                pull_along(focused)
        elif "WindowClosed" in ev:
            windows.pop(ev["WindowClosed"]["id"], None)
        elif "WorkspaceActivated" in ev:
            a = ev["WorkspaceActivated"]
            for ws in workspaces.values():
                if ws["id"] == a["id"]:
                    ws["is_active"] = True
                    ws["is_focused"] = a["focused"]
                elif a["focused"]:
                    ws["is_focused"] = False
            if a["focused"]:
                pull_along(a["id"])
    return proc.wait()


if __name__ == "__main__":
    # Stream ends when niri exits; systemd restarts us with the new NIRI_SOCKET.
    sys.exit(follow())

"""
Mock Room Booking API — runs on RPi for ESP32 LIV-24 panel
Start: python booking_api.py
Endpoint: GET http://<rpi-ip>:5000/booking
"""

from flask import Flask, jsonify
from datetime import datetime

app = Flask(__name__)

BOOKED_SLOTS = [
    {"start_h": 9,  "start_m": 0,  "end_h": 10, "end_m": 0,
     "title": "Weekly Standup",  "organizer": "Somchai K."},
    {"start_h": 13, "start_m": 0,  "end_h": 14, "end_m": 30,
     "title": "Design Review",   "organizer": "Nattawut P."},
    {"start_h": 15, "start_m": 0,  "end_h": 16, "end_m": 0,
     "title": "Sprint Planning", "organizer": "Weerachai T."},
]


def to_hhmm(h, m):
    return f"{h:02d}:{m:02d}"


@app.route("/booking")
def booking():
    now = datetime.now()
    now_min = now.hour * 60 + now.minute

    current = None
    upcoming = []

    for s in BOOKED_SLOTS:
        start_min = s["start_h"] * 60 + s["start_m"]
        end_min   = s["end_h"]   * 60 + s["end_m"]
        entry = {
            "title":     s["title"],
            "organizer": s["organizer"],
            "start":     to_hhmm(s["start_h"], s["start_m"]),
            "end":       to_hhmm(s["end_h"],   s["end_m"]),
        }
        if start_min <= now_min < end_min:
            current = entry
        elif start_min > now_min:
            upcoming.append(entry)

    return jsonify({
        "room":    "Meeting Room A",
        "floor":   "3F",
        "status":  "booked" if current else "available",
        "current": current,
        "next":    upcoming[0] if upcoming else None,
    })


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False)

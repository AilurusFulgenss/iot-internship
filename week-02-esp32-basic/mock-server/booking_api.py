"""
LIV-24 Room Status API — Mock Server (Demo build)

GET /api/rooms/<room_id>
  Returns: { id, name, status, current, next }

Toggle BOOKED / AVAILABLE by commenting one ROOM_STATE block below.
"""

from flask import Flask, jsonify

app = Flask(__name__)

ROOM_ID   = "M-MTG1"
ROOM_NAME = "Meeting Room 1"

# ── AVAILABLE ─────────────────────────────────────────────────
# ROOM_STATE = {
#     "status":  "available",
#     "current": None,
#     "next":    {"start": "11:00", "end": "12:00"},
# }

# ── BOOKED ────────────────────────────────────────────────────
ROOM_STATE = {
    "status":  "booked",
    "current": {
        "title":     "Project Review",
        "organizer": "Kuda V. (Unit 2403)",
        "start":     "09:00",
        "end":       "11:00",
    },
    "next": {"start": "14:00", "end": "15:00"},
}

# ── Endpoint ──────────────────────────────────────────────────

@app.route("/api/rooms/<room_id>")
def get_room(room_id):
    if room_id != ROOM_ID:
        return jsonify({"error": "Room not found"}), 404
    return jsonify({
        "id":      ROOM_ID,
        "name":    ROOM_NAME,
        "status":  ROOM_STATE["status"],
        "current": ROOM_STATE["current"],
        "next":    ROOM_STATE["next"],
    })

if __name__ == "__main__":
    print(f"LIV-24 Mock API  —  {ROOM_NAME}")
    print(f"State : {ROOM_STATE['status'].upper()}")
    print("URL   : http://0.0.0.0:5000")
    app.run(host="0.0.0.0", port=5000, debug=False)

"""
Sansiri Mock Room Booking API
Runs on RPi — python booking_api.py
Base URL: http://192.168.1.112:5000

Endpoints:
  GET  /api/buildings                       → list all buildings
  GET  /api/rooms?building=A                → all rooms (opt. filter by building)
  GET  /api/rooms/<room_id>                 → room detail + today's schedule
  GET  /api/rooms/<room_id>/slots?date=...  → available 30-min slots
  POST /api/bookings                        → create booking
  DELETE /api/bookings/<booking_id>         → cancel booking
"""

from flask import Flask, jsonify, request, abort
from datetime import datetime, date, timedelta
import uuid

app = Flask(__name__)

# ─── Static room data ────────────────────────────────────────────────────────

BUILDINGS = {
    "A": {"name": "Building A", "floors": 4},
    "B": {"name": "Building B", "floors": 6},
    "C": {"name": "Building C", "floors": 3},
    "D": {"name": "Building D", "floors": 5},
    "E": {"name": "Building E", "floors": 8},
}

ROOMS = {
    # ── Building A ──
    "A-M1": {"id": "A-M1", "building": "A", "name": "Meeting Room A1",  "type": "meeting",   "floor": "2F", "capacity": 8},
    "A-M2": {"id": "A-M2", "building": "A", "name": "Meeting Room A2",  "type": "meeting",   "floor": "2F", "capacity": 8},
    "A-TH": {"id": "A-TH", "building": "A", "name": "Theater A",        "type": "theater",   "floor": "3F", "capacity": 40},
    "A-TR": {"id": "A-TR", "building": "A", "name": "Training Room A",  "type": "training",  "floor": "4F", "capacity": 20},

    # ── Building B ──
    "B-M1": {"id": "B-M1", "building": "B", "name": "Meeting Room B1",  "type": "meeting",   "floor": "1F", "capacity": 6},
    "B-M2": {"id": "B-M2", "building": "B", "name": "Meeting Room B2",  "type": "meeting",   "floor": "1F", "capacity": 10},
    "B-BD": {"id": "B-BD", "building": "B", "name": "Boardroom B",      "type": "boardroom", "floor": "6F", "capacity": 20},

    # ── Building C ──
    "C-M1": {"id": "C-M1", "building": "C", "name": "Meeting Room C1",  "type": "meeting",   "floor": "1F", "capacity": 8},
    "C-M2": {"id": "C-M2", "building": "C", "name": "Meeting Room C2",  "type": "meeting",   "floor": "2F", "capacity": 8},
    "C-TR": {"id": "C-TR", "building": "C", "name": "Training Room C",  "type": "training",  "floor": "3F", "capacity": 30},

    # ── Building D ──
    "D-M1": {"id": "D-M1", "building": "D", "name": "Meeting Room D1",  "type": "meeting",   "floor": "1F", "capacity": 6},
    "D-M2": {"id": "D-M2", "building": "D", "name": "Meeting Room D2",  "type": "meeting",   "floor": "2F", "capacity": 8},
    "D-BD": {"id": "D-BD", "building": "D", "name": "Boardroom D",      "type": "boardroom", "floor": "5F", "capacity": 15},

    # ── Building E ──
    "E-M1": {"id": "E-M1", "building": "E", "name": "Meeting Room E1",  "type": "meeting",   "floor": "1F", "capacity": 6},
    "E-EX": {"id": "E-EX", "building": "E", "name": "Executive Suite E","type": "boardroom", "floor": "8F", "capacity": 12},
    "E-TH": {"id": "E-TH", "building": "E", "name": "Grand Theater E",  "type": "theater",   "floor": "2F", "capacity": 80},
}

# ─── Seeded bookings (realistic daily schedule) ──────────────────────────────
# These regenerate fresh each day — organizer names rotate for realism.

_SEED_TEMPLATES = [
    # room_id, start_h, start_m, end_h, end_m, title, organizer
    ("A-M1", 9,  0, 10,  0, "Weekly Standup",      "Somchai K."),
    ("A-M1", 13, 0, 14, 30, "Design Review",        "Nattawut P."),
    ("A-M1", 15, 0, 16,  0, "Sprint Planning",      "Weerachai T."),
    ("A-M2", 10, 0, 11, 30, "Product Roadmap",      "Panida S."),
    ("A-M2", 14, 0, 15,  0, "Vendor Meeting",       "Ariya W."),
    ("A-TH", 9,  0, 12,  0, "All-Hands Q2",         "CEO Office"),
    ("A-TH", 14, 0, 16, 30, "Training: Safety",     "HR Team"),
    ("A-TR", 9,  0, 17,  0, "Onboarding Day 1",     "HR Team"),
    ("B-M1", 10, 0, 11,  0, "1:1 Session",          "Thanakorn R."),
    ("B-M1", 14, 30,15, 30, "Budget Review",        "Finance Team"),
    ("B-M2", 9,  0, 10, 30, "Customer Call",        "Sales Team"),
    ("B-M2", 13, 0, 14,  0, "Legal Briefing",       "Legal Dept."),
    ("B-M2", 16, 0, 17,  0, "EOD Sync",             "Ops Team"),
    ("B-BD", 9,  0, 11,  0, "Board Meeting",        "Exec. Team"),
    ("B-BD", 14, 0, 16,  0, "Strategy Session",     "C-Suite"),
    ("C-M1", 11, 0, 12,  0, "UX Research Sync",     "Pimchanok L."),
    ("C-M2", 13, 30,15,  0, "Dev Sprint Review",    "Dev Team A"),
    ("C-TR", 9,  0, 12,  0, "React Workshop",       "Tech Academy"),
    ("C-TR", 13, 0, 17,  0, "LVGL Deep Dive",       "IoT Team"),
    ("D-M1", 10, 0, 11,  0, "Q3 Planning",          "Siriporn C."),
    ("D-M2", 14, 0, 15, 30, "Marketing Review",     "Brand Team"),
    ("D-BD", 9,  0, 10,  0, "Investor Update",      "CFO Office"),
    ("D-BD", 13, 0, 14,  0, "Risk Committee",       "Risk Mgmt."),
    ("E-M1", 9, 30,10, 30, "Kickoff: Phase 2",     "PM Office"),
    ("E-EX", 11, 0, 13,  0, "Executive Lunch",      "C-Suite"),
    ("E-EX", 15, 0, 17,  0, "Confidential Review",  "Exec. Team"),
    ("E-TH", 13, 0, 16,  0, "Company Town Hall",    "HR + Comms"),
]


def _make_booking_id():
    return str(uuid.uuid4())[:8].upper()


def _seed_bookings_for_today():
    today = date.today().isoformat()
    result = {}
    for (room_id, sh, sm, eh, em, title, organizer) in _SEED_TEMPLATES:
        bid = _make_booking_id()
        result[bid] = {
            "id":        bid,
            "room_id":   room_id,
            "date":      today,
            "start":     f"{sh:02d}:{sm:02d}",
            "end":       f"{eh:02d}:{em:02d}",
            "title":     title,
            "organizer": organizer,
            "seeded":    True,
        }
    return result


# In-memory booking store — seeded fresh at startup
_bookings: dict = _seed_bookings_for_today()
_last_seed_date: str = date.today().isoformat()


def _ensure_fresh_seed():
    """Re-seed daily so the schedule resets each morning."""
    global _bookings, _last_seed_date
    today = date.today().isoformat()
    if today != _last_seed_date:
        fresh = _seed_bookings_for_today()
        # keep user-created bookings from today
        user_bookings = {k: v for k, v in _bookings.items()
                         if not v.get("seeded") and v["date"] == today}
        _bookings = {**fresh, **user_bookings}
        _last_seed_date = today


# ─── Helpers ─────────────────────────────────────────────────────────────────

def _hhmm_to_min(s: str) -> int:
    h, m = s.split(":")
    return int(h) * 60 + int(m)


def _bookings_for_room_date(room_id: str, date_str: str) -> list:
    _ensure_fresh_seed()
    result = [b for b in _bookings.values()
              if b["room_id"] == room_id and b["date"] == date_str]
    return sorted(result, key=lambda b: _hhmm_to_min(b["start"]))


def _current_booking(room_id: str) -> dict | None:
    now = datetime.now()
    now_min = now.hour * 60 + now.minute
    for b in _bookings_for_room_date(room_id, date.today().isoformat()):
        if _hhmm_to_min(b["start"]) <= now_min < _hhmm_to_min(b["end"]):
            return b
    return None


def _next_booking(room_id: str) -> dict | None:
    now = datetime.now()
    now_min = now.hour * 60 + now.minute
    for b in _bookings_for_room_date(room_id, date.today().isoformat()):
        if _hhmm_to_min(b["start"]) > now_min:
            return b
    return None


def _available_slots(room_id: str, date_str: str) -> list:
    """Return list of free 30-min slots between 08:00 and 18:00."""
    booked = _bookings_for_room_date(room_id, date_str)
    slots = []
    slot_start = 8 * 60  # 08:00
    slot_end   = 18 * 60  # 18:00

    while slot_start < slot_end:
        s_str = f"{slot_start // 60:02d}:{slot_start % 60:02d}"
        e_min = slot_start + 30
        e_str = f"{e_min // 60:02d}:{e_min % 60:02d}"

        # Check overlap with any booking
        free = all(
            _hhmm_to_min(b["end"]) <= slot_start or
            _hhmm_to_min(b["start"]) >= e_min
            for b in booked
        )
        if free:
            slots.append({"start": s_str, "end": e_str})
        slot_start += 30

    return slots


def _room_with_status(room: dict) -> dict:
    cur = _current_booking(room["id"])
    nxt = _next_booking(room["id"])
    return {
        **room,
        "status":  "booked" if cur else "available",
        "current": cur,
        "next":    nxt,
    }


# ─── Routes ──────────────────────────────────────────────────────────────────

@app.route("/api/buildings")
def get_buildings():
    result = []
    for bid, bdata in BUILDINGS.items():
        rooms = [r for r in ROOMS.values() if r["building"] == bid]
        available = sum(1 for r in rooms if not _current_booking(r["id"]))
        result.append({
            "id":        bid,
            "name":      bdata["name"],
            "floors":    bdata["floors"],
            "rooms":     len(rooms),
            "available": available,
        })
    return jsonify(result)


@app.route("/api/rooms")
def get_rooms():
    building = request.args.get("building")
    rooms = [r for r in ROOMS.values()
             if not building or r["building"] == building]
    return jsonify([_room_with_status(r) for r in rooms])


@app.route("/api/rooms/<room_id>")
def get_room(room_id):
    room = ROOMS.get(room_id)
    if not room:
        abort(404, description=f"Room {room_id} not found")
    date_str = request.args.get("date", date.today().isoformat())
    data = _room_with_status(room)
    data["schedule"] = _bookings_for_room_date(room_id, date_str)
    data["slots"]    = _available_slots(room_id, date_str)
    return jsonify(data)


@app.route("/api/rooms/<room_id>/slots")
def get_slots(room_id):
    if room_id not in ROOMS:
        abort(404, description=f"Room {room_id} not found")
    date_str = request.args.get("date", date.today().isoformat())
    return jsonify({
        "room_id": room_id,
        "date":    date_str,
        "slots":   _available_slots(room_id, date_str),
    })


@app.route("/api/bookings", methods=["POST"])
def create_booking():
    body = request.get_json(silent=True) or {}
    required = ("room_id", "title", "organizer", "date", "start", "end")
    missing = [k for k in required if not body.get(k)]
    if missing:
        abort(400, description=f"Missing fields: {', '.join(missing)}")

    room_id = body["room_id"]
    if room_id not in ROOMS:
        abort(404, description=f"Room {room_id} not found")

    date_str = body["date"]
    start_min = _hhmm_to_min(body["start"])
    end_min   = _hhmm_to_min(body["end"])

    if start_min >= end_min:
        abort(400, description="start must be before end")
    if start_min < 8 * 60 or end_min > 18 * 60:
        abort(400, description="Bookings allowed between 08:00 and 18:00")

    # Conflict check
    for b in _bookings_for_room_date(room_id, date_str):
        bs = _hhmm_to_min(b["start"])
        be = _hhmm_to_min(b["end"])
        if start_min < be and end_min > bs:
            abort(409, description=f"Conflicts with: {b['title']} {b['start']}-{b['end']}")

    bid = _make_booking_id()
    booking = {
        "id":        bid,
        "room_id":   room_id,
        "room_name": ROOMS[room_id]["name"],
        "building":  ROOMS[room_id]["building"],
        "date":      date_str,
        "start":     body["start"],
        "end":       body["end"],
        "title":     body["title"],
        "organizer": body["organizer"],
        "seeded":    False,
    }
    _bookings[bid] = booking
    return jsonify(booking), 201


@app.route("/api/bookings/<booking_id>", methods=["DELETE"])
def cancel_booking(booking_id):
    if booking_id not in _bookings:
        abort(404, description=f"Booking {booking_id} not found")
    booking = _bookings.pop(booking_id)
    return jsonify({"cancelled": True, "booking": booking})


@app.route("/api/bookings")
def list_bookings():
    room_id  = request.args.get("room_id")
    date_str = request.args.get("date", date.today().isoformat())
    result = [b for b in _bookings.values()
              if b["date"] == date_str and
              (not room_id or b["room_id"] == room_id)]
    return jsonify(sorted(result, key=lambda b: (b["room_id"], b["start"])))


# ─── Error handlers ───────────────────────────────────────────────────────────

@app.errorhandler(400)
@app.errorhandler(404)
@app.errorhandler(409)
def http_error(e):
    return jsonify({"error": str(e.description)}), e.code


if __name__ == "__main__":
    print("Sansiri Mock Booking API")
    print(f"  Rooms   : {len(ROOMS)} across {len(BUILDINGS)} buildings")
    print(f"  Bookings: {len(_bookings)} seeded for today")
    print("  Listening on http://0.0.0.0:5000")
    app.run(host="0.0.0.0", port=5000, debug=False)

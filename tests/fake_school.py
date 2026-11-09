#!/usr/bin/env python3
"""A fake Canvas and Blackbaud SKY API with made-up data, for testing
`omamastery setup` end to end without a real school. Every name here is
invented. Usage: fake_school.py PORT"""

import json
import sys
from datetime import date, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

CANVAS_TOKEN = "fake-canvas-token"
SKY_TOKEN = "fake-sky-token"
SKY_KEY = "fake-subscription-key"

COURSES = [
    {"id": 101, "name": "AP Computer Science Principles - Block 1A", "course_code": "CSP",
     "sis_course_id": "BB-5001", "total_students": 3, "sections": [{"id": 1, "name": "CSP 1A"}]},
    {"id": 102, "name": "Robotics", "course_code": "ROBO", "sis_course_id": None, "total_students": 3,
     "sections": [{"id": 2, "name": "Robotics"}]},
    {"id": 103, "name": "Sandbox: Mr. Example", "course_code": "SBX", "total_students": 0, "sections": []},
]
STUDENTS = {
    "101": [{"id": 1, "name": "Mara Quill", "short_name": "Mara", "sortable_name": "Quill, Mara"},
            {"id": 2, "name": "Teo Brandt", "short_name": "Teo", "sortable_name": "Brandt, Teo"},
            {"id": 3, "name": "Ines Volk", "short_name": "Ines", "sortable_name": "Volk, Ines"}],
    "102": [{"id": 4, "name": "Sam Arden", "short_name": "Sam", "sortable_name": "Arden, Sam"},
            {"id": 5, "name": "Sam Berry", "short_name": "Sam", "sortable_name": "Berry, Sam"},
            {"id": 6, "name": "Lio Park", "short_name": "Lio", "sortable_name": "Park, Lio"}],
}
OUTCOMES = {
    "101": [
        {"outcome_group": {"title": "AP Computer Science Principles - Block 1A"},
         "outcome": {"id": 11, "title": "CSP.VAR", "description": "<p>Store and update program state in variables</p>"}},
        {"outcome_group": {"title": "Algorithms"},
         "outcome": {"id": 12, "title": "CSP.LOOP", "description": "<p>Repeat a step until a condition is met</p>"}},
        {"outcome_group": {"title": "Data"},
         "outcome": {"id": 13, "title": "CSP.LIST", "display_name": "Traverse a list"}},
    ],
}
MODULES = {
    "101": [{"id": 1, "name": "Unit 1: Programs", "unlock_at": "2026-08-24T07:00:00Z", "items": []}],
    "102": [
        {"id": 2, "name": "Start Here", "items": [{"title": "Welcome", "type": "Page"}]},
        {"id": 3, "name": "Unit 1: Build a drivetrain", "unlock_at": None,
         "items": [{"title": "Drivetrain build", "type": "Assignment", "content_id": 31},
                   {"title": "Gear ratios quiz", "type": "Quiz", "content_id": 77}]},
        {"id": 4, "name": "Unit 2: Sensors", "unlock_at": "2026-10-05T07:00:00Z",
         "items": [{"title": "Sensors", "type": "SubHeader"}, {"title": "Line follower", "type": "Assignment", "content_id": 32}]},
    ],
}
# CSP has only non-mastery work, so its objectives fall back to Outcomes.
ASSIGNMENT_GROUPS = {
    "101": [{"id": 1, "name": "Non-Mastery Assignments", "position": 1,
             "assignments": [{"id": 21, "name": "Syllabus quiz", "due_at": "2026-08-28T06:59:00Z"}]}],
    "102": [{"id": 2, "name": "Mastery Checks", "position": 1,
             "assignments": [{"id": 31, "name": "Drivetrain build", "position": 1, "due_at": "2026-09-17T06:59:00Z"},
                             {"id": 32, "name": "Line follower", "position": 2, "due_at": "2026-10-09T06:59:00Z"},
                             {"id": 34, "name": "Final robot", "position": 4, "due_at": None}]},
            {"id": 3, "name": "Labs", "position": 2,
             "assignments": [{"id": 33, "name": "Gear ratios quiz", "quiz_id": 77, "position": 1,
                              "due_at": "2026-09-10T06:59:00Z"}]},
            {"id": 4, "name": "non-mastery assignments", "position": 3,
             "assignments": [{"id": 35, "name": "Homework 1", "due_at": "2026-09-03T06:59:00Z"}]}],
}
SECTIONS = [
    {"id": 5001, "course_title": "AP Computer Science Principles", "section_identifier": "1A"},
    {"id": 5002, "course_title": "Robotics", "section_identifier": "3B"},
    {"id": 5003, "course_title": "Advisory", "section_identifier": "ADV"},
]
SKY_STUDENTS = {"5003": [{"id": 801, "first_name": "Rory", "last_name": "Fenn"}]}
# Section → (weekdays, block, start, end, room). Monday = 0.
MEETS = {5001: ([0, 2], "Block 1", "08:00", "09:25", "Lab 204"),
         5002: ([1, 3], "Block 3", "11:40", "13:05", "Room 110"),
         5003: ([0, 1, 2, 3], "Advisory", "10:00", "10:30", "Room 9")}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send(self, status, body, link=None):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        if link:
            self.send_header("Link", f'<{link}>; rel="next"')
        self.end_headers()
        self.wfile.write(data)

    def page(self, items, query):
        # Two per page, to exercise Link: rel="next" pagination.
        n = int(query.get("page", ["1"])[0])
        chunk = items[(n - 1) * 2:n * 2]
        link = None
        if n * 2 < len(items):
            q = {k: v for k, v in query.items() if k != "page"}
            q["page"] = [str(n + 1)]
            parts = "&".join(f"{k}={v}" for k, vs in q.items() for v in vs)
            link = f"http://{self.headers['Host']}{urlparse(self.path).path}?{parts}"
        self.send(200, chunk, link)

    def do_GET(self):
        url = urlparse(self.path)
        q = parse_qs(url.query)
        p = url.path.rstrip("/").split("/")
        auth = self.headers.get("Authorization", "")
        if url.path.startswith("/api/v1/"):
            if auth != f"Bearer {CANVAS_TOKEN}":
                return self.send(401, {"errors": [{"message": "Invalid access token."}]})
            rest = p[3:]
            if rest == ["courses"]:
                assert q.get("enrollment_type") == ["teacher"]
                return self.page(COURSES, q)
            cid = rest[1]
            if rest[2:] == ["users"]:
                assert q.get("enrollment_type[]") == ["student"]
                return self.page(STUDENTS.get(cid, []), q)
            if rest[2:] == ["outcome_group_links"]:
                if cid not in OUTCOMES:
                    return self.send(404, {"errors": [{"message": "not found"}]})
                return self.send(200, OUTCOMES[cid])
            if rest[2:] == ["modules"]:
                return self.send(200, MODULES.get(cid, []))
            if rest[2:] == ["assignment_groups"]:
                assert q.get("include[]") == ["assignments"]
                return self.send(200, ASSIGNMENT_GROUPS.get(cid, []))
            if len(rest) == 2:
                return self.send(200, {"id": int(cid), "syllabus_body": "<h2>About</h2><p>We build &amp; test.</p><ul><li>Labs</li></ul>"})
        if url.path.startswith("/school/v1/"):
            if auth != f"Bearer {SKY_TOKEN}" or self.headers.get("Bb-Api-Subscription-Key") != SKY_KEY:
                return self.send(401, {"message": "Access denied"})
            rest = p[3:]
            if rest == ["users", "me"]:
                return self.send(200, {"id": 9001})
            if rest == ["years"]:
                return self.send(200, {"value": [
                    {"school_year_label": "2025 - 2026", "current_year": False, "begin_date": "2025-08-01"},
                    {"school_year_label": "2026 - 2027", "current_year": True, "begin_date": "2026-08-01"}]})
            if rest == ["academics", "teachers", "9001", "sections"]:
                assert q.get("school_year") == ["2026 - 2027"]
                return self.send(200, {"count": 3, "value": SECTIONS})
            if rest[:2] == ["academics", "sections"] and rest[3:] == ["students"]:
                return self.send(200, {"value": SKY_STUDENTS.get(rest[2], [])})
            if rest == ["schedules", "meetings"]:
                start = date.fromisoformat(q["start_date"][0])
                end = date.fromisoformat(q["end_date"][0])
                wanted = {int(s) for s in q.get("section_ids", [""])[0].split(",") if s}
                rows, d = [], start
                while d <= end:
                    for sid, (days, block, t0, t1, room) in MEETS.items():
                        if sid in wanted and d.weekday() in days:
                            rows.append({"section_id": sid, "block_name": block, "room_name": room,
                                         "meeting_date": d.isoformat() + "T00:00:00",
                                         "start_time": f"1900-01-01T{t0}:00-05:00", "end_time": f"1900-01-01T{t1}:00-05:00"})
                    d += timedelta(days=1)
                return self.send(200, {"count": len(rows), "value": rows})
        self.send(404, {"message": "no such fake route"})


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()

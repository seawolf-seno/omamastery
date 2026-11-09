#!/bin/bash
# End-to-end test of `omamastery setup` against tests/fake_school.py (made-up
# data only). Runs in a scratch home: never touches ~/Classes or the keyring.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-$ROOT/build/omamastery}"
WORK="$(mktemp -d)"
PORT=$((20000 + RANDOM % 20000))
trap 'kill $SERVER 2>/dev/null || true; rm -rf "$WORK"' EXIT

python3 -I "$ROOT/tests/fake_school.py" "$PORT" &
SERVER=$!
for _ in $(seq 50); do (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null && break; sleep 0.1; done

export OMAMASTERY_HOME="$WORK/Classes" XDG_CONFIG_HOME="$WORK/config" OMAMASTERY_NO_KEYRING=1
export OMAMASTERY_CANVAS_URL="http://127.0.0.1:$PORT" OMAMASTERY_SKY_API="http://127.0.0.1:$PORT"
export OMAMASTERY_CANVAS_TOKEN=fake-canvas-token OMAMASTERY_BLACKBAUD_TOKEN=fake-sky-token
mkdir -p "$XDG_CONFIG_HOME/omamastery"
echo 'blackbaud_subscription_key = "fake-subscription-key"' > "$XDG_CONFIG_HOME/omamastery/connect.toml"

fail() { echo "FAIL: $*" >&2; exit 1; }
has() { grep -qF -- "$2" "$1" || fail "$1 lacks: $2"; }
lacks() { ! grep -qF -- "$2" "$1" || fail "$1 has: $2"; }

out="$("$BIN" setup --yes)"
echo "$out"
for name in Mara Teo Ines Sam Lio Rory; do
  [[ $out != *"$name"* ]] || fail "setup printed a student name ($name)"
done

C="$OMAMASTERY_HOME"
[[ -f $C/schedule.toml && -f $C/AGENTS.md ]] || fail "no schedule.toml or AGENTS.md"
has "$C/AGENTS.md" "Never open \`roster.toml\`"

csp="$C/ap-computer-science-principles-block-1a"
robo="$C/robotics"
[[ -d $csp && -d $robo ]] || fail "class folders: $(ls "$C")"
[[ ! -d $C/sandbox-mr-example && ! -d $C/advisory ]] || fail "unticked classes were set up"

# CSP: Canvas Outcomes, matched to Blackbaud by SIS id; day/block/room from meetings.
has "$csp/class.toml" 'day = "A"'
has "$csp/class.toml" 'block = 1'
has "$csp/class.toml" 'room = "Lab 204"'
has "$csp/class.toml" 'sample = false'
has "$csp/class.toml" 'canvas_course = "101"'
has "$csp/class.toml" 'blackbaud_section = "5001"'
lacks "$csp/class.toml" 'Mara'
has "$csp/roster.toml" 'name = "Mara"'
[[ $(stat -c %a "$csp/roster.toml") == 600 ]] || fail "roster.toml isn't private"
[[ $(stat -c %a "$csp/mastery.csv") == 600 ]] || fail "mastery.csv isn't private"
has "$csp/plan.toml" 'title = "Store and update program state in variables"'
has "$csp/plan.toml" 'idea = "Algorithms"'
has "$csp/plan.toml" 'title = "Traverse a list"'
has "$csp/ideas.toml" 'name = "Data"'
has "$csp/syllabus.md" '- Labs'
has "$csp/syllabus.md" 'We build & test.'

# Robotics: objectives are its assignments outside "Non-Mastery Assignments"
# (any capitalization), in the week each is due, undated last; matched to
# Blackbaud by name; two Sams told apart.
has "$robo/class.toml" 'day = "B"'
has "$robo/class.toml" 'block = 3'
has "$robo/class.toml" 'room = "Room 110"'
has "$robo/roster.toml" 'name = "Sam A."'
has "$robo/roster.toml" 'name = "Sam B."'
titles="$(grep '^title' "$robo/plan.toml" | tr '\n' '|')"
[[ $titles == 'title = "Gear ratios quiz"|title = "Drivetrain build"|title = "Line follower"|title = "Final robot"|' ]] \
  || fail "robotics objectives: $titles"
has "$robo/plan.toml" 'week = "2026-09-07"'
has "$robo/plan.toml" 'week = "2026-09-14"'
has "$robo/plan.toml" 'week = "2026-10-05"'
has "$robo/plan.toml" 'From your Canvas assignments'
lacks "$robo/plan.toml" 'Homework 1'
lacks "$robo/plan.toml" 'Unit 1'
has "$robo/canvas-plan.md" 'Gear ratios quiz (Quiz, due 2026-09-09)'

# Starting levels from Canvas grades (out of 10: 10 Extending, 9 Proficient,
# 8 Developing, 7 Beginning, less Not yet); a level word wins; ungraded stays blank.
has "$robo/plan.toml" 'canvas_assignment = "31"'
has "$robo/mastery.csv" '2026-09-18,Sam A.,drivetrain-build,Extending,From Canvas: 10 of 10'
has "$robo/mastery.csv" '2026-10-10,Sam A.,line-follower,Proficient,From Canvas: 9.5 of 10'
has "$robo/mastery.csv" '2026-09-18,Sam B.,drivetrain-build,Beginning,From Canvas: 7 of 10'
has "$robo/mastery.csv" '2026-09-11,Sam B.,gear-ratios-quiz,Developing,From Canvas: 3 of 4'
has "$robo/mastery.csv" '2026-09-19,Lio,drivetrain-build,Not yet,From Canvas: 0 of 10'
lacks "$robo/mastery.csv" 'Lio,line-follower'
[[ $(grep -c 'From Canvas' "$robo/mastery.csv") == 5 ]] || fail "starting levels: $(cat "$robo/mastery.csv" | wc -l) lines"
has "$C/schedule.toml" '[score_levels]'
[[ $out == *"robotics: 3 students, 4 objectives, B3, room Room 110, 5 starting levels from Canvas grades"* ]] || fail "report: $out"
has "$robo/canvas-plan.md" '## Unit 2: Sensors (week of 2026-10-05)'
lacks "$csp/plan.toml" 'Syllabus quiz'

# Bell times for the blocks Blackbaud knows replace placeholders; the other
# blocks stay, still marked as placeholders.
has "$C/schedule.toml" '{ number = 1, start = "08:00", end = "09:25" }'
has "$C/schedule.toml" '{ number = 2, start = "09:35", end = "11:00" }'
has "$C/schedule.toml" '{ number = 3, start = "11:40", end = "13:05" }'
has "$C/schedule.toml" 'Blocks 1, 3 from Blackbaud'
[[ $(grep -c 'Bell schedule' "$C/schedule.toml") == 1 ]] || fail "bell note repeated"

# OmaClass reads students (from roster.toml) and rooms.
json="$("$BIN" classes --json)"
[[ $json == *'"room":"Lab 204"'* && $json == *'"Mara"'* ]] || fail "classes --json: $json"

# Running setup again refreshes rosters and keeps the teacher's plan.
# Running setup again never changes a level that's set.
echo '2026-11-01,Sam B.,drivetrain-build,Proficient,Set in OmaMastery' >> "$robo/mastery.csv"

# Running setup again also keeps names the teacher changed in roster.toml.
echo '# my edit' >> "$robo/plan.toml"
sed -i 's/name = "Sam A."/name = "Sammy"/' "$robo/roster.toml"
"$BIN" setup --yes >/dev/null
has "$robo/plan.toml" '# my edit'
has "$robo/roster.toml" 'name = "Sammy"'
lacks "$robo/roster.toml" 'name = "Sam A."'
# Sam B.'s level set by the teacher stays; nobody whose name is unchanged gets
# new rows. (Sam A. was renamed, so "Sammy" starts with empty cells and is seeded.)
[[ $(grep -c '^[^,]*,Sam B\.,' "$robo/mastery.csv") == 3 ]] || fail "rerun touched Sam B."
[[ $(grep -c '^[^,]*,Lio,' "$robo/mastery.csv") == 1 ]] || fail "rerun touched Lio"
[[ $(tail -n 1 "$robo/mastery.csv" | cut -d, -f2) == "Sammy" ]] || fail "rerun: $(tail -n 2 "$robo/mastery.csv")"
[[ $(find "$C" -mindepth 1 -maxdepth 1 -type d | wc -l) == 2 ]] || fail "rerun made new folders: $(ls "$C")"
[[ $(grep -c 'Bell schedule' "$C/schedule.toml") == 1 ]] || fail "bell note repeated on rerun"

# A bad token is reported plainly.
if OMAMASTERY_CANVAS_TOKEN=wrong "$BIN" setup --yes >"$WORK/bad" 2>&1; then fail "bad token accepted"; fi
has "$WORK/bad" "didn't accept that token"

# Refuses to write real names into a git repository.
mkdir -p "$WORK/repo/.git"
if OMAMASTERY_HOME="$WORK/repo/Classes" "$BIN" setup --yes >"$WORK/git" 2>&1; then fail "wrote into a git repo"; fi
has "$WORK/git" "inside a git repository"

echo "setup e2e: all checks passed"

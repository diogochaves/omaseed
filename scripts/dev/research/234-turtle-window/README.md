# 234-turtle-window

**Question (#234, DECISIONS C25 K20):** omaseed reads Transmission's turtle clock
(`alt_speed_time_begin/end/day/enabled`, minutes since midnight and a day mask, Sunday 1 …
Saturday 64) the way libtransmission 4.1's source reads: a window past midnight runs forwards
into the next day, on the day it began; begin == end is a whole day from begin; the daemon looks
every second and acts only when its own reading changes. Does a live daemon do that across a
boundary?

No real midnight is needed: each window is set relative to now, so "now" lands in the part
before midnight, the part after it (yesterday's window), or outside, and the day mask picks
today or yesterday.

```
scripts/dev/test-server.sh start --transmission
python3 -I scripts/dev/research/234-turtle-window/turtle_window.py
scripts/dev/test-server.sh stop --transmission   # if it wasn't running before
```

Each case first puts the turtle in the state opposite to omaseed's reading (a window surely
containing now, or surely not), so the daemon has to act; a state that doesn't move is watched
75 s, past a minute. The script writes the daemon's `alt_speed_*` settings back at the end.
Run it at least 130 minutes away from midnight.

## What it printed

Transmission 4.1.3 (838877323f), RPC 6.0.1, 2026-11-06, a Friday:

```
Transmission 4.1.3 (838877323f), now Fri 13:19 (minute 799), today's bit 32, yesterday's 16
case                                                       begin   end mask  omaseed  daemon  took
wrap, now after midnight (yesterday's window), every day     859   829  127  on       on      0.0s
wrap, now after midnight, yesterday only                     859   829   16  on       on      0.0s
wrap, now after midnight, today only                         859   829   32  off      off     0.0s
wrap, now before midnight (today's window), every day        739   679  127  on       on      0.0s
wrap, now before midnight, today only                        739   679   32  on       on      0.0s
wrap, now before midnight, yesterday only                    739   679   16  off      off     0.0s
wrap, now outside it, every day                              859   739  127  off      off     0.0s
begin == end, before now, every day                          769   769  127  on       on      0.0s
begin == end, after now, yesterday only                      829   829   16  on       on      0.0s
begin == end, after now, today only                          829   829   32  off      off     0.0s
same day, now inside, today only                             769   829   32  on       on      0.0s
same day, now inside, every day but today                    769   829   95  off      off     0.0s
same day, now inside, no day                                 769   829    0  off      off     0.0s

by the clock                                               begin   end mask  omaseed  daemon
same day, begins next minute, every day                      801   830  127  off→on   off→on   moved +0.2s from the minute
wrap, yesterday's ends next minute, every day                861   802  127  on→off  on→off  moved +0.1s from the minute
begin == end next minute, yesterday only                     803   803   16  on→off  on→off  moved +0.0s from the minute
begin == end next minute, every day                          804   804  127  on→on   on→on   didn't move

turned off by hand inside the window: stays off (75s watched)
restored: {'alt_speed_enabled': False, 'alt_speed_time_begin': 540, 'alt_speed_time_day': 127, 'alt_speed_time_enabled': False, 'alt_speed_time_end': 1020}
```

So, on the daemon:

- A window past midnight runs forwards: on from begin on a picked day until end the next day,
  the hours after midnight counted to the day it began (Thursday's bit keeps Friday morning on;
  Friday's bit alone doesn't). Not qBittorrent's backwards days.
- Begin == end is a whole day from begin, not nothing; with every day picked the days meet and
  it never turns off.
- The mask picks the day a window begins; no day, never on.
- A new window takes at once (session_set), and a minute's edge within a fraction of a second.
- Its scheduler acts only when its own reading changes: turned off by hand inside the window,
  it stays off.

omaseed's `AltSchedule` (Transmission's rule) read every case the same way.

The tests that hold omaseed to it: `tests/core/tst_transmissiontranslate.cpp`
(`turtleWindowAsTheDaemonKeepsIt`, `turtleWindowAcrossAMinute`,
`turtleTurnedOffByHandInsideTheWindow`), each case read through the session the way omaseed
reads it.

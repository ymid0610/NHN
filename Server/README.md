# NHN Server

Arcade multiplayer backend: matchmaking, instance, chat and voice. C++20, IOCP,
CMake + Ninja + vcpkg, MSVC only.

No accounts and no login — a session *is* the identity, and it lasts exactly as
long as the socket.

## Layout

| Directory | What it is |
|---|---|
| `ServerCore/` | IOCP core: sessions, buffers, job queues, crash handler, hand-rolled serialisation |
| `Protocol/` | Packet definitions and the framing/dispatch layer |
| `Common/` | Shared server plumbing: bootstrap, control link, ticket table, rate limiter |
| `MatchServer/` | Rooms, search, host, kick, tickets, handoff to an instance |
| `ChatServer/` | Channel fan-out: global, per-room, per-instance |
| `VoiceServer/` | UDP relay (SFU). Never decodes audio |
| `InstanceServer/` | Live matches, and the game itself under `game/` |
| `TestClient/` | Console client. Every feature as a command, plus script replay |
| `Tests/` | gtest unit tests. No network |
| `scripts/` | Launcher scripts and end-to-end scenarios |

## Build

```bash
cmake --preset debug && cmake --build --preset debug
```

Needs vcpkg. It is found through `VCPKG_ROOT`, or at `C:/tools/vcpkg`,
`C:/vcpkg`, `C:/dev/vcpkg`, `%USERPROFILE%/vcpkg`. The only dependency is gtest.

Binaries land in `build/<preset>/bin`. Rider opens `CMakePresets.json` directly.

## Run

```bash
./scripts/run_dev.ps1
```

Starts the four servers and opens two interactive client windows. This is the
everyday driver — `-Clients 4` for a full quad room, `-NoServers` to attach to a
stack that is already up, `-Stop` to shut everything down.

To start only the servers:

```bash
./scripts/run_stack.ps1
```

The match server must come up first — the others dial *it* and announce their
own endpoints, so nothing needs to be configured with anyone else's address.

```bash
./scripts/run_stack.ps1 -Stop
```

### Why every process gets its own console

Nothing here redirects a long-lived process's output, and that is deliberate.

`Start-Process` can only redirect by going through `CreateProcess` *without*
`CREATE_NEW_CONSOLE`, so the child inherits the launching console and joins its
process group — `-WindowStyle Hidden` is quietly ignored on that path. When that
console goes away, which is exactly what happens if you double-click a `.ps1` or
use "Run with PowerShell", Windows sends `CTRL_CLOSE_EVENT` to everything
attached and the servers shut down as instructed. They appear to die instantly
for no reason.

Own console means they survive the launcher. Nothing is lost: each server
already writes `logs/<Name>_<timestamp>.log` itself.

For clients the same choice matters twice over — redirection breaks the prompt,
and it silently disables the wait-for-developer step on a crash, which only
triggers when stdin is a console.

`run_stack.ps1 -Hidden` restores capture-to-file for automation, where the
caller stays alive for the duration. That mode passes `--no-wait`; the default
does not, so a fatal error stops in that server's own window with the reason and
stack on screen.

### Ports

| Port | Server | Purpose |
|---|---|---|
| 7777 | Match | clients |
| 7787 | Match | clients, **WebSocket** |
| 7900 | Match | control plane (peer servers only) |
| 7800 | Chat | clients |
| 7810 | Chat | clients, **WebSocket** |
| 7870 | Voice | clients, **UDP** |
| 7850 | Instance | clients |
| 7860 | Instance | clients, **WebSocket** |

Each client-facing server listens twice. A browser cannot open a raw socket, so
the same session classes are offered a second time behind a WebSocket
handshake — the layer sits below the packet framer, so no handler differs and a
browser and a native client can share a match. See `Client/web/README.md`.

Voice has no WebSocket equivalent: it is UDP, which a browser cannot open at
all. The match server reports no voice endpoint to a browser rather than
handing out one it could never reach.

Client and control ports are separate so a misconfigured client can never reach
control-plane packets, and the two can be firewalled apart.

### Common switches

Every executable accepts these:

| Switch | Meaning |
|---|---|
| `--config=<path>` | settings file, default `<name>.cfg` |
| `--log-level=` | `trace`\|`debug`\|`info`\|`warn`\|`error` |
| `--log-dir=` | default `logs` |
| `--workers=` | IOCP worker threads, 0 = one per hardware thread |
| `--no-wait` | do not block for a developer on a fatal error |

## Test client

```bash
./build/debug/bin/TestClient.exe --nick=me
> help
```

Everything the servers expose is a command. `run <script>` replays a command
file, and `--script=<file>` runs one and exits — which is what makes a
four-player scenario a matter of launching four processes.

`expect <condition> [timeoutMs]` waits on real state rather than sleeping, so
scenarios do not turn into flaky timing guesses.

### Scenarios

```bash
./scripts/run_stack.ps1        # start the servers first
./scripts/run_quad_test.ps1    # 4 players: join, ready, start, instance, voice
```

| Script | What it does |
|---|---|
| `run_dev.ps1` | servers + N interactive client windows |
| `run_stack.ps1` | servers only |
| `run_quad_test.ps1` | 4 scripted clients, runs to completion and prints the transcript |

| Script | Covers |
|---|---|
| `smoke_room_rules.txt` | locked rooms, search filters, chat channels, start/kick refusals, rate limiting |
| `smoke_quick.txt` | quick match funnels four players into one room |
| `smoke_quick_overflow.txt` | six duo players become three full rooms, not six empty ones |
| `smoke_quick_locked_*.txt` | quick match refuses a locked room even as top candidate, which stays reachable by hand |
| `smoke_game_*.txt` | a full tic-tac-toe match, board logic only |
| `smoke_game_items_*.txt` | a gomoku match with every item enabled |
| `smoke_host.txt` + `smoke_join.txt` | full 4-player room to instance handoff, with voice |
| `smoke_kick_host.txt` + `smoke_kick_joiner.txt` | ready enforcement, kick, rejoin cooldown |
| `smoke_migrate_host.txt` + `smoke_migrate_joiner.txt` | host migration when the host leaves |
| `smoke_crash.txt` | the crash path |

## The game

A shooting gallery where the target is a board. A paper sheet zigzags down the
screen each wave; hitting a cell claims it, and completing a line takes the
round. Items arc past on parabolas and get in the way.

Everyone shoots the same pass at the same time — a round is a race, not a
sequence of turns. That is what makes the disruptive items worth having: a stun
or a paint can costs an opponent real seconds of a contest already in progress.

| Mode | Board | Line | Players | Ammo choices | Wave caps |
|---|---|---|---|---|---|
| `tictactoe` | 3×3 | 3 | 2 | 1, 2, 3, ∞ | 10, ∞ |
| `gomoku9` | 9×9 | 4 | 2–4 | 1, 3, 6, ∞ | 10, ∞ |
| `gomoku15` | 15×15 | 5 | 2–4 | 1, 3, 6, ∞ | 20, ∞ |

Mode is the room type: quick match is per mode, and a room's rules are fixed
when it is created. Everything else — rounds, ammo, wave cap, difficulty band,
which items are on — is a lobby setting the host controls.

Both per-mode columns exist for concrete reasons. A 3×3 board cannot absorb six
shots per player per wave; the first pass would decide the round. And on 15×15,
five in a row is statistically unreachable inside ten waves, so that mode gets a
larger cap rather than a warning the player has to understand.

Difficulty is **cell size**, redrawn each round from the configured band, so it
means the same thing whatever the board dimensions are.

### Items

| Item | Layer | Effect |
|---|---|---|
| Tumbleweed | **blocker** | absorbs the shot |
| Wanted poster | **target** | decoy: ammo spent, nothing else |
| Frying pan | item | stuns a random player for the rest of the wave |
| Balloon | item | shoves nearby entities, including the sheet |
| Paint can | item | blinds a random player through the *next* wave |
| Grenade | item | six further shots around it |
| Speed loader | item | sets the shooter's ammo back to full |

Neither a blocker nor a decoy carries a penalty beyond the wasted shot.

The pan and the paint can may land on the player who shot them. Their victim is
drawn on the server — letting clients roll would have each of them believe a
different player got hit.

Item availability narrows with the ammo rule: the pan is out at one shot each
(losing a wave would be the whole round) and the loader is out at one shot and
at unlimited (nothing to reload). The narrowed set is computed server-side and
sent to the lobby, so the UI and the spawner cannot disagree.

### Hit resolution

The client sends **where it aimed and when**. Nothing else.

```
C_Fire { aimX, aimY, clientTick, sequence }
```

Because the sheet is moving, a position alone does not identify what was hit —
so the shot carries the tick the client was displaying, and the server rewinds
to it. The world keeps 300 ms of history; a claim older than that is clamped
rather than honoured, so nobody can sit on a packet and take a cell that was
contested several frames ago.

Order of resolution is blockers, then items, then the sheet. That ordering comes
from three separate containers rather than a sortable field, which makes it
impossible to put a target in front of a blocker by getting a number wrong.

Contested cells go to **whoever's packet arrives first**. Resolving by client
tick instead would let a laggy player retroactively take a cell someone already
holds, and watching your own mark disappear is worse than losing the race.

A grenade's fragments are ordinary shots through the same pipeline, so "six
firing events" needed no separate machinery — just a depth cap.

### What this model cannot do

Sending only a position means the server can check that a shot was possible —
in bounds, in time, ammo available, not stunned, not too fast — but it cannot
tell a human from an aimbot. Perfect aim and good aim look identical.

That is inherent to the input format, and it is a reasonable trade for an arcade
game. It is worth being clear that client *authority* would be a much larger
concession: a client that reports its own hits can claim cells it never hit,
which in a 9×9 race is a single packet away from an instant win. Responsiveness
comes from prediction, not from authority — the client shows its own hit
immediately and the server confirms.

## Design notes

### Job queues instead of locks

Rooms, chat channels and instances each own a `JobQueue`. Every mutation runs as
a job, and one queue never runs two jobs at once — so inside a handler the
object can be treated as single-threaded. Kick, host migration and handoff all
touch several members at a time, and doing that under per-member locks is where
deadlocks come from.

### Tickets

The match server mints a ticket and **pushes it** to the target server before
telling the client where to connect. The receiving server validates by table
lookup and never asks the match server anything.

Tickets are plaintext, with no signature. That only holds up because they are
also:

- **unguessable** — 128 bits from `BCryptGenRandom`, never a sequential id
- **short-lived** — 30 seconds
- **single-use** — consuming one removes it

Drop any of those three and a ticket becomes a room number anyone can enumerate.

### Quick match

`room quick <mode>` drops a player into a joinable room of that mode, opening
one if nothing suitable exists. Rooms are otherwise ordinary — quick match only
automates finding one with space.

Candidates are ordered **fullest first**. Filling one room to capacity starts a
game; spreading players evenly leaves several rooms one player short and nobody
playing. Rooms already in or entering a match are skipped.

Locked rooms are skipped too — quick match has no password to offer — and that
holds twice over: they are filtered out of the candidate list, and a join
attempt with an empty password would be refused anyway, which the retry path
absorbs. A room's password is fixed at creation, so there is no window in which
an indexed room becomes locked. Skipping them does not make them unreachable:
they stay listed and joinable by hand with the password, exactly as before.
Auto-created quick-match rooms are always open, or the next player could not get
in.

The candidate list comes from the search index, which lags the rooms themselves,
so a room can fill up between being picked and the join actually running. Rather
than surfacing that race as "room full", a failed attempt moves on to the next
candidate; running out of candidates means opening a new room. The same path
absorbs a kick cooldown on one of the candidates.

### Channel membership is server-driven

A client names a channel *kind* (`global`/`room`/`instance`), never an id. The
match server pushes membership to chat and voice, so a client cannot address a
room it is not in. When a chat or voice server reconnects, the match server
replays the full membership list — a satellite restart is invisible to players
already sitting in rooms.

### Voice

A selective forwarder, not a mixer: an incoming frame is copied to every other
member of the speaker's channel with only the sender id rewritten. The server
never decodes audio.

The speaker is identified by **the address the datagram arrived from**, never by
the id in the header — otherwise anyone could impersonate any player.

A speaker is heard on the most specific channel they are in: their instance
while a match is running, otherwise their room. There is no global voice.

### Crash handling

Every abnormal-termination path Windows and the CRT offer routes into one
routine: print the reason, symbolise the stack, write a minidump, wait for a
developer, exit. `NHN_CHECK(cond, ...)` and `NHN_FATAL(...)` use the same path.

The wait is skipped automatically when stdin is not a console, so redirected and
service-hosted runs cannot wedge. `--no-wait` disables it explicitly.

Try it: `crash` in the test client.

## Playing without a client

There is no Unity client yet, so the test client is the only way to exercise the
game — which means it has to be able to play it with no renderer at all.

It keeps the board and the last world snapshot, and aims from those:

```
fire cell 40        aim at a board cell where it was last seen
fire item grenade   aim at an item
fire 960 540        raw point in the field
autoplay on         keep shooting at unclaimed cells and the occasional item
board               print the grid
```

`fire cell` does exactly what a real client's aiming would: it works out where
the cell was in the most recent snapshot and sends **that snapshot's tick**. The
server rewinds to that moment to judge the shot. So the test client is not a
special case — it exercises the real lag-compensation path.

## Not implemented

- **Presentation.** The server decides everything that affects the outcome and
  sends the rest as an event plus a seed: paint splatter shape, ricochet arcs,
  muzzle flashes and camera shake are the client's to draw.
- **Tournament modes.** Deferred. The 8- and 16-player brackets would be rows in
  the mode table plus a bracket object inside the instance holding several
  concurrent boards.
- **Real audio.** The servers relay opaque payloads and do not care what is in
  them. Capture, Opus encoding, jitter buffering and playback are the client's
  job. The test client sends synthetic frames at the real rate and counts what
  comes back, which tests routing — the thing that can actually break server
  side.
- **Unity client.** `Client/Assets` is untouched. There is a working browser
  client under `Client/web` — worth keeping even once Unity exists, since a
  second independent implementation of the wire format is what catches protocol
  ambiguities.

## Known limits

- The match server is a single point of failure, and holds all room state in
  memory. Restarting it loses every room.
- Room passwords travel in the clear (no TLS anywhere). Acceptable for an arcade
  door code; do not reuse the mechanism for anything else.
- IPv4 only.
- Global chat fans out linearly. Fine into the low thousands; past that it needs
  sharding.

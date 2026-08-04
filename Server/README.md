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
| `InstanceServer/` | Live matches. Game logic is a stub |
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
| 7900 | Match | control plane (peer servers only) |
| 7800 | Chat | clients |
| 7870 | Voice | clients, **UDP** |
| 7850 | Instance | clients |

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
| `smoke_host.txt` + `smoke_join.txt` | full 4-player room to instance handoff, with voice |
| `smoke_kick_host.txt` + `smoke_kick_joiner.txt` | ready enforcement, kick, rejoin cooldown |
| `smoke_migrate_host.txt` + `smoke_migrate_joiner.txt` | host migration when the host leaves |
| `smoke_crash.txt` | the crash path |

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

## Not implemented

- **Game logic.** `IGameMode` in `InstanceServer/include/instance/Instance.h` is
  where it goes; `NullGameMode` is the current placeholder. Instance lifecycle,
  handoff, tick loop and teardown are done.
- **Real audio.** The servers relay opaque payloads and do not care what is in
  them. Capture, Opus encoding, jitter buffering and playback are the client's
  job. The test client sends synthetic frames at the real rate and counts what
  comes back, which tests routing — the thing that can actually break server
  side.
- **Unity client.** `Client/` is untouched.

## Known limits

- The match server is a single point of failure, and holds all room state in
  memory. Restarting it loses every room.
- Room passwords travel in the clear (no TLS anywhere). Acceptable for an arcade
  door code; do not reuse the mechanism for anything else.
- IPv4 only.
- Global chat fans out linearly. Fine into the low thousands; past that it needs
  sharding.

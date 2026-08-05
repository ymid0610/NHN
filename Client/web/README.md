# Web client

A single-page client for the same servers everything else talks to. Useful for
seeing the game before the Unity client exists, and useful afterwards as a
second, independent implementation of the wire format — the kind of thing that
finds protocol ambiguities a single implementation never notices.

## Running

```bash
cd Server && ./scripts/run_stack.ps1     # the servers
python -m http.server 8080 --directory Client/web
```

Then open <http://localhost:8080>.

## Letting someone else connect

They need **no files at all** — a browser and a URL. That is the whole point of
this client.

On your machine:

```powershell
# 1. tell the satellite servers what address to advertise
cd Server; .\scripts\run_stack.ps1 -PublicHost <your-ip>

# 2. serve the page on all interfaces (the default for http.server)
python -m http.server 8080 --directory Client/web

# 3. allow the ports through the firewall (run as administrator, once)
New-NetFirewallRule -DisplayName "TickTackBang" -Direction Inbound `
  -Protocol TCP -LocalPort 8080,7787,7810,7860 -Action Allow
```

Then send them `http://<your-ip>:8080`.

`-PublicHost` is the step that is easy to miss and hard to diagnose. The chat and
instance servers register their own address with the match server, which passes
it to clients. Left at the default they advertise `127.0.0.1`, so a remote
browser is told to connect to *itself*: the lobby works, and chat and the game
then fail with nothing to explain why. The client detects that specific case and
warns, but fixing it at the source is better.

No domain is needed. If you are behind a router and your friend is not on the
same network, the simplest option is a mesh VPN such as Tailscale — both
machines get a stable address and everything works exactly as it does on a LAN.

Avoid HTTPS tunnels (ngrok, Cloudflare Tunnel) unless you are prepared for the
consequence: a page served over `https://` cannot open a `ws://` socket, so you
would have to tunnel all four ports with TLS and switch the client to `wss://`.

## Why the server needed changing

A browser cannot open a raw TCP or UDP socket, so it could not speak to these
servers at all as they were. The fix was to add a WebSocket listener to
`ServerCore`, sitting **below** the packet framer:

```
Session (raw bytes)
  └ WebSocket handshake + frames    ← the only new layer
       └ [size][id][payload] framing and dispatch   (unchanged)
```

Room, chat and game handlers are untouched — a browser and a native client
differ only in transport, and can play in the same match. Each server therefore
listens twice:

| Server | native | browser |
|---|---|---|
| Match | 7777 | **7787** |
| Chat | 7800 | **7810** |
| Instance | 7850 | **7860** |

The match server hands out whichever port the asking connection can reach, so
nothing in the client has to know which transport it is on.

**Voice is not available here.** It is UDP, which a browser cannot open at all;
reaching it would mean WebRTC, which is a different project. The match server
reports no voice endpoint to a browser rather than handing out one it could
never use.

## Files

| | |
|---|---|
| `protocol.js` | wire format, framing and the WebSocket connection |
| `app.js` | screens, packet handling, canvas rendering |
| `index.html` | markup and styling |

## Keeping the format in step

`protocol.js` is a **second hand-written implementation** of the byte layout in
`Server/Protocol`. Nothing generates it, so adding a field on one side and not
the other produces silent corruption rather than an error. The read and write
helpers are kept in the same field order as the C++ `Serialize` methods so the
two can be diffed by eye.

If this becomes a source of bugs, the fix is golden vectors: have the C++ test
binary emit the bytes for every packet and have a JS test assert it decodes them
identically. That catches drift without introducing a code generator.

## Debug hooks

The page exposes a few globals on purpose — it is a test client, and nothing
there is hidden from a player anyway:

```js
game                 // full client state
cellPoint(index)     // where a board cell is right now, plus the tick
fireAt(x, y)         // fire at a field coordinate
paint()              // draw one frame without waiting for requestAnimationFrame
```

## Aiming, and why there is no interpolation

The latest snapshot is drawn exactly as received, with no smoothing between
frames. That is deliberate: a shot carries the tick of the snapshot it was aimed
with, and the server rewinds to precisely that moment. Interpolating would mean
aiming at a position that never existed server-side, and near a cell edge that
is the difference between a hit and a miss.

The cost is that motion is visibly stepped at the 20 Hz snapshot rate. A real
client would interpolate for display and keep aiming against the last true
snapshot; this one keeps them the same for clarity.

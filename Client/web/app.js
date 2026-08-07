import * as P from './protocol.js';
import { Art, loadArt } from './assets.js';

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const S = {
  host: location.hostname || '127.0.0.1',
  nickname: 'web' + Math.floor(Math.random() * 900 + 100),
  match: null, chat: null, inst: null,
  sessionId: 0, mySlot: 0xFF,
  screen: 'connect',
  rooms: [], room: null, config: null, effectiveItems: 0,
  board: null, cells: [], players: [], scores: [],
  entities: [], snapshotTick: 0,
  ammo: 0, roundIndex: 0, roundCount: 0, paperSize: 0,
  waveActive: false, gameOver: false, stunned: false, blinded: false,
  fireSeq: 1,
};

const view = document.getElementById('view');
const logEl = document.getElementById('log');

const log = (text, cls = '') => {
  const line = document.createElement('div');
  if (cls) line.className = cls;
  line.textContent = text;
  logEl.appendChild(line);
  logEl.scrollTop = logEl.scrollHeight;
  while (logEl.childElementCount > 300) logEl.removeChild(logEl.firstChild);
};

const isHost = () => S.room && S.room.hostSessionId === S.sessionId;

function header() {
  document.getElementById('hdrState').textContent = S.screen;
  document.getElementById('hdrSession').textContent =
    S.sessionId ? `session ${S.sessionId} · ${S.nickname}` : '';
  document.getElementById('hdrRoom').textContent =
    S.room ? `room ${S.room.roomId} "${S.room.name}"${isHost() ? ' (host)' : ''}` : '';
  document.getElementById('hdrAmmo').textContent =
    S.screen === 'game'
      ? `round ${S.roundIndex + 1}/${S.roundCount} · ammo ${S.ammo === 0 ? '∞' : S.ammo}` +
        (S.stunned ? ' · STUNNED' : '') + (S.blinded ? ' · BLINDED' : '')
      : '';
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

function setScreen(name) {
  S.screen = name;
  render();
}

function render() {
  header();
  const draw = { connect: screenConnect, lobby: screenLobby, room: screenRoom, game: screenGame };
  view.replaceChildren();
  (draw[S.screen] ?? screenConnect)();
}

const el = (tag, props = {}, ...children) => {
  const node = Object.assign(document.createElement(tag), props);
  for (const child of children.flat()) {
    if (child != null) node.append(child.nodeType ? child : String(child));
  }
  return node;
};

function screenConnect() {
  const host = el('input', { value: S.host });
  const nick = el('input', { value: S.nickname });
  const go = el('button', { className: 'primary', textContent: 'connect' });
  go.onclick = () => { S.host = host.value.trim(); S.nickname = nick.value.trim(); connectMatch(); };

  view.append(
    el('div', { className: 'row' }, el('label', {}, 'host'), host,
       el('label', {}, 'nickname'), nick, go),
    el('p', { className: 'dim' },
       'The browser cannot open a raw socket, so this connects over WebSocket ' +
       '(match 7787, chat 7810, instance 7860). Voice is UDP and therefore not ' +
       'available here.'));
}

function screenLobby() {
  const name = el('input', { value: 'web room' });
  const password = el('input', { placeholder: 'password (optional)' });
  const mode = el('select', {}, ...Object.entries(P.MODE_NAMES).map(
    ([value, label]) => el('option', { value, textContent: label })));
  mode.value = String(P.GameMode.TicTacToe);

  const create = el('button', { className: 'primary', textContent: 'create' });
  create.onclick = () => send(S.match, new P.Writer(P.PacketId.C_RoomCreate)
    .str(name.value).u8(Number(mode.value)).str(password.value));

  const quick = el('button', { textContent: 'quick match' });
  quick.onclick = () => send(S.match, new P.Writer(P.PacketId.C_QuickMatch).u8(Number(mode.value)));

  const refresh = el('button', { textContent: 'refresh' });
  refresh.onclick = requestRoomList;

  const rows = S.rooms.map((room) => {
    const tr = el('tr', { className: 'clickable' },
      el('td', {}, `#${room.roomId}`),
      el('td', {}, room.name),
      el('td', {}, P.MODE_NAMES[room.mode] ?? '?'),
      el('td', {}, `${room.memberCount}/${room.capacity}`),
      el('td', {}, room.hasPassword ? '🔒' : ''),
      el('td', { className: 'dim' }, room.hostNickname));
    tr.onclick = () => {
      const pw = room.hasPassword ? (prompt(`password for "${room.name}"`) ?? '') : '';
      send(S.match, new P.Writer(P.PacketId.C_RoomJoin).u64(room.roomId).str(pw));
    };
    return tr;
  });

  view.append(
    el('div', { className: 'row' }, mode, name, password, create, quick, refresh),
    el('table', {},
      el('thead', {}, el('tr', {},
        el('th', {}, 'id'), el('th', {}, 'name'), el('th', {}, 'mode'),
        el('th', {}, 'players'), el('th', {}, ''), el('th', {}, 'host'))),
      el('tbody', {}, ...(rows.length ? rows
        : [el('tr', {}, el('td', { colSpan: 6, className: 'dim' }, 'no rooms'))]))));
}

function screenRoom() {
  const room = S.room;
  if (!room) return setScreen('lobby');

  const rows = room.members.map((m) => {
    const kick = el('button', { textContent: 'kick', disabled: !isHost() || m.sessionId === S.sessionId });
    kick.onclick = () => send(S.match, new P.Writer(P.PacketId.C_RoomKick).u64(m.sessionId));
    return el('tr', {},
      el('td', {}, `slot ${m.slot}`),
      el('td', {}, m.nickname),
      el('td', {}, m.isHost ? 'host' : ''),
      el('td', {}, m.isReady ? 'ready' : ''),
      el('td', {}, kick));
  });

  const ready = el('button', { textContent: 'toggle ready' });
  const me = room.members.find((m) => m.sessionId === S.sessionId);
  ready.onclick = () => send(S.match,
    new P.Writer(P.PacketId.C_RoomReady).bool(!(me && me.isReady)));

  const start = el('button', { className: 'primary', textContent: 'start', disabled: !isHost() });
  start.onclick = () => send(S.match, new P.Writer(P.PacketId.C_RoomStart));

  const leave = el('button', { textContent: 'leave' });
  leave.onclick = () => send(S.match, new P.Writer(P.PacketId.C_RoomLeave));

  view.append(
    el('table', {}, el('tbody', {}, ...rows)),
    el('div', { className: 'row', style: 'margin-top:12px' }, ready, start, leave),
    configPanel());
}

function configPanel() {
  const c = S.config;
  if (!c) return el('div');

  const disabled = !isHost();
  const rounds = el('select', { disabled }, ...[3, 5, 7].map(
    (n) => el('option', { value: n, textContent: n })));
  rounds.value = String(c.rounds);

  const ammo = el('input', { type: 'number', min: 0, max: 9, value: c.ammoPerWave, disabled });
  const waves = el('input', { type: 'number', min: 0, max: 60, value: c.waveLimit, disabled });
  const pmin = el('input', { type: 'number', min: 1, max: 5, value: c.paperSizeMin, disabled });
  const pmax = el('input', { type: 'number', min: 1, max: 5, value: c.paperSizeMax, disabled });

  const items = Object.entries(P.ItemKind).map(([bit, label]) => {
    const box = el('input', { type: 'checkbox', disabled });
    box.checked = (c.itemMask & (1 << Number(bit))) !== 0;
    box.dataset.bit = bit;
    return el('label', { style: 'margin-right:10px' }, box, ' ' + label);
  });

  const apply = el('button', { className: 'primary', textContent: 'apply', disabled });
  apply.onclick = () => {
    let mask = 0;
    for (const box of view.querySelectorAll('input[type=checkbox][data-bit]')) {
      if (box.checked) mask |= 1 << Number(box.dataset.bit);
    }
    const w = new P.Writer(P.PacketId.C_RoomSetConfig);
    P.writeConfig(w, {
      rounds: Number(rounds.value), paperSizeMin: Number(pmin.value),
      paperSizeMax: Number(pmax.value), ammoPerWave: Number(ammo.value),
      waveLimit: Number(waves.value), itemMask: mask,
    });
    send(S.match, w);
  };

  return el('fieldset', {},
    el('legend', {}, isHost() ? 'settings' : 'settings (host only)'),
    el('div', { className: 'row' },
      el('label', {}, 'rounds'), rounds,
      el('label', {}, 'ammo (0=∞)'), ammo,
      el('label', {}, 'wave cap (0=∞)'), waves,
      el('label', {}, 'paper'), pmin, pmax),
    el('div', { className: 'row' }, ...items),
    el('div', { className: 'row' }, apply,
       el('span', { className: 'dim' },
          `server will spawn: 0x${S.effectiveItems.toString(16).toUpperCase()}`)));
}

function screenGame() {
  const canvas = el('canvas', { id: 'field', width: 1280, height: 720 });

  const toField = (event) => {
    const rect = canvas.getBoundingClientRect();
    return {
      x: Math.round((event.clientX - rect.left) / rect.width * 1920),
      y: Math.round((event.clientY - rect.top) / rect.height * 1080),
    };
  };

  canvas.onclick = (event) => {
    const point = toField(event);
    fire(point.x, point.y);
  };
  canvas.onmousemove = (event) => { aim = toField(event); };
  canvas.onmouseleave = () => { aim = null; };

  view.append(canvas, el('div', { id: 'scores', className: 'scores', style: 'margin-top:10px' }));
  updateScores();
}

function updateScores() {
  const box = document.getElementById('scores');
  if (!box) return;
  box.replaceChildren(...S.scores.map((s) => {
    const who = S.players.find((p) => p.sessionId === s.sessionId);
    return el('span', {}, `${who ? who.nickname : 'slot ' + s.slot}: ${s.score}`);
  }));
}

// ---------------------------------------------------------------------------
// Drawing
//
// The latest snapshot is drawn as-is, with no interpolation between them on
// purpose: the shot carries the tick of the snapshot it was aimed with, and the
// server rewinds to exactly that. Smoothing the render would mean aiming at a
// position that never existed on the server, and near a cell edge that is the
// difference between a hit and a miss.
// ---------------------------------------------------------------------------

const SLOT_COLOURS = ['#f2b134', '#4bbf73', '#4c9be8', '#e5484d'];

/// Fallback colours for the items with no art yet. Drawn as shapes rather than
/// borrowing an unrelated sprite, which would read as a different item.
const ITEM_COLOURS = {
  1: '#8a6f3f',  // tumbleweed
  4: '#e05fa0',  // balloon
  5: '#7d5fe0',  // paint can
  6: '#4a5058',  // grenade
};

/// Muzzle flashes and impact marks, in field coordinates. Purely local: the
/// server has already decided the outcome, this only makes it visible.
const effects = [];
const EFFECT_MS = 260;

/// Where the mouse is on the field, for the crosshair.
let aim = null;

function drawFrame() {
  paint();
  requestAnimationFrame(drawFrame);
}

/// One frame. Separated from the rAF loop so it can be called directly — rAF
/// does not run in a background or non-compositing tab, which would otherwise
/// make the renderer impossible to exercise without a visible window.
function paint() {
  const canvas = document.getElementById('field');
  if (!canvas) return;

  const g = canvas.getContext('2d');
  const scale = canvas.width / 1920;
  g.clearRect(0, 0, canvas.width, canvas.height);
  g.save();
  g.scale(scale, scale);

  const backdrop = Art.images.background;
  if (backdrop) {
    g.drawImage(backdrop, 0, 0, 1920, 1080);
  } else {
    g.fillStyle = '#2b1e12';
    g.fillRect(0, 0, 1920, 1080);
  }

  for (const e of S.entities) {
    if (e.kind === P.EntityKind.TargetSheet) drawSheet(g, e);
  }
  for (const e of S.entities) {
    if (e.kind === P.EntityKind.Item) drawItem(g, e);
  }

  drawEffects(g);
  if (aim) drawCrosshair(g, aim.x, aim.y);

  g.restore();

  if (S.blinded) {
    g.fillStyle = 'rgba(125,95,224,0.55)';
    g.fillRect(0, 0, canvas.width, canvas.height);
  }
  if (S.stunned) {
    g.fillStyle = 'rgba(229,72,77,0.28)';
    g.fillRect(0, 0, canvas.width, canvas.height);
  }
}

function drawSheet(g, e) {
  if (!S.board) return;

  const width = e.halfWidth * 2;
  const height = e.halfHeight * 2;
  const left = e.x - e.halfWidth;
  const top = e.y - e.halfHeight;

  const face = Art.boardFace;
  if (face) {
    // Drawn to exactly the hit box. The source rect is the measured opaque area
    // of the sprite, so the paper's edges and the server's rectangle coincide —
    // every point that looks like board is board.
    g.drawImage(face.image, face.sx, face.sy, face.sw, face.sh, left, top, width, height);
  } else {
    g.fillStyle = '#efe6d0';
    g.fillRect(left, top, width, height);
  }

  const cw = width / S.board.width;
  const ch = height / S.board.height;

  g.strokeStyle = 'rgba(74, 48, 26, 0.55)';
  g.lineWidth = Math.max(1.5, Math.min(cw, ch) * 0.05);
  for (let x = 0; x <= S.board.width; x++) {
    g.beginPath(); g.moveTo(left + x * cw, top); g.lineTo(left + x * cw, top + height); g.stroke();
  }
  for (let y = 0; y <= S.board.height; y++) {
    g.beginPath(); g.moveTo(left, top + y * ch); g.lineTo(left + width, top + y * ch); g.stroke();
  }

  const hole = Art.images.bulletHole;
  const radius = Math.min(cw, ch) * 0.34;

  for (let i = 0; i < S.cells.length; i++) {
    const owner = S.cells[i];
    if (owner === 0xFF) continue;
    const cx = left + (i % S.board.width) * cw + cw / 2;
    const cy = top + Math.floor(i / S.board.width) * ch + ch / 2;
    const colour = SLOT_COLOURS[owner % SLOT_COLOURS.length];

    if (hole) {
      g.drawImage(hole, cx - radius, cy - radius, radius * 2, radius * 2);
    }
    // A ring in the owner's colour: the hole art is the same for everyone, and
    // whose mark it is has to be readable at a glance.
    g.strokeStyle = colour;
    g.lineWidth = Math.max(2, radius * 0.28);
    g.beginPath();
    g.arc(cx, cy, radius * (hole ? 1.0 : 0.7), 0, Math.PI * 2);
    g.stroke();
    if (!hole) {
      g.fillStyle = colour;
      g.fill();
    }
  }
}

function drawItem(g, e) {
  const sprite = Art.itemSprite(e.item);
  const width = e.halfWidth * 2;
  const height = e.halfHeight * 2;

  if (sprite) {
    // Item art is square with generous padding; drawing it oversized relative
    // to the hit box keeps it readable without making it feel bigger to hit.
    const scale = 2.1;
    g.drawImage(sprite, e.x - (width * scale) / 2, e.y - (height * scale) / 2,
                width * scale, height * scale);
    return;
  }

  g.fillStyle = ITEM_COLOURS[e.item] ?? '#6b5a44';
  g.strokeStyle = 'rgba(30, 18, 8, 0.85)';
  g.lineWidth = 3;
  g.beginPath();
  g.arc(e.x, e.y, Math.max(e.halfWidth, e.halfHeight), 0, Math.PI * 2);
  g.fill();
  g.stroke();

  g.fillStyle = '#1b1109';
  g.font = 'bold 26px Georgia, serif';
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  g.fillText((P.ItemKind[e.item] ?? '?')[0].toUpperCase(), e.x, e.y);
}

function drawCrosshair(g, x, y) {
  const sprite = Art.images.crosshair;
  const size = 84;
  if (sprite) {
    g.drawImage(sprite, x - size / 2, y - size / 2, size, size);
    return;
  }
  g.strokeStyle = '#c0392b';
  g.lineWidth = 3;
  g.beginPath();
  g.arc(x, y, size / 3, 0, Math.PI * 2);
  g.stroke();
}

function addEffect(x, y) {
  effects.push({ x, y, at: performance.now() });
  if (effects.length > 40) effects.shift();
}

function drawEffects(g) {
  const now = performance.now();
  for (let i = effects.length - 1; i >= 0; i--) {
    const age = now - effects[i].at;
    if (age > EFFECT_MS) { effects.splice(i, 1); continue; }

    const t = age / EFFECT_MS;
    const { x, y } = effects[i];
    g.globalAlpha = 1 - t;
    g.fillStyle = '#ffd98a';
    g.beginPath();
    g.arc(x, y, 10 + 34 * t, 0, Math.PI * 2);
    g.fill();
    g.globalAlpha = 1;
  }
}

function fire(x, y) {
  if (!S.inst || !S.inst.open) return;
  // Shown immediately, before the server has ruled on it. The flash is only
  // feedback that the trigger was pulled; whether anything was hit comes back
  // as S_ShotResolved and is what actually changes the board.
  addEffect(x, y);
  send(S.inst, new P.Writer(P.PacketId.C_Fire)
    .i32(x).i32(y).u32(S.snapshotTick).u16(S.fireSeq++));
}

// ---------------------------------------------------------------------------
// Connections
// ---------------------------------------------------------------------------

const send = (conn, writer) => conn && conn.send(writer);

const LOOPBACK = ['127.0.0.1', 'localhost', '::1'];

/// Chat and instance addresses come from the server, which is right in general
/// — they can genuinely live elsewhere.
///
/// But if it names loopback while we reached it over the network, that address
/// is provably wrong for us: the satellite servers were started without
/// --public-host and are advertising themselves as 127.0.0.1. Substituting the
/// host we already reached is correct for every single-box setup, and saying so
/// beats the alternative, which is a lobby that works and a game that silently
/// never connects.
function resolveHost(advertised) {
  if (!advertised) return S.host;
  if (LOOPBACK.includes(advertised) && !LOOPBACK.includes(S.host)) {
    log(`server advertised ${advertised}, which cannot be right from here — ` +
        `using ${S.host}. Start the servers with -PublicHost to fix this properly.`, 'err');
    return S.host;
  }
  return advertised;
}

function connectMatch() {
  S.match?.close();
  const url = `ws://${S.host}:7787`;
  log(`connecting to ${url}`, 'ev');
  S.match = new P.Connection(url, {
    onOpen: () => send(S.match, new P.Writer(P.PacketId.C_Hello).str(S.nickname).str('web/0.1')),
    onPacket: onMatchPacket,
    onClose: () => { log('match connection closed', 'err'); S.sessionId = 0; setScreen('connect'); },
    onError: () => log('match connection failed — is the stack running?', 'err'),
  });
}

function connectChat(host, port, ticket) {
  if (!port) { log('no chat server available', 'ev'); return; }
  S.chat?.close();
  S.chat = new P.Connection(`ws://${host}:${port}`, {
    onOpen: () => send(S.chat, new P.Writer(P.PacketId.C_ChatHello).str(ticket)),
    onPacket: onChatPacket,
    onClose: () => log('chat closed', 'ev'),
  });
}

function connectInstance(host, port, ticket) {
  S.inst?.close();
  S.inst = new P.Connection(`ws://${host}:${port}`, {
    onOpen: () => send(S.inst, new P.Writer(P.PacketId.C_InstHello).str(ticket)),
    onPacket: onInstancePacket,
    onClose: () => { log('instance closed', 'ev'); if (S.screen === 'game') setScreen('room'); },
  });
}

const requestRoomList = () => send(S.match,
  new P.Writer(P.PacketId.C_RoomList).u8(0).str('').bool(false).bool(false).u16(0).u16(50));

// ---------------------------------------------------------------------------
// Packet handling
// ---------------------------------------------------------------------------

function onMatchPacket(id, r) {
  const Id = P.PacketId;
  switch (id) {
    case Id.S_HelloAck: {
      const result = r.u16();
      S.sessionId = r.u64();
      S.nickname = r.str();
      const chatHost = r.str(), chatPort = r.u16(), chatTicket = r.str();
      r.str(); r.u16(); r.str();  // voice endpoint: UDP, unusable from a browser
      if (result !== 0) { log(`hello rejected: ${P.resultName(result)}`, 'err'); return; }
      log(`connected as session ${S.sessionId}`, 'ok');
      connectChat(resolveHost(chatHost), chatPort, chatTicket);
      setScreen('lobby');
      requestRoomList();
      break;
    }
    case Id.S_RoomList:
      S.rooms = r.vec(P.readRoomSummary);
      if (S.screen === 'lobby') render();
      break;

    case Id.S_RoomCreateAck:
    case Id.S_RoomJoinAck: {
      const result = r.u16();
      const room = P.readRoomDetail(r);
      if (result !== 0) { log(`join failed: ${P.resultName(result)}`, 'err'); requestRoomList(); return; }
      S.room = room;
      setScreen('room');
      break;
    }
    case Id.S_RoomConfigChanged:
      S.config = P.readConfig(r);
      S.effectiveItems = r.u32();
      if (!r.bool()) log('settings were adjusted to fit the mode', 'ev');
      if (S.screen === 'room') render();
      break;

    case Id.S_RoomMemberJoined: {
      const member = P.readMember(r);
      S.room?.members.push(member);
      log(`${member.nickname} joined`, 'ev');
      if (S.screen === 'room') render();
      break;
    }
    case Id.S_RoomMemberLeft: {
      const sessionId = r.u64(); r.u8();
      if (S.room) S.room.members = S.room.members.filter((m) => m.sessionId !== sessionId);
      if (S.screen === 'room') render();
      break;
    }
    case Id.S_RoomMemberReady: {
      const sessionId = r.u64(), ready = r.bool();
      const member = S.room?.members.find((m) => m.sessionId === sessionId);
      if (member) member.isReady = ready;
      if (S.screen === 'room') render();
      break;
    }
    case Id.S_RoomHostChanged: {
      const hostId = r.u64();
      if (S.room) {
        S.room.hostSessionId = hostId;
        for (const m of S.room.members) m.isHost = m.sessionId === hostId;
      }
      log(hostId === S.sessionId ? 'you are now host' : 'host changed', 'ev');
      if (S.screen === 'room') render();
      break;
    }
    case Id.S_RoomLeaveAck:
      S.room = null; setScreen('lobby'); requestRoomList();
      break;
    case Id.S_RoomKicked:
      log(`kicked — cannot rejoin for ${(r.u64(), r.u32())}s`, 'err');
      S.room = null; setScreen('lobby'); requestRoomList();
      break;
    case Id.S_RoomClosed:
      r.u64(); log(`room closed: ${r.str()}`, 'ev');
      S.room = null; setScreen('lobby'); requestRoomList();
      break;
    case Id.S_RoomStartAck: {
      const result = r.u16();
      if (result !== 0) log(`start refused: ${P.resultName(result)}`, 'err');
      break;
    }
    case Id.S_RoomKickAck: {
      const result = r.u16();
      if (result !== 0) log(`kick refused: ${P.resultName(result)}`, 'err');
      break;
    }
    case Id.S_GameStarting: {
      const instanceId = r.u64(), host = r.str(), port = r.u16(), ticket = r.str();
      log(`game starting — instance ${instanceId}`, 'ok');
      connectInstance(resolveHost(host), port, ticket);
      break;
    }
    case Id.S_Error:
      log(`server error: ${P.resultName(r.u16())} ${r.str()}`, 'err');
      break;
    default: break;
  }
}

function onChatPacket(id, r) {
  const Id = P.PacketId;
  if (id === Id.S_ChatHelloAck) {
    const result = r.u16();
    log(result === 0 ? 'chat ready' : `chat rejected: ${P.resultName(result)}`,
        result === 0 ? 'ok' : 'err');
  } else if (id === Id.S_ChatMessage) {
    const channel = r.u8(); r.u64();
    log(`[${['?', 'global', 'room', 'instance'][channel] ?? '?'}] ${r.str()}: ${r.str()}`);
  } else if (id === Id.S_ChatNotice) {
    r.u8(); log(`* ${r.str()}`, 'ev');
  } else if (id === Id.S_ChatError) {
    log(`chat: ${P.resultName(r.u16())}`, 'err');
  }
}

function onInstancePacket(id, r) {
  const Id = P.PacketId;
  switch (id) {
    case Id.S_InstHelloAck: {
      const result = r.u16();
      if (result !== 0) { log(`instance rejected: ${P.resultName(result)}`, 'err'); return; }
      r.u64(); S.mySlot = r.u8();
      S.players = r.vec(P.readMember);
      S.gameOver = false;
      setScreen('game');
      break;
    }
    case Id.S_MatchSetup: {
      r.u8();
      S.config = P.readConfig(r);
      S.board = { width: r.u8(), height: r.u8(), winLength: r.u8() };
      S.cells = new Array(S.board.width * S.board.height).fill(0xFF);
      r.u32(); r.u32(); r.u32();
      S.players = r.vec(P.readMember);
      S.roundCount = S.config.rounds;
      log(`board ${S.board.width}x${S.board.height}, ${S.board.winLength} in a row`, 'ok');
      break;
    }
    case Id.S_RoundStart:
      S.roundIndex = r.u8(); S.roundCount = r.u8(); S.paperSize = r.u8();
      S.cells.fill(0xFF);
      log(`round ${S.roundIndex + 1}/${S.roundCount}`, 'ev');
      header();
      break;
    case Id.S_WaveStart:
      r.u16(); r.u32(); r.u32(); S.ammo = r.u8();
      S.waveActive = true; header();
      break;
    case Id.S_WaveEnd:
      S.waveActive = false; S.entities = []; header();
      break;
    case Id.S_WorldSnapshot:
      S.snapshotTick = r.u32();
      S.entities = r.vec(P.readEntity);
      break;
    case Id.S_CellClaimed: {
      const cell = r.u16(), owner = r.u8();
      if (cell < S.cells.length) S.cells[cell] = owner;
      break;
    }
    case Id.S_AmmoChanged: {
      const slot = r.u8(), ammo = r.u8();
      if (slot === S.mySlot) { S.ammo = ammo; header(); }
      break;
    }
    case Id.S_StatusChanged: {
      const slot = r.u8(), flags = r.u8();
      if (slot === S.mySlot) {
        S.stunned = (flags & 1) !== 0;
        S.blinded = (flags & 2) !== 0;
        header();
      }
      break;
    }
    case Id.S_ItemTriggered: {
      const item = r.u8(); r.u32(); r.u64();
      const victim = r.u8();
      log(`item ${P.ItemKind[item] ?? item}` +
          (victim === 0xFF ? '' : victim === S.mySlot ? ' — on you!' : ` — on slot ${victim}`), 'ev');
      break;
    }
    case Id.S_ShotResolved: {
      const shooter = r.u64(); r.u16();
      const kind = r.u8();
      if (shooter !== S.sessionId) break;
      if (kind === P.HitKind.Blocker) log('blocked', 'ev');
      else if (kind === P.HitKind.Decoy) log('decoy — shot wasted', 'ev');
      else if (kind === P.HitKind.CellTaken) log('cell already taken', 'ev');
      break;
    }
    case Id.S_FireRejected:
      r.u16(); log(`fire rejected: ${P.resultName(r.u16())}`, 'err');
      break;
    case Id.S_RoundEnd: {
      const index = r.u8(), winner = r.u8();
      S.scores = r.vec(P.readScore);
      log(`round ${index + 1}: ${winner === 0xFF ? 'no line completed' : 'slot ' + winner + ' wins'}`,
          'ok');
      updateScores();
      break;
    }
    case Id.S_GameOver: {
      const winners = r.vec((rr) => rr.u8());
      S.scores = r.vec(P.readScore);
      S.gameOver = true;
      log(`match over — winning slot(s): ${winners.join(', ')}`, 'ok');
      updateScores();
      break;
    }
    default: break;
  }
}

// ---------------------------------------------------------------------------
// Chat input
// ---------------------------------------------------------------------------

document.getElementById('chatText').addEventListener('keydown', (event) => {
  if (event.key !== 'Enter') return;
  const text = event.target.value.trim();
  if (!text || !S.chat) return;
  send(S.chat, new P.Writer(P.PacketId.C_ChatSend)
    .u8(Number(document.getElementById('chatChannel').value)).str(text));
  event.target.value = '';
});

// Exposed deliberately. This is a test client: being able to inspect the world
// and aim from the console is the point, and there is nothing here a player
// could not already see on screen.
window.game = S;
window.art = Art;
window.fireAt = fire;
window.paint = paint;
/// Centre of a board cell right now, in field coordinates, or null when the
/// sheet is off screen. Same calculation the click handler ends up doing.
window.cellPoint = (index) => {
  const sheet = S.entities.find((e) => e.kind === P.EntityKind.TargetSheet);
  if (!sheet || !S.board) return null;
  const cw = (sheet.halfWidth * 2) / S.board.width;
  const ch = (sheet.halfHeight * 2) / S.board.height;
  return {
    x: Math.round(sheet.x - sheet.halfWidth + (index % S.board.width) * cw + cw / 2),
    y: Math.round(sheet.y - sheet.halfHeight + Math.floor(index / S.board.width) * ch + ch / 2),
    tick: S.snapshotTick,
  };
};

render();
requestAnimationFrame(drawFrame);

// Art loads in the background. Every draw path has a fallback, so the client is
// usable from the first frame and simply gets better looking once the sprites
// arrive — a loading gate would only stand between the user and a working lobby.
loadArt((done, total) => {
  const label = document.getElementById('hdrArt');
  if (!label) return;
  label.textContent = done < total ? `art ${done}/${total}` : '';
}).then(() => log('art loaded', 'ok'));

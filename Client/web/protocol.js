// Wire format, mirrored from the C++ side.
//
// Serialisation is hand-rolled there, which means this file is a second
// implementation of the same byte layout rather than something generated. Add a
// field in Protocol/include/protocol/*.h and you must add it here too — the
// failure mode is silent corruption, not an error, so the field order below is
// kept in the same order as the C++ Serialize methods to make them easy to
// diff by eye.
//
// Everything is little-endian. Strings are [u16 byteLength][utf-8 bytes],
// vectors are [u16 count][elements].

export const PacketId = {
  C_Hello: 1000, S_HelloAck: 1001, C_Ping: 1002, S_Pong: 1003, S_Error: 1004,
  C_RoomCreate: 1010, S_RoomCreateAck: 1011, C_RoomList: 1012, S_RoomList: 1013,
  C_RoomJoin: 1014, S_RoomJoinAck: 1015, C_RoomLeave: 1016, S_RoomLeaveAck: 1017,
  C_RoomKick: 1018, S_RoomKickAck: 1019, C_RoomReady: 1020, S_RoomReadyAck: 1021,
  C_RoomStart: 1022, S_RoomStartAck: 1023, C_QuickMatch: 1024,
  C_RoomSetConfig: 1025, S_RoomConfigChanged: 1026,
  S_RoomMemberJoined: 1030, S_RoomMemberLeft: 1031, S_RoomMemberReady: 1032,
  S_RoomHostChanged: 1033, S_RoomKicked: 1034, S_RoomStateChanged: 1035,
  S_RoomClosed: 1036, S_GameStarting: 1037,

  C_ChatHello: 2000, S_ChatHelloAck: 2001, C_ChatSend: 2002,
  S_ChatMessage: 2003, S_ChatNotice: 2004, S_ChatError: 2005,

  C_InstHello: 3000, S_InstHelloAck: 3001, C_InstLeave: 3002,
  S_InstMemberJoined: 3003, S_InstMemberLeft: 3004, S_InstStart: 3005, S_InstEnd: 3006,

  C_Fire: 3100, S_FireRejected: 3101, S_ShotResolved: 3102,
  S_MatchSetup: 3110, S_RoundStart: 3111, S_RoundEnd: 3112,
  S_WaveStart: 3113, S_WaveEnd: 3114, S_WorldSnapshot: 3115,
  S_CellClaimed: 3116, S_ItemTriggered: 3117, S_StatusChanged: 3118,
  S_AmmoChanged: 3119, S_GameOver: 3120,
};

export const GameMode = { None: 0, TicTacToe: 1, Gomoku9: 2, Gomoku15: 3 };
export const ChannelType = { None: 0, Global: 1, Room: 2, Instance: 3 };
export const HitKind = { Miss: 0, Cell: 1, CellTaken: 2, Decoy: 3, Blocker: 4, Item: 5 };
export const EntityKind = { None: 0, TargetSheet: 1, Item: 2 };

export const ItemKind = {
  1: 'tumbleweed', 2: 'poster', 3: 'pan', 4: 'balloon',
  5: 'paint', 6: 'grenade', 7: 'speedloader',
};

export const MODE_NAMES = { 1: 'tictactoe', 2: 'gomoku9', 3: 'gomoku15' };

const RESULT_NAMES = {
  0: 'Ok', 2: 'InvalidRequest', 3: 'NotAuthenticated', 5: 'ServerFull', 6: 'RateLimited',
  100: 'NicknameInvalid', 200: 'RoomNotFound', 201: 'RoomFull', 202: 'AlreadyInRoom',
  203: 'NotInRoom', 204: 'WrongPassword', 205: 'PasswordRequired', 208: 'RoomNotWaiting',
  209: 'KickCooldown', 300: 'NotHost', 301: 'CannotTargetSelf', 302: 'TargetNotFound',
  303: 'NotEnoughPlayers', 304: 'NotAllReady', 400: 'TicketInvalid', 401: 'TicketExpired',
  500: 'NoInstanceServer', 501: 'InstanceCreateFailed', 601: 'MessageTooLong',
  603: 'NotInChannel',
};
export const resultName = (code) => RESULT_NAMES[code] ?? `code ${code}`;

const HEADER_SIZE = 4;

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

export class Writer {
  constructor(packetId) {
    this.bytes = new Uint8Array(1024);
    this.view = new DataView(this.bytes.buffer);
    this.pos = HEADER_SIZE; // the header is back-filled once the length is known
    this.packetId = packetId;
  }

  _room(extra) {
    if (this.pos + extra <= this.bytes.length) return;
    let size = this.bytes.length * 2;
    while (size < this.pos + extra) size *= 2;
    const grown = new Uint8Array(size);
    grown.set(this.bytes);
    this.bytes = grown;
    this.view = new DataView(this.bytes.buffer);
  }

  u8(v) { this._room(1); this.view.setUint8(this.pos, v); this.pos += 1; return this; }
  u16(v) { this._room(2); this.view.setUint16(this.pos, v, true); this.pos += 2; return this; }
  u32(v) { this._room(4); this.view.setUint32(this.pos, v, true); this.pos += 4; return this; }
  i32(v) { this._room(4); this.view.setInt32(this.pos, v, true); this.pos += 4; return this; }
  bool(v) { return this.u8(v ? 1 : 0); }

  // 64-bit ids are written as two 32-bit halves; JS numbers hold the range the
  // server actually issues (sequential session and room ids).
  u64(v) {
    this._room(8);
    this.view.setBigUint64(this.pos, BigInt(v), true);
    this.pos += 8;
    return this;
  }
  i64(v) {
    this._room(8);
    this.view.setBigInt64(this.pos, BigInt(v), true);
    this.pos += 8;
    return this;
  }

  str(text) {
    const encoded = new TextEncoder().encode(text ?? '');
    this.u16(encoded.length);
    this._room(encoded.length);
    this.bytes.set(encoded, this.pos);
    this.pos += encoded.length;
    return this;
  }

  finish() {
    this.view.setUint16(0, this.pos, true);
    this.view.setUint16(2, this.packetId, true);
    return this.bytes.subarray(0, this.pos);
  }
}

// ---------------------------------------------------------------------------
// Reader
//
// Mirrors the C++ reader's contract: an overrun latches a failure rather than
// throwing per-field, so a handler checks once at the end.
// ---------------------------------------------------------------------------

export class Reader {
  constructor(bytes, offset = HEADER_SIZE) {
    this.bytes = bytes;
    this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    this.pos = offset;
    this.failed = false;
  }

  _take(n) {
    if (this.failed || this.pos + n > this.bytes.byteLength) { this.failed = true; return false; }
    return true;
  }

  u8() { if (!this._take(1)) return 0; const v = this.view.getUint8(this.pos); this.pos += 1; return v; }
  u16() { if (!this._take(2)) return 0; const v = this.view.getUint16(this.pos, true); this.pos += 2; return v; }
  u32() { if (!this._take(4)) return 0; const v = this.view.getUint32(this.pos, true); this.pos += 4; return v; }
  i32() { if (!this._take(4)) return 0; const v = this.view.getInt32(this.pos, true); this.pos += 4; return v; }
  bool() { return this.u8() !== 0; }
  u64() { if (!this._take(8)) return 0; const v = this.view.getBigUint64(this.pos, true); this.pos += 8; return Number(v); }
  i64() { if (!this._take(8)) return 0; const v = this.view.getBigInt64(this.pos, true); this.pos += 8; return Number(v); }

  str() {
    const length = this.u16();
    if (!this._take(length)) return '';
    const text = new TextDecoder().decode(
      this.bytes.subarray(this.pos, this.pos + length));
    this.pos += length;
    return text;
  }

  vec(readElement) {
    const count = this.u16();
    const result = [];
    for (let i = 0; i < count && !this.failed; i++) result.push(readElement(this));
    return result;
  }
}

// ---------------------------------------------------------------------------
// Shared structures — field order matches the C++ Serialize methods exactly
// ---------------------------------------------------------------------------

export const readMember = (r) => ({
  sessionId: r.u64(), nickname: r.str(), slot: r.u8(), isHost: r.bool(), isReady: r.bool(),
});

export const readRoomSummary = (r) => ({
  roomId: r.u64(), name: r.str(), mode: r.u8(), state: r.u8(),
  memberCount: r.u8(), capacity: r.u8(), hasPassword: r.bool(), hostNickname: r.str(),
});

export const readRoomDetail = (r) => ({
  roomId: r.u64(), name: r.str(), mode: r.u8(), state: r.u8(), capacity: r.u8(),
  hasPassword: r.bool(), hostSessionId: r.u64(), members: r.vec(readMember),
});

export const readConfig = (r) => ({
  rounds: r.u8(), paperSizeMin: r.u8(), paperSizeMax: r.u8(),
  ammoPerWave: r.u8(), waveLimit: r.u8(), itemMask: r.u32(),
});

export const writeConfig = (w, c) => {
  w.u8(c.rounds).u8(c.paperSizeMin).u8(c.paperSizeMax)
   .u8(c.ammoPerWave).u8(c.waveLimit).u32(c.itemMask);
};

export const readEntity = (r) => ({
  entityId: r.u32(), kind: r.u8(), item: r.u8(),
  x: r.i32(), y: r.i32(), halfWidth: r.i32(), halfHeight: r.i32(),
});

export const readScore = (r) => ({ sessionId: r.u64(), slot: r.u8(), score: r.u8() });

// ---------------------------------------------------------------------------
// Connection
//
// One WebSocket message can carry more than one packet, so the framing below is
// the same length-prefixed scan the server does rather than assuming one
// message equals one packet.
// ---------------------------------------------------------------------------

export class Connection {
  constructor(url, { onOpen, onPacket, onClose, onError } = {}) {
    this.pending = new Uint8Array(0);
    this.socket = new WebSocket(url);
    this.socket.binaryType = 'arraybuffer';
    this.socket.onopen = () => onOpen?.();
    this.socket.onclose = () => onClose?.();
    this.socket.onerror = (e) => onError?.(e);
    this.socket.onmessage = (event) => {
      const incoming = new Uint8Array(event.data);
      const merged = new Uint8Array(this.pending.length + incoming.length);
      merged.set(this.pending);
      merged.set(incoming, this.pending.length);
      this.pending = merged;

      let offset = 0;
      for (;;) {
        if (this.pending.length - offset < HEADER_SIZE) break;
        const view = new DataView(this.pending.buffer, this.pending.byteOffset + offset);
        const size = view.getUint16(0, true);
        const id = view.getUint16(2, true);
        if (size < HEADER_SIZE || this.pending.length - offset < size) break;
        onPacket?.(id, new Reader(this.pending.subarray(offset, offset + size)));
        offset += size;
      }
      this.pending = this.pending.subarray(offset);
    };
  }

  send(writer) {
    if (this.socket.readyState === WebSocket.OPEN) {
      this.socket.send(writer.finish());
    }
  }

  close() {
    try { this.socket.close(); } catch { /* already gone */ }
  }

  get open() { return this.socket.readyState === WebSocket.OPEN; }
}

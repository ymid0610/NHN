using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using UnityEngine;

namespace NHN.Network
{
    /// <summary>
    /// The connection to the instance server — the one that actually runs the
    /// match.
    ///
    /// Threading follows MatchServerClient: the socket lives on a worker thread
    /// and every result is marshalled onto the main thread through a queue
    /// drained in Update(), because Unity API calls are only legal there.
    ///
    /// Unlike MatchServerClient this also RETAINS the state it is told about.
    /// The connection is opened in the menu scene and the game scene loads
    /// afterwards, so S_MatchSetup and the first S_RoundStart routinely arrive
    /// before anything is listening. A controller that subscribes late reads the
    /// current state first and then follows the events, instead of waiting for a
    /// setup packet that has already been and gone.
    /// </summary>
    public sealed class InstanceClient : MonoBehaviour
    {
        private const int HeaderSize = 4;

        /// Snapshots are the largest thing that arrives here and they are a few
        /// hundred bytes, so this is generous. It has to stay inside the ushort
        /// the frame header is read into, or the guard below can never fire.
        private const int MaxPacketSize = 32 * 1024;

        public string Host { get; private set; }
        public int Port { get; private set; }
        public bool IsConnected { get; private set; }
        public bool IsAuthenticated { get; private set; }

        /// Our own slot, as assigned by the room. GameSlots.NoOwner until the
        /// hello is acknowledged.
        public byte Slot { get; private set; } = GameSlots.NoOwner;
        public ulong InstanceId { get; private set; }

        // -- retained authoritative state ---------------------------------------

        public ServerMatchSetup Setup { get; private set; }
        public bool HasSetup { get; private set; }
        public ServerRoundStart Round { get; private set; }
        public bool RoundActive { get; private set; }
        public ServerWaveStart Wave { get; private set; }
        public bool WaveActive { get; private set; }

        /// Latest replicated transforms, and the tick they belong to. A shot is
        /// aimed against these, and carries this tick so the server rewinds to
        /// the same moment.
        public uint SnapshotTick { get; private set; }
        public List<ServerEntityState> Entities { get; private set; } = new List<ServerEntityState>();

        /// Cell owner by cell index, or GameSlots.NoOwner. Reset each round.
        public byte[] CellOwners { get; private set; } = new byte[0];

        public byte MyAmmo { get; private set; }
        public bool AmIStunned { get; private set; }
        public bool AmIBlinded { get; private set; }

        public event Action<string> StatusChanged;
        public event Action<ServerResultCode> HelloAck;
        public event Action<ServerMatchSetup> MatchSetupReceived;
        public event Action<ServerRoundStart> RoundStarted;
        public event Action<ServerRoundEnd> RoundEnded;
        public event Action<ServerWaveStart> WaveStarted;
        public event Action<ushort> WaveEnded;
        public event Action SnapshotReceived;
        public event Action<int, byte> CellClaimed;
        public event Action<ServerShotResolved> ShotResolved;
        public event Action<ushort, ServerResultCode> FireRejected;
        public event Action<ServerItemTriggered> ItemTriggered;
        public event Action<byte, ServerStatusFlags> PlayerStatusChanged;
        public event Action<byte, byte> AmmoChanged;
        public event Action<ServerGameOver> GameOver;
        public event Action<string> InstanceEnded;

        private readonly object sendLock = new object();
        private readonly object actionLock = new object();
        private readonly Queue<Action> mainThreadActions = new Queue<Action>();
        private readonly Dictionary<byte, byte> ammoBySlot = new Dictionary<byte, byte>();
        private readonly Dictionary<byte, ServerStatusFlags> statusBySlot = new Dictionary<byte, ServerStatusFlags>();

        private TcpClient client;
        private NetworkStream stream;
        private Thread workerThread;
        private volatile bool shouldRun;
        private string ticket;
        private ushort fireSequence;

        public void Connect(string host, int port, string instanceTicket)
        {
            Disconnect();

            Host = string.IsNullOrWhiteSpace(host) ? "127.0.0.1" : host.Trim();
            Port = Mathf.Clamp(port, 1, 65535);
            ticket = instanceTicket ?? string.Empty;
            shouldRun = true;

            workerThread = new Thread(ConnectAndReceiveLoop);
            workerThread.IsBackground = true;
            workerThread.Name = "InstanceClient";
            workerThread.Start();

            EnqueueStatus(string.Format("인스턴스 연결 중: {0}:{1}", Host, Port));
        }

        public void Disconnect()
        {
            shouldRun = false;

            lock (sendLock)
            {
                if (stream != null)
                {
                    stream.Close();
                    stream = null;
                }

                if (client != null)
                {
                    client.Close();
                    client = null;
                }
            }

            // Wait for the receive loop to notice the closed socket before
            // returning. Without this a reconnect can start a second worker
            // while the first is still draining, and both would push onto the
            // main-thread queue.
            Thread worker = workerThread;
            if (worker != null && worker.IsAlive && worker != Thread.CurrentThread)
            {
                worker.Join(500);
            }

            workerThread = null;
            IsConnected = false;
            IsAuthenticated = false;
            Slot = GameSlots.NoOwner;
        }

        /// <summary>
        /// Fires at a point in field space (1920x1080, y down).
        ///
        /// The tick sent is the one the caller is currently displaying, so the
        /// server rewinds to the same moment it was aimed against.
        /// </summary>
        public bool Fire(int fieldX, int fieldY, out ushort sequence)
        {
            sequence = unchecked(++fireSequence);
            ushort captured = sequence;
            uint tick = SnapshotTick;

            return Send(ServerPacketId.C_Fire, writer =>
            {
                writer.Write(fieldX);
                writer.Write(fieldY);
                writer.Write(tick);
                writer.Write(captured);
            });
        }

        public bool Leave()
        {
            return Send(ServerPacketId.C_InstLeave, null);
        }

        public byte GetAmmo(byte slot)
        {
            byte value;
            return ammoBySlot.TryGetValue(slot, out value) ? value : (byte)0;
        }

        public ServerStatusFlags GetStatus(byte slot)
        {
            ServerStatusFlags value;
            return statusBySlot.TryGetValue(slot, out value) ? value : ServerStatusFlags.None;
        }

        public byte GetCellOwner(int cellIndex)
        {
            if (cellIndex < 0 || cellIndex >= CellOwners.Length)
            {
                return GameSlots.NoOwner;
            }

            return CellOwners[cellIndex];
        }

        /// The target sheet from the latest snapshot, if one is in flight.
        public bool TryGetSheet(out ServerEntityState sheet)
        {
            for (int i = 0; i < Entities.Count; i++)
            {
                if (Entities[i].Kind == ServerEntityKind.TargetSheet)
                {
                    sheet = Entities[i];
                    return true;
                }
            }

            sheet = default(ServerEntityState);
            return false;
        }

        private void Update()
        {
            for (;;)
            {
                Action action = null;
                lock (actionLock)
                {
                    if (mainThreadActions.Count > 0)
                    {
                        action = mainThreadActions.Dequeue();
                    }
                }

                if (action == null)
                {
                    return;
                }

                action();
            }
        }

        private void OnDestroy()
        {
            Disconnect();
        }

        private void ConnectAndReceiveLoop()
        {
            try
            {
                TcpClient tcpClient = new TcpClient();
                tcpClient.NoDelay = true;
                tcpClient.Connect(Host, Port);

                lock (sendLock)
                {
                    client = tcpClient;
                    stream = tcpClient.GetStream();
                }

                Enqueue(delegate
                {
                    IsConnected = true;
                    StatusChanged?.Invoke("인스턴스 연결 완료, 티켓 확인 중");
                });

                Send(ServerPacketId.C_InstHello, writer => writer.Write(ticket));

                ReceiveLoop(tcpClient.GetStream());
            }
            catch (Exception exception)
            {
                if (shouldRun)
                {
                    EnqueueStatus("인스턴스 연결 실패: " + exception.Message);
                }
            }
            finally
            {
                Enqueue(delegate
                {
                    IsConnected = false;
                    IsAuthenticated = false;
                    StatusChanged?.Invoke("인스턴스 연결 종료");
                });
            }
        }

        private void ReceiveLoop(NetworkStream networkStream)
        {
            byte[] header = new byte[HeaderSize];

            while (shouldRun)
            {
                if (!ReadExact(networkStream, header, 0, HeaderSize))
                {
                    break;
                }

                ushort size = (ushort)(header[0] | (header[1] << 8));
                ushort id = (ushort)(header[2] | (header[3] << 8));
                if (size < HeaderSize || size > MaxPacketSize)
                {
                    throw new InvalidDataException("Bad packet size " + size);
                }

                byte[] frame = new byte[size];
                Buffer.BlockCopy(header, 0, frame, 0, HeaderSize);

                int bodySize = size - HeaderSize;
                if (bodySize > 0 && !ReadExact(networkStream, frame, HeaderSize, bodySize))
                {
                    break;
                }

                Dispatch((ServerPacketId)id, frame);
            }
        }

        private static bool ReadExact(Stream input, byte[] buffer, int offset, int length)
        {
            int read = 0;
            while (read < length)
            {
                int amount = input.Read(buffer, offset + read, length - read);
                if (amount <= 0)
                {
                    return false;
                }

                read += amount;
            }

            return true;
        }

        private bool Send(ServerPacketId id, Action<PacketWriter> bodyWriter)
        {
            lock (sendLock)
            {
                if (stream == null)
                {
                    return false;
                }

                try
                {
                    PacketWriter writer = new PacketWriter(id);
                    if (bodyWriter != null)
                    {
                        bodyWriter(writer);
                    }

                    byte[] bytes = writer.ToArray();
                    stream.Write(bytes, 0, bytes.Length);
                    stream.Flush();
                    return true;
                }
                catch (Exception exception)
                {
                    EnqueueStatus("인스턴스 전송 실패: " + exception.Message);
                    return false;
                }
            }
        }

        private void Dispatch(ServerPacketId id, byte[] frame)
        {
            try
            {
                PacketReader reader = new PacketReader(frame);
                switch (id)
                {
                    case ServerPacketId.S_InstHelloAck:
                        HandleHelloAck(reader);
                        break;
                    case ServerPacketId.S_InstStart:
                        HandleInstStart(reader);
                        break;
                    case ServerPacketId.S_InstEnd:
                        HandleInstEnd(reader);
                        break;
                    case ServerPacketId.S_InstMemberJoined:
                        HandleMemberJoined(reader);
                        break;
                    case ServerPacketId.S_InstMemberLeft:
                        HandleMemberLeft(reader);
                        break;
                    case ServerPacketId.S_MatchSetup:
                        HandleMatchSetup(reader);
                        break;
                    case ServerPacketId.S_RoundStart:
                        HandleRoundStart(reader);
                        break;
                    case ServerPacketId.S_RoundEnd:
                        HandleRoundEnd(reader);
                        break;
                    case ServerPacketId.S_WaveStart:
                        HandleWaveStart(reader);
                        break;
                    case ServerPacketId.S_WaveEnd:
                        HandleWaveEnd(reader);
                        break;
                    case ServerPacketId.S_WorldSnapshot:
                        HandleWorldSnapshot(reader);
                        break;
                    case ServerPacketId.S_CellClaimed:
                        HandleCellClaimed(reader);
                        break;
                    case ServerPacketId.S_ShotResolved:
                        HandleShotResolved(reader);
                        break;
                    case ServerPacketId.S_FireRejected:
                        HandleFireRejected(reader);
                        break;
                    case ServerPacketId.S_ItemTriggered:
                        HandleItemTriggered(reader);
                        break;
                    case ServerPacketId.S_StatusChanged:
                        HandleStatusChanged(reader);
                        break;
                    case ServerPacketId.S_AmmoChanged:
                        HandleAmmoChanged(reader);
                        break;
                    case ServerPacketId.S_GameOver:
                        HandleGameOver(reader);
                        break;
                    default:
                        EnqueueStatus("처리하지 않는 인스턴스 패킷: " + id);
                        break;
                }
            }
            catch (Exception exception)
            {
                EnqueueStatus("패킷 처리 실패: " + id + " / " + exception.Message);
            }
        }

        private void HandleHelloAck(PacketReader reader)
        {
            ServerResultCode result = (ServerResultCode)reader.ReadUInt16();
            ulong instanceId = reader.ReadUInt64();
            byte slot = reader.ReadByte();
            List<ServerRoomMember> members = reader.ReadList(r => r.ReadRoomMember());

            Enqueue(delegate
            {
                IsAuthenticated = result == ServerResultCode.Ok;
                InstanceId = instanceId;
                Slot = IsAuthenticated ? slot : GameSlots.NoOwner;

                if (IsAuthenticated && !HasSetup)
                {
                    // The member list arrives before S_MatchSetup; keep it so a
                    // scoreboard has names to show while the match spins up.
                    ServerMatchSetup partial = Setup;
                    partial.Players = members;
                    Setup = partial;
                }

                HelloAck?.Invoke(result);
                StatusChanged?.Invoke(IsAuthenticated
                    ? string.Format("인스턴스 입장 완료: slot {0}", slot)
                    : "인스턴스 입장 실패: " + result);
            });
        }

        private void HandleInstStart(PacketReader reader)
        {
            reader.ReadUInt64(); // serverTimeMs, unused for now
            EnqueueStatus("전원 입장 완료");
        }

        private void HandleInstEnd(PacketReader reader)
        {
            string reason = reader.ReadString();
            Enqueue(delegate
            {
                WaveActive = false;
                RoundActive = false;
                InstanceEnded?.Invoke(reason);
                StatusChanged?.Invoke("인스턴스 종료: " + reason);
            });
        }

        private void HandleMemberJoined(PacketReader reader)
        {
            ServerRoomMember member = reader.ReadRoomMember();
            EnqueueStatus(member.Nickname + " 입장");
        }

        private void HandleMemberLeft(PacketReader reader)
        {
            ulong sessionId = reader.ReadUInt64();
            ServerLeaveReason reason = (ServerLeaveReason)reader.ReadByte();
            EnqueueStatus(string.Format("session {0} 퇴장: {1}", sessionId, reason));
        }

        private void HandleMatchSetup(PacketReader reader)
        {
            ServerMatchSetup setup = new ServerMatchSetup
            {
                Mode = (ServerGameMode)reader.ReadByte(),
                Config = reader.ReadMatchConfig(),
                Board = reader.ReadBoardSpec(),
                FieldWidth = reader.ReadUInt32(),
                FieldHeight = reader.ReadUInt32(),
                WaveDurationMs = reader.ReadUInt32(),
                Players = reader.ReadList(r => r.ReadRoomMember())
            };

            Enqueue(delegate
            {
                Setup = setup;
                HasSetup = true;
                CellOwners = new byte[Mathf.Max(0, setup.Board.CellCount)];
                ClearCellOwners();
                MatchSetupReceived?.Invoke(setup);
                StatusChanged?.Invoke(string.Format("매치 시작: {0} {1}x{2}, {3}목",
                    ServerProtocolText.ToDisplay(setup.Mode),
                    setup.Board.Width,
                    setup.Board.Height,
                    setup.Board.WinLength));
            });
        }

        private void HandleRoundStart(PacketReader reader)
        {
            ServerRoundStart round = new ServerRoundStart
            {
                RoundIndex = reader.ReadByte(),
                RoundCount = reader.ReadByte(),
                PaperSize = reader.ReadByte(),
                RoundSeed = reader.ReadUInt64()
            };

            Enqueue(delegate
            {
                Round = round;
                RoundActive = true;
                ClearCellOwners();
                RoundStarted?.Invoke(round);
            });
        }

        private void HandleRoundEnd(PacketReader reader)
        {
            ServerRoundEnd round = new ServerRoundEnd
            {
                RoundIndex = reader.ReadByte(),
                WinnerSlot = reader.ReadByte(),
                Scores = reader.ReadList(r => r.ReadPlayerScore())
            };

            Enqueue(delegate
            {
                RoundActive = false;
                WaveActive = false;
                RoundEnded?.Invoke(round);
            });
        }

        private void HandleWaveStart(PacketReader reader)
        {
            ServerWaveStart wave = new ServerWaveStart
            {
                WaveIndex = reader.ReadUInt16(),
                StartTick = reader.ReadUInt32(),
                EndTick = reader.ReadUInt32(),
                Ammo = reader.ReadByte()
            };

            Enqueue(delegate
            {
                Wave = wave;
                WaveActive = true;
                WaveStarted?.Invoke(wave);
            });
        }

        private void HandleWaveEnd(PacketReader reader)
        {
            ushort waveIndex = reader.ReadUInt16();
            Enqueue(delegate
            {
                WaveActive = false;
                Entities.Clear();
                WaveEnded?.Invoke(waveIndex);
            });
        }

        private void HandleWorldSnapshot(PacketReader reader)
        {
            uint tick = reader.ReadUInt32();
            List<ServerEntityState> entities = reader.ReadList(r => r.ReadEntityState());

            Enqueue(delegate
            {
                // A snapshot that lost a race with a newer one would drag the
                // world backwards, and the tick it carries is what a shot is
                // rewound to, so an out-of-order one is worse than none.
                if (SnapshotTick != 0 && tick < SnapshotTick)
                {
                    return;
                }

                SnapshotTick = tick;
                Entities = entities;
                SnapshotReceived?.Invoke();
            });
        }

        private void HandleCellClaimed(PacketReader reader)
        {
            ushort cellIndex = reader.ReadUInt16();
            byte ownerSlot = reader.ReadByte();

            Enqueue(delegate
            {
                if (cellIndex < CellOwners.Length)
                {
                    CellOwners[cellIndex] = ownerSlot;
                }

                CellClaimed?.Invoke(cellIndex, ownerSlot);
            });
        }

        private void HandleShotResolved(PacketReader reader)
        {
            ServerShotResolved shot = new ServerShotResolved
            {
                Shooter = reader.ReadUInt64(),
                Sequence = reader.ReadUInt16(),
                Kind = (ServerHitKind)reader.ReadByte(),
                HitX = reader.ReadInt32(),
                HitY = reader.ReadInt32(),
                EntityId = reader.ReadUInt32(),
                CellIndex = reader.ReadUInt16(),
                Derived = reader.ReadBool()
            };

            Enqueue(delegate { ShotResolved?.Invoke(shot); });
        }

        private void HandleFireRejected(PacketReader reader)
        {
            ushort sequence = reader.ReadUInt16();
            ServerResultCode result = (ServerResultCode)reader.ReadUInt16();
            Enqueue(delegate { FireRejected?.Invoke(sequence, result); });
        }

        private void HandleItemTriggered(PacketReader reader)
        {
            ServerItemTriggered item = new ServerItemTriggered
            {
                Item = (ServerItemKind)reader.ReadByte(),
                EntityId = reader.ReadUInt32(),
                Instigator = reader.ReadUInt64(),
                VictimSlot = reader.ReadByte(),
                Seed = reader.ReadUInt32()
            };

            Enqueue(delegate { ItemTriggered?.Invoke(item); });
        }

        private void HandleStatusChanged(PacketReader reader)
        {
            byte slot = reader.ReadByte();
            ServerStatusFlags flags = (ServerStatusFlags)reader.ReadByte();
            reader.ReadUInt32(); // untilTick, informational

            Enqueue(delegate
            {
                statusBySlot[slot] = flags;
                if (slot == Slot)
                {
                    AmIStunned = (flags & ServerStatusFlags.Stunned) != 0;
                    AmIBlinded = (flags & ServerStatusFlags.Blinded) != 0;
                }

                PlayerStatusChanged?.Invoke(slot, flags);
            });
        }

        private void HandleAmmoChanged(PacketReader reader)
        {
            byte slot = reader.ReadByte();
            byte ammo = reader.ReadByte();

            Enqueue(delegate
            {
                ammoBySlot[slot] = ammo;
                if (slot == Slot)
                {
                    MyAmmo = ammo;
                }

                AmmoChanged?.Invoke(slot, ammo);
            });
        }

        private void HandleGameOver(PacketReader reader)
        {
            ServerGameOver over = new ServerGameOver
            {
                WinnerSlots = reader.ReadList(r => r.ReadByte()),
                Scores = reader.ReadList(r => r.ReadPlayerScore())
            };

            Enqueue(delegate
            {
                RoundActive = false;
                WaveActive = false;
                GameOver?.Invoke(over);
            });
        }

        private void ClearCellOwners()
        {
            for (int i = 0; i < CellOwners.Length; i++)
            {
                CellOwners[i] = GameSlots.NoOwner;
            }
        }

        private void EnqueueStatus(string message)
        {
            Enqueue(delegate { StatusChanged?.Invoke(message); });
        }

        private void Enqueue(Action action)
        {
            lock (actionLock)
            {
                mainThreadActions.Enqueue(action);
            }
        }
    }
}

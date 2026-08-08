using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using UnityEngine;

namespace NHN.Network
{
    public sealed class MatchServerClient : MonoBehaviour
    {
        private const int HeaderSize = 4;
        private const int MaxPacketSize = 16 * 1024;

        public string Host { get; private set; }
        public int Port { get; private set; }
        public string Nickname { get; private set; }
        public bool IsConnected { get; private set; }
        public bool IsAuthenticated { get; private set; }
        public ulong SessionId { get; private set; }
        public ServerRoomDetail CurrentRoom { get; private set; }
        public List<ServerRoomSummary> LastRoomList { get; private set; } = new List<ServerRoomSummary>();

        public event Action<string> StatusChanged;
        public event Action<ServerResultCode, ulong, string> HelloAck;
        public event Action<ServerRoomDetail> RoomChanged;
        public event Action<List<ServerRoomSummary>, uint, ushort> RoomListReceived;
        public event Action<ServerGameStartingInfo> GameStarting;
        public event Action<ServerResultCode, ServerRoomMember> BotChanged;
        public event Action<ServerMatchConfig, uint> RoomConfigChanged;

        /// The room's current settings, and the item set the server will really
        /// spawn under them. Retained because the settings panel has to render
        /// what is in force, not only react to changes.
        public ServerMatchConfig CurrentConfig { get; private set; }
        public uint EffectiveItemMask { get; private set; }
        public event Action<ServerQuickMatchStatus> QuickMatchStatus;
        public event Action<ServerResultCode> QuickMatchCancelled;

        /// Latest queue state, so a screen opening mid-wait has something to show.
        public ServerQuickMatchStatus LastQuickMatch { get; private set; }
        public bool IsQuickMatching { get; private set; }

        private readonly object sendLock = new object();
        private readonly object actionLock = new object();
        private readonly Queue<Action> mainThreadActions = new Queue<Action>();

        private TcpClient client;
        private NetworkStream stream;
        private Thread workerThread;
        private volatile bool shouldRun;

        public void ConnectAndHello(string host, int port, string nickname)
        {
            Disconnect();

            Host = string.IsNullOrWhiteSpace(host) ? "127.0.0.1" : host.Trim();
            Port = Mathf.Clamp(port, 1, 65535);
            Nickname = string.IsNullOrWhiteSpace(nickname) ? "Player" : nickname.Trim();
            shouldRun = true;

            workerThread = new Thread(ConnectAndReceiveLoop);
            workerThread.IsBackground = true;
            workerThread.Name = "MatchServerClient";
            workerThread.Start();

            EnqueueStatus(string.Format("Match 서버 연결 중: {0}:{1}", Host, Port));
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

            IsConnected = false;
            IsAuthenticated = false;
            SessionId = 0;
            CurrentRoom = default(ServerRoomDetail);
        }

        public bool CreateRoom(string roomName, ServerGameMode mode, string password)
        {
            return Send(ServerPacketId.C_RoomCreate, writer =>
            {
                writer.Write(roomName);
                writer.Write(mode);
                writer.Write(password);
            });
        }

        public bool RequestRoomList(ServerGameMode mode, string nameFilter, bool hideFull, bool hideLocked, ushort page, ushort pageSize)
        {
            return Send(ServerPacketId.C_RoomList, writer =>
            {
                writer.Write(mode);
                writer.Write(nameFilter);
                writer.Write(hideFull);
                writer.Write(hideLocked);
                writer.Write(page);
                writer.Write(pageSize);
            });
        }

        public bool JoinRoom(ulong roomId, string password)
        {
            return Send(ServerPacketId.C_RoomJoin, writer =>
            {
                writer.Write(roomId);
                writer.Write(password);
            });
        }

        public bool LeaveRoom()
        {
            return Send(ServerPacketId.C_RoomLeave, null);
        }

        public bool SetReady(bool ready)
        {
            return Send(ServerPacketId.C_RoomReady, writer => writer.Write(ready));
        }

        public bool StartRoom()
        {
            return Send(ServerPacketId.C_RoomStart, null);
        }

        /// <summary>
        /// Joins the matchmaking queue for a mode.
        ///
        /// No room is created and none is joined: the server gathers enough
        /// players, then starts an instance with that mode's default settings.
        /// Progress arrives as QuickMatchStatus and the match itself as
        /// GameStarting.
        /// </summary>
        public bool QuickMatch(ServerGameMode mode)
        {
            return Send(ServerPacketId.C_QuickMatch, writer => writer.Write(mode));
        }

        public bool CancelQuickMatch()
        {
            return Send(ServerPacketId.C_QuickMatchCancel, null);
        }

        /// <summary>
        /// Host-only. Adds a bot to the room; it takes a slot and counts
        /// towards the minimum needed to start.
        /// </summary>
        public bool AddBot(byte difficulty)
        {
            return Send(ServerPacketId.C_RoomAddBot, writer => writer.Write(difficulty));
        }

        public bool RemoveBot(ulong botSessionId)
        {
            return Send(ServerPacketId.C_RoomRemoveBot, writer => writer.Write(botSessionId));
        }

        public bool SetBotDifficulty(ulong botSessionId, byte difficulty)
        {
            return Send(ServerPacketId.C_RoomSetBotDifficulty, writer =>
            {
                writer.Write(botSessionId);
                writer.Write(difficulty);
            });
        }

        public bool SetRoomConfig(ServerMatchConfig config)
        {
            return Send(ServerPacketId.C_RoomSetConfig, writer => writer.Write(config));
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
                    StatusChanged?.Invoke("Match 서버 연결 완료, 인증 중");
                });

                Send(ServerPacketId.C_Hello, writer =>
                {
                    writer.Write(Nickname);
                    writer.Write("unity-client/0.1");
                });

                ReceiveLoop(tcpClient.GetStream());
            }
            catch (Exception exception)
            {
                if (shouldRun)
                {
                    EnqueueStatus("Match 서버 연결 실패: " + exception.Message);
                }
            }
            finally
            {
                Enqueue(delegate
                {
                    IsConnected = false;
                    IsAuthenticated = false;
                    StatusChanged?.Invoke("Match 서버 연결 종료");
                });
            }
        }

        private void ReceiveLoop(NetworkStream networkStream)
        {
            byte[] header = new byte[HeaderSize];

            while (shouldRun)
            {
                if (!ReadExact(networkStream, header, HeaderSize))
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

        private static bool ReadExact(Stream input, byte[] buffer, int length)
        {
            return ReadExact(input, buffer, 0, length);
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
                    EnqueueStatus("서버 연결 전이라 요청을 보낼 수 없음");
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
                    EnqueueStatus("서버 전송 실패: " + exception.Message);
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
                    case ServerPacketId.S_HelloAck:
                        HandleHelloAck(reader);
                        break;
                    case ServerPacketId.S_Error:
                        HandleServerError(reader);
                        break;
                    case ServerPacketId.S_RoomCreateAck:
                        HandleRoomCreateAck(reader);
                        break;
                    case ServerPacketId.S_RoomJoinAck:
                        HandleRoomJoinAck(reader);
                        break;
                    case ServerPacketId.S_RoomList:
                        HandleRoomList(reader);
                        break;
                    case ServerPacketId.S_RoomLeaveAck:
                        HandleRoomLeaveAck(reader);
                        break;
                    case ServerPacketId.S_RoomReadyAck:
                        HandleRoomReadyAck(reader);
                        break;
                    case ServerPacketId.S_RoomStartAck:
                        HandleRoomStartAck(reader);
                        break;
                    case ServerPacketId.S_RoomConfigChanged:
                        HandleRoomConfigChanged(reader);
                        break;
                    case ServerPacketId.S_RoomMemberJoined:
                        HandleRoomMemberJoined(reader);
                        break;
                    case ServerPacketId.S_RoomMemberLeft:
                        HandleRoomMemberLeft(reader);
                        break;
                    case ServerPacketId.S_RoomMemberReady:
                        HandleRoomMemberReady(reader);
                        break;
                    case ServerPacketId.S_RoomHostChanged:
                        HandleRoomHostChanged(reader);
                        break;
                    case ServerPacketId.S_RoomKicked:
                        HandleRoomKicked(reader);
                        break;
                    case ServerPacketId.S_RoomStateChanged:
                        HandleRoomStateChanged(reader);
                        break;
                    case ServerPacketId.S_RoomClosed:
                        HandleRoomClosed(reader);
                        break;
                    case ServerPacketId.S_QuickMatchQueued:
                        HandleQuickMatchQueued(reader);
                        break;
                    case ServerPacketId.S_QuickMatchCancelled:
                        HandleQuickMatchCancelled(reader);
                        break;
                    case ServerPacketId.S_RoomBotChanged:
                        HandleBotChanged(reader);
                        break;
                    case ServerPacketId.S_GameStarting:
                        HandleGameStarting(reader);
                        break;
                    default:
                        EnqueueStatus("처리하지 않는 Match 패킷: " + id);
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
            ulong sessionId = reader.ReadUInt64();
            string nickname = reader.ReadString();
            string chatHost = reader.ReadString();
            ushort chatPort = reader.ReadUInt16();
            string chatTicket = reader.ReadString();
            string voiceHost = reader.ReadString();
            ushort voicePort = reader.ReadUInt16();
            string voiceTicket = reader.ReadString();

            Enqueue(delegate
            {
                IsAuthenticated = result == ServerResultCode.Ok;
                SessionId = IsAuthenticated ? sessionId : 0;
                Nickname = nickname;
                HelloAck?.Invoke(result, SessionId, nickname);
                StatusChanged?.Invoke(IsAuthenticated
                    ? string.Format("인증 완료: session {0}", SessionId)
                    : "인증 실패: " + result);

                if (chatPort != 0 && !string.IsNullOrEmpty(chatTicket))
                {
                    Debug.Log(string.Format("Chat ticket issued at {0}:{1}", chatHost, chatPort));
                }

                if (voicePort != 0 && !string.IsNullOrEmpty(voiceTicket))
                {
                    Debug.Log(string.Format("Voice ticket issued at {0}:{1}", voiceHost, voicePort));
                }
            });
        }

        private void HandleServerError(PacketReader reader)
        {
            ServerResultCode result = (ServerResultCode)reader.ReadUInt16();
            string detail = reader.ReadString();
            EnqueueStatus("서버 오류: " + result + " " + detail);
        }

        private void HandleRoomCreateAck(PacketReader reader)
        {
            ServerResultCode result = (ServerResultCode)reader.ReadUInt16();
            ServerRoomDetail room = reader.ReadRoomDetail();
            Enqueue(delegate
            {
                if (result == ServerResultCode.Ok)
                {
                    ApplyRoom(room);
                    StatusChanged?.Invoke(string.Format("방 생성 완료: #{0} {1}", room.RoomId, room.Name));
                }
                else
                {
                    StatusChanged?.Invoke("방 생성 실패: " + result);
                }
            });
        }

        private void HandleRoomJoinAck(PacketReader reader)
        {
            ServerResultCode result = (ServerResultCode)reader.ReadUInt16();
            ServerRoomDetail room = reader.ReadRoomDetail();
            Enqueue(delegate
            {
                if (result == ServerResultCode.Ok)
                {
                    ApplyRoom(room);
                    StatusChanged?.Invoke(string.Format("방 입장 완료: #{0} {1}", room.RoomId, room.Name));
                }
                else
                {
                    StatusChanged?.Invoke("방 입장 실패: " + result);
                }
            });
        }

        private void HandleRoomList(PacketReader reader)
        {
            List<ServerRoomSummary> rooms = reader.ReadList(r => r.ReadRoomSummary());
            uint totalCount = reader.ReadUInt32();
            ushort page = reader.ReadUInt16();
            Enqueue(delegate
            {
                LastRoomList = rooms;
                RoomListReceived?.Invoke(rooms, totalCount, page);
            });
        }

        private void HandleRoomLeaveAck(PacketReader reader)
        {
            ServerResultCode result = (ServerResultCode)reader.ReadUInt16();
            Enqueue(delegate
            {
                if (result == ServerResultCode.Ok)
                {
                    CurrentRoom = default(ServerRoomDetail);
                    RoomChanged?.Invoke(CurrentRoom);
                }

                StatusChanged?.Invoke("방 나가기: " + result);
            });
        }

        private void HandleRoomReadyAck(PacketReader reader)
        {
            ServerResultCode result = (ServerResultCode)reader.ReadUInt16();
            bool ready = reader.ReadBool();
            EnqueueStatus(string.Format("준비 {0}: {1}", ready ? "완료" : "해제", result));
        }

        private void HandleRoomStartAck(PacketReader reader)
        {
            ServerResultCode result = (ServerResultCode)reader.ReadUInt16();
            EnqueueStatus(result == ServerResultCode.Ok ? "게임 시작 요청 승인" : "게임 시작 실패: " + result);
        }

        private void HandleRoomConfigChanged(PacketReader reader)
        {
            ServerMatchConfig config = reader.ReadMatchConfig();
            uint effectiveItemMask = reader.ReadUInt32();
            bool accepted = reader.ReadBool();

            Enqueue(delegate
            {
                CurrentConfig = config;
                EffectiveItemMask = effectiveItemMask;
                RoomConfigChanged?.Invoke(config, effectiveItemMask);
                StatusChanged?.Invoke(string.Format("방 설정: {0}라운드, 탄약 {1}, 웨이브 {2}{3}",
                    config.Rounds,
                    config.AmmoPerWave == GameField.Unlimited ? "무제한" : config.AmmoPerWave.ToString(),
                    config.WaveLimit == GameField.Unlimited ? "무제한" : config.WaveLimit.ToString(),
                    accepted ? string.Empty : " (서버가 조정함)"));
            });
        }

        private void HandleRoomMemberJoined(PacketReader reader)
        {
            ServerRoomMember member = reader.ReadRoomMember();
            Enqueue(delegate
            {
                ServerRoomDetail room = CurrentRoom;
                List<ServerRoomMember> members = CloneMembers(room);
                members.RemoveAll(item => item.SessionId == member.SessionId);
                members.Add(member);
                room.Members = members;
                ApplyRoom(room);
                StatusChanged?.Invoke(member.Nickname + " 입장");
            });
        }

        private void HandleRoomMemberLeft(PacketReader reader)
        {
            ulong sessionId = reader.ReadUInt64();
            ServerLeaveReason reason = (ServerLeaveReason)reader.ReadByte();
            Enqueue(delegate
            {
                ServerRoomDetail room = CurrentRoom;
                List<ServerRoomMember> members = CloneMembers(room);
                members.RemoveAll(item => item.SessionId == sessionId);
                room.Members = members;
                ApplyRoom(room);
                StatusChanged?.Invoke(string.Format("session {0} 퇴장: {1}", sessionId, reason));
            });
        }

        private void HandleRoomMemberReady(PacketReader reader)
        {
            ulong sessionId = reader.ReadUInt64();
            bool ready = reader.ReadBool();
            Enqueue(delegate
            {
                ServerRoomDetail room = CurrentRoom;
                List<ServerRoomMember> members = CloneMembers(room);
                for (int i = 0; i < members.Count; i++)
                {
                    if (members[i].SessionId == sessionId)
                    {
                        ServerRoomMember member = members[i];
                        member.IsReady = ready;
                        members[i] = member;
                        break;
                    }
                }

                room.Members = members;
                ApplyRoom(room);
            });
        }

        private void HandleRoomHostChanged(PacketReader reader)
        {
            ulong hostSessionId = reader.ReadUInt64();
            Enqueue(delegate
            {
                ServerRoomDetail room = CurrentRoom;
                room.HostSessionId = hostSessionId;
                List<ServerRoomMember> members = CloneMembers(room);
                for (int i = 0; i < members.Count; i++)
                {
                    ServerRoomMember member = members[i];
                    member.IsHost = member.SessionId == hostSessionId;
                    members[i] = member;
                }

                room.Members = members;
                ApplyRoom(room);
                StatusChanged?.Invoke("방장 변경: session " + hostSessionId);
            });
        }

        private void HandleRoomKicked(PacketReader reader)
        {
            ulong roomId = reader.ReadUInt64();
            uint cooldownSeconds = reader.ReadUInt32();
            Enqueue(delegate
            {
                CurrentRoom = default(ServerRoomDetail);
                RoomChanged?.Invoke(CurrentRoom);
                StatusChanged?.Invoke(string.Format("#{0} 방에서 강퇴됨, {1}s 재입장 제한", roomId, cooldownSeconds));
            });
        }

        private void HandleRoomStateChanged(PacketReader reader)
        {
            ServerRoomState state = (ServerRoomState)reader.ReadByte();
            Enqueue(delegate
            {
                ServerRoomDetail room = CurrentRoom;
                room.State = state;
                ApplyRoom(room);
                StatusChanged?.Invoke("방 상태: " + state);
            });
        }

        private void HandleRoomClosed(PacketReader reader)
        {
            ulong roomId = reader.ReadUInt64();
            string reason = reader.ReadString();
            Enqueue(delegate
            {
                CurrentRoom = default(ServerRoomDetail);
                RoomChanged?.Invoke(CurrentRoom);
                StatusChanged?.Invoke(string.Format("#{0} 방 닫힘: {1}", roomId, reason));
            });
        }

        private void HandleBotChanged(PacketReader reader)
        {
            ServerResultCode result = (ServerResultCode)reader.ReadUInt16();
            ServerRoomMember bot = reader.ReadRoomMember();

            Enqueue(delegate
            {
                if (result != ServerResultCode.Ok)
                {
                    StatusChanged?.Invoke("봇 변경 실패: " + result);
                    BotChanged?.Invoke(result, bot);
                    return;
                }

                // Upsert: the same packet announces a new bot and a difficulty
                // change, and the roster should end up the same either way.
                ServerRoomDetail room = CurrentRoom;
                List<ServerRoomMember> members = CloneMembers(room);
                members.RemoveAll(item => item.SessionId == bot.SessionId);
                members.Add(bot);
                room.Members = members;
                ApplyRoom(room);

                BotChanged?.Invoke(result, bot);
                StatusChanged?.Invoke(string.Format("{0} 난이도 {1}", bot.Nickname, bot.BotDifficulty));
            });
        }

        private void HandleQuickMatchQueued(PacketReader reader)
        {
            ServerQuickMatchStatus status = new ServerQuickMatchStatus
            {
                Result = (ServerResultCode)reader.ReadUInt16(),
                Mode = (ServerGameMode)reader.ReadByte(),
                Waiting = reader.ReadByte(),
                Needed = reader.ReadByte(),
                Capacity = reader.ReadByte()
            };

            Enqueue(delegate
            {
                LastQuickMatch = status;
                IsQuickMatching = status.IsQueued;
                QuickMatchStatus?.Invoke(status);
                StatusChanged?.Invoke(status.IsQueued
                    ? string.Format("{0} 대기 중 {1}/{2}",
                        ServerProtocolText.ToDisplay(status.Mode), status.Waiting, status.Capacity)
                    : "빠른 대전 실패: " + status.Result);
            });
        }

        private void HandleQuickMatchCancelled(PacketReader reader)
        {
            ServerResultCode result = (ServerResultCode)reader.ReadUInt16();
            Enqueue(delegate
            {
                IsQuickMatching = false;
                QuickMatchCancelled?.Invoke(result);
                StatusChanged?.Invoke(result == ServerResultCode.Ok
                    ? "빠른 대전 취소됨"
                    : "취소할 대기열 없음");
            });
        }

        private void HandleGameStarting(PacketReader reader)
        {
            ServerGameStartingInfo info = new ServerGameStartingInfo
            {
                InstanceId = reader.ReadUInt64(),
                Host = reader.ReadString(),
                Port = reader.ReadUInt16(),
                Ticket = reader.ReadString()
            };

            Enqueue(delegate
            {
                // Whether we queued or came from a room, the wait is over.
                IsQuickMatching = false;
                GameStarting?.Invoke(info);
                StatusChanged?.Invoke(string.Format("게임 인스턴스 준비: {0}:{1}", info.Host, info.Port));
            });
        }

        private void ApplyRoom(ServerRoomDetail room)
        {
            if (room.Members == null)
            {
                room.Members = new List<ServerRoomMember>();
            }

            CurrentRoom = room;
            RoomChanged?.Invoke(CurrentRoom);
        }

        private static List<ServerRoomMember> CloneMembers(ServerRoomDetail room)
        {
            return room.Members != null ? new List<ServerRoomMember>(room.Members) : new List<ServerRoomMember>();
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

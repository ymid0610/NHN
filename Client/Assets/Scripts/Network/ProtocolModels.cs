using System;
using System.Collections.Generic;

namespace NHN.Network
{
    internal enum ServerPacketId : ushort
    {
        None = 0,

        C_Hello = 1000,
        S_HelloAck = 1001,
        C_Ping = 1002,
        S_Pong = 1003,
        S_Error = 1004,

        C_RoomCreate = 1010,
        S_RoomCreateAck = 1011,
        C_RoomList = 1012,
        S_RoomList = 1013,
        C_RoomJoin = 1014,
        S_RoomJoinAck = 1015,
        C_RoomLeave = 1016,
        S_RoomLeaveAck = 1017,
        C_RoomKick = 1018,
        S_RoomKickAck = 1019,
        C_RoomReady = 1020,
        S_RoomReadyAck = 1021,
        C_RoomStart = 1022,
        S_RoomStartAck = 1023,
        C_QuickMatch = 1024,
        C_RoomSetConfig = 1025,
        S_RoomConfigChanged = 1026,
        C_QuickMatchCancel = 1027,
        S_QuickMatchQueued = 1028,
        S_QuickMatchCancelled = 1029,

        S_RoomMemberJoined = 1030,
        S_RoomMemberLeft = 1031,
        S_RoomMemberReady = 1032,
        S_RoomHostChanged = 1033,
        S_RoomKicked = 1034,
        S_RoomStateChanged = 1035,
        S_RoomClosed = 1036,
        S_GameStarting = 1037,

        C_RoomAddBot = 1040,
        C_RoomRemoveBot = 1041,
        C_RoomSetBotDifficulty = 1042,
        S_RoomBotChanged = 1043,

        // -- client <-> instance ------------------------------------------------
        C_InstHello = 3000,
        S_InstHelloAck = 3001,
        C_InstLeave = 3002,
        S_InstMemberJoined = 3003,
        S_InstMemberLeft = 3004,
        S_InstStart = 3005,
        S_InstEnd = 3006,

        // -- the game -----------------------------------------------------------
        C_Fire = 3100,
        S_FireRejected = 3101,
        S_ShotResolved = 3102,
        S_MatchSetup = 3110,
        S_RoundStart = 3111,
        S_RoundEnd = 3112,
        S_WaveStart = 3113,
        S_WaveEnd = 3114,
        S_WorldSnapshot = 3115,
        S_CellClaimed = 3116,
        S_ItemTriggered = 3117,
        S_StatusChanged = 3118,
        S_AmmoChanged = 3119,
        S_GameOver = 3120
    }

    public enum ServerGameMode : byte
    {
        None = 0,
        TicTacToe = 1,
        Gomoku9 = 2,
        Gomoku15 = 3
    }

    public enum ServerRoomState : byte
    {
        Waiting = 0,
        Starting = 1,
        InGame = 2,
        Closing = 3
    }

    public enum ServerLeaveReason : byte
    {
        Left = 0,
        Disconnected = 1,
        Kicked = 2,
        RoomClosed = 3,
        MovedToInstance = 4
    }

    public enum ServerResultCode : ushort
    {
        Ok = 0,

        UnknownError = 1,
        InvalidRequest = 2,
        NotAuthenticated = 3,
        InternalError = 4,
        ServerFull = 5,
        RateLimited = 6,

        NicknameInvalid = 100,
        AlreadyAuthenticated = 101,

        RoomNotFound = 200,
        RoomFull = 201,
        AlreadyInRoom = 202,
        NotInRoom = 203,
        WrongPassword = 204,
        PasswordRequired = 205,
        RoomNameInvalid = 206,
        GameModeInvalid = 207,
        RoomNotWaiting = 208,
        KickCooldown = 209,

        NotHost = 300,
        CannotTargetSelf = 301,
        TargetNotFound = 302,
        NotEnoughPlayers = 303,
        NotAllReady = 304,

        TicketInvalid = 400,
        TicketExpired = 401,
        TicketAlreadyUsed = 402,

        NoInstanceServer = 500,
        InstanceCreateFailed = 501,
        InstanceNotFound = 502
    }

    [Serializable]
    public struct ServerMatchConfig
    {
        public const uint AllItemsMask = 0xFE;

        public byte Rounds;
        public byte PaperSizeMin;
        public byte PaperSizeMax;
        public byte AmmoPerWave;
        public byte WaveLimit;
        public uint ItemMask;

        public static ServerMatchConfig CreateDefault(ServerGameMode mode, bool itemsEnabled)
        {
            return new ServerMatchConfig
            {
                Rounds = 3,
                PaperSizeMin = 2,
                PaperSizeMax = 4,
                AmmoPerWave = mode == ServerGameMode.TicTacToe ? (byte)1 : (byte)6,
                WaveLimit = mode == ServerGameMode.Gomoku15 ? (byte)20 : (byte)10,
                ItemMask = itemsEnabled ? AllItemsMask : 0u
            };
        }
    }

    [Serializable]
    public struct ServerRoomMember
    {
        public ulong SessionId;
        public string Nickname;
        public byte Slot;
        public bool IsHost;
        public bool IsReady;
        /// 0 for a person, 1..5 for a bot.
        public byte BotDifficulty;

        public bool IsBot
        {
            get { return BotDifficulty != 0; }
        }
    }

    public static class BotLimits
    {
        public const byte MinDifficulty = 1;
        public const byte MaxDifficulty = 5;
        public const byte DefaultDifficulty = 3;
    }

    [Serializable]
    public struct ServerRoomSummary
    {
        public ulong RoomId;
        public string Name;
        public ServerGameMode Mode;
        public ServerRoomState State;
        public byte MemberCount;
        public byte Capacity;
        public bool HasPassword;
        public string HostNickname;
    }

    [Serializable]
    public struct ServerRoomDetail
    {
        public ulong RoomId;
        public string Name;
        public ServerGameMode Mode;
        public ServerRoomState State;
        public byte Capacity;
        public bool HasPassword;
        public ulong HostSessionId;
        public List<ServerRoomMember> Members;

        public bool IsValid
        {
            get { return RoomId != 0; }
        }
    }

    /// <summary>
    /// Progress in the quick-match queue.
    ///
    /// There is no room behind this: the next thing to arrive after enough
    /// players have gathered is S_GameStarting.
    /// </summary>
    [Serializable]
    public struct ServerQuickMatchStatus
    {
        public ServerResultCode Result;
        public ServerGameMode Mode;
        /// Players waiting for this mode, including us.
        public byte Waiting;
        /// Enough to start once the server's fill window closes.
        public byte Needed;
        /// Starts at once on reaching this many.
        public byte Capacity;

        /// <summary>
        /// True only for a status the server actually sent.
        ///
        /// The mode is part of the test on purpose: Ok is zero, so a
        /// default-constructed status would otherwise read as "queued" before
        /// any reply had arrived, and a screen checking this on open would show
        /// a queue nobody had joined.
        /// </summary>
        public bool IsQueued
        {
            get { return Result == ServerResultCode.Ok && Mode != ServerGameMode.None; }
        }
    }

    [Serializable]
    public struct ServerGameStartingInfo
    {
        public ulong InstanceId;
        public string Host;
        public ushort Port;
        public string Ticket;
    }

    public static class ServerProtocolText
    {
        public static string ToDisplay(ServerResultCode result)
        {
            return result.ToString();
        }

        public static string ToDisplay(ServerGameMode mode)
        {
            switch (mode)
            {
                case ServerGameMode.TicTacToe:
                    return "TicTacToe";
                case ServerGameMode.Gomoku9:
                    return "Gomoku 9x9";
                case ServerGameMode.Gomoku15:
                    return "Gomoku 15x15";
                default:
                    return "Any";
            }
        }

        public static string ToRoomLine(ServerRoomSummary room)
        {
            string locked = room.HasPassword ? " locked" : string.Empty;
            return string.Format("#{0} {1} / {2} / {3}/{4}{5} / host {6}",
                room.RoomId,
                room.Name,
                ToDisplay(room.Mode),
                room.MemberCount,
                room.Capacity,
                locked,
                room.HostNickname);
        }
    }
}

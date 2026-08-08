using System;
using System.Collections.Generic;

namespace NHN.Network
{
    /// <summary>
    /// The instance server's game vocabulary, mirroring Server/Protocol/include/protocol/GameTypes.h.
    ///
    /// Every value here is on the wire, so the numbers are the contract — do not
    /// renumber anything without changing the C++ side in the same commit.
    /// </summary>
    public static class GameField
    {
        /// The server works in a fixed pixel space and the client maps its
        /// viewport onto it, which keeps aiming resolution-independent.
        ///
        /// Y INCREASES DOWNWARD. The target sheet enters above the field at a
        /// negative y and leaves below it at y > Height, so anything mapping
        /// this onto Unity world space has to flip.
        public const int Width = 1920;
        public const int Height = 1080;

        /// How far back a shot may claim to have happened, in milliseconds.
        public const uint MaxRewindMs = 300;

        /// Ammo and wave-limit sentinel. Zero means "no limit", not "none left".
        public const byte Unlimited = 0;

        /// Cell size in field units for paper sizes 1..5, matching World.cpp.
        /// Difficulty is the size of a cell, so it means the same thing whatever
        /// the board dimensions are.
        public static float CellSizeFor(byte paperSize)
        {
            byte clamped = paperSize < 1 ? (byte)1 : paperSize > 5 ? (byte)5 : paperSize;
            return 24f + clamped * 8f;
        }
    }

    public enum ServerItemKind : byte
    {
        None = 0,
        Tumbleweed = 1,   // blocks shots, nothing else
        WantedPoster = 2, // decoy target
        FryingPan = 3,    // ricochets, stuns someone
        Balloon = 4,      // pops, shoves nearby entities
        PaintCan = 5,     // pops, blinds someone
        Grenade = 6,      // fragments into further shots
        SpeedLoader = 7   // refills the shooter's ammo
    }

    /// What a shot ended up hitting.
    public enum ServerHitKind : byte
    {
        Miss = 0,
        Cell = 1,       // scored a mark on the board
        CellTaken = 2,  // someone else got there first
        Decoy = 3,      // wanted poster: ammo spent, nothing else
        Blocker = 4,    // tumbleweed absorbed it
        Item = 5
    }

    [Flags]
    public enum ServerStatusFlags : byte
    {
        None = 0,
        /// Cannot fire. Clears at the end of the wave it was applied in.
        Stunned = 1 << 0,
        /// View obscured. Clears at the end of the *following* wave.
        Blinded = 1 << 1
    }

    public enum ServerEntityKind : byte
    {
        None = 0,
        TargetSheet = 1,
        Item = 2
    }

    /// One entity's replicated transform, in field space.
    [Serializable]
    public struct ServerEntityState
    {
        public uint EntityId;
        public ServerEntityKind Kind;
        public ServerItemKind Item; // meaningful when Kind == Item
        public int X;
        public int Y;
        /// Half-extents, so a client can draw and re-check a hit.
        public int HalfWidth;
        public int HalfHeight;
    }

    /// Fixed description of the board for a match.
    [Serializable]
    public struct ServerBoardSpec
    {
        public byte Width;
        public byte Height;
        public byte WinLength;

        public int CellCount
        {
            get { return Width * Height; }
        }

        public bool IsValid
        {
            get { return Width > 0 && Height > 0; }
        }

        /// The server lays cells out row-major with y running DOWNWARD from the
        /// top of the sheet: cellIndex = y * width + x.
        public int CellIndex(int x, int y)
        {
            return y * Width + x;
        }

        public void SplitCellIndex(int cellIndex, out int x, out int y)
        {
            if (Width <= 0)
            {
                x = 0;
                y = 0;
                return;
            }

            x = cellIndex % Width;
            y = cellIndex / Width;
        }
    }

    [Serializable]
    public struct ServerPlayerScore
    {
        public ulong SessionId;
        public byte Slot;
        public byte Score;
    }

    /// Sent once when the match begins, so a client knows the shape of
    /// everything that follows.
    [Serializable]
    public struct ServerMatchSetup
    {
        public ServerGameMode Mode;
        public ServerMatchConfig Config;
        public ServerBoardSpec Board;
        public uint FieldWidth;
        public uint FieldHeight;
        public uint WaveDurationMs;
        public List<ServerRoomMember> Players;

        public bool IsValid
        {
            get { return Board.IsValid; }
        }
    }

    [Serializable]
    public struct ServerRoundStart
    {
        public byte RoundIndex;
        public byte RoundCount;
        /// Cell size band index for this round, drawn from the configured range.
        public byte PaperSize;
        public ulong RoundSeed;
    }

    [Serializable]
    public struct ServerRoundEnd
    {
        public byte RoundIndex;
        /// 0xFF when the round ended with no line completed; nobody scores.
        public byte WinnerSlot;
        public List<ServerPlayerScore> Scores;
    }

    [Serializable]
    public struct ServerWaveStart
    {
        public ushort WaveIndex;
        public uint StartTick;
        public uint EndTick;
        public byte Ammo; // GameField.Unlimited allowed
    }

    /// The outcome of one shot, as decided by the server.
    [Serializable]
    public struct ServerShotResolved
    {
        public ulong Shooter;
        public ushort Sequence;
        public ServerHitKind Kind;
        public int HitX;
        public int HitY;
        public uint EntityId;
        /// Board cell when Kind is Cell or CellTaken, otherwise NoCell.
        public ushort CellIndex;
        /// True when this came from a grenade fragment or a ricochet rather than
        /// a trigger pull, so the client does not animate a muzzle flash.
        public bool Derived;

        public const ushort NoCell = 0xFFFF;

        public bool HasCell
        {
            get { return CellIndex != NoCell; }
        }
    }

    /// An item went off. The server decides who it landed on; the seed lets each
    /// client draw the same splatter without needing the geometry.
    [Serializable]
    public struct ServerItemTriggered
    {
        public ServerItemKind Item;
        public uint EntityId;
        public ulong Instigator;
        /// NoVictim when the item does not single anyone out.
        public byte VictimSlot;
        public uint Seed;

        public const byte NoVictim = 0xFF;
    }

    [Serializable]
    public struct ServerGameOver
    {
        /// More than one entry when the match ends level; scoreless rounds make
        /// that reachable even with an odd round count.
        public List<byte> WinnerSlots;
        public List<ServerPlayerScore> Scores;
    }

    public static class GameSlots
    {
        /// The server's Board::kNoOwner. Also the "no winner" marker in
        /// S_RoundEnd and the "nobody in particular" marker in S_ItemTriggered.
        public const byte NoOwner = 0xFF;
    }
}

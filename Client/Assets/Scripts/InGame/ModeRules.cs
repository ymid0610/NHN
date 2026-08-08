using NHN.Network;

namespace NHN.InGame
{
    /// <summary>
    /// The board rules for each mode, mirroring the server's GameModeDef table
    /// in Server/Protocol/src/Protocol.cpp.
    ///
    /// The server is the authority — it decides every cell and every win — so
    /// nothing here changes the outcome of a networked match. It exists so the
    /// menu can size a board preview and the offline practice mode can play by
    /// the same rules the real game does, instead of inventing its own.
    ///
    /// Keep it in step with the C++ table. There is no code generator, so a
    /// board size changed on one side and not the other shows up as a practice
    /// mode that quietly disagrees with live play.
    /// </summary>
    public struct ModeRules
    {
        public ServerGameMode Mode;
        public int BoardSize;
        /// Marks in a row needed to take the round. Six also wins — the server
        /// tests `run >= winLength`, so there is no overline rule.
        public int WinLength;
        public int Capacity;
        public int MinimumToStart;
        /// Ammo per wave this mode starts with, matching DefaultMatchConfig.
        public int DefaultAmmo;
        public string DisplayName;

        public static ModeRules For(ServerGameMode mode)
        {
            switch (mode)
            {
                case ServerGameMode.TicTacToe:
                    return new ModeRules
                    {
                        Mode = ServerGameMode.TicTacToe,
                        BoardSize = 3,
                        WinLength = 3,
                        Capacity = 2,
                        MinimumToStart = 2,
                        DefaultAmmo = 1,
                        DisplayName = "틱택토"
                    };

                case ServerGameMode.Gomoku15:
                    return new ModeRules
                    {
                        Mode = ServerGameMode.Gomoku15,
                        BoardSize = 15,
                        WinLength = 5,
                        Capacity = 4,
                        MinimumToStart = 2,
                        DefaultAmmo = 6,
                        DisplayName = "오목 15x15"
                    };

                // Gomoku9 is the fallback: it is the middle mode, and a stale
                // PlayerPrefs value from before the modes were renumbered lands
                // here rather than on something unplayable.
                default:
                    return new ModeRules
                    {
                        Mode = ServerGameMode.Gomoku9,
                        BoardSize = 9,
                        WinLength = 4,
                        Capacity = 4,
                        MinimumToStart = 2,
                        DefaultAmmo = 6,
                        DisplayName = "오목 9x9"
                    };
            }
        }

        /// Every mode a player can pick, in menu order.
        public static readonly ServerGameMode[] Playable =
        {
            ServerGameMode.TicTacToe,
            ServerGameMode.Gomoku9,
            ServerGameMode.Gomoku15
        };

        public static ServerGameMode Sanitise(ServerGameMode mode)
        {
            for (int i = 0; i < Playable.Length; i++)
            {
                if (Playable[i] == mode)
                {
                    return mode;
                }
            }

            return ServerGameMode.Gomoku9;
        }

        /// True for the modes drawn with the small gomoku marker rather than the
        /// large tic-tac-toe one.
        public bool UsesSmallMarker
        {
            get { return Mode != ServerGameMode.TicTacToe; }
        }
    }
}

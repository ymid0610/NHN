using System.Collections.Generic;
using UnityEngine;

namespace NHN.InGame
{
    public sealed class GomokuBoardState
    {
        private readonly int[,] cells;

        public GomokuBoardState(int size)
        {
            Size = Mathf.Max(3, size);
            cells = new int[Size, Size];
        }

        public int Size { get; }

        public bool IsInside(Vector2Int cell)
        {
            return cell.x >= 0 && cell.x < Size && cell.y >= 0 && cell.y < Size;
        }

        public int GetOwner(Vector2Int cell)
        {
            return IsInside(cell) ? cells[cell.x, cell.y] : -1;
        }

        /// <summary>
        /// Claims a cell for a player, first come first served.
        ///
        /// There is no "allow re-shooting" option: the server's Board::Claim
        /// refuses a cell that already has an owner, and in simultaneous play
        /// that rule *is* the contest.
        /// </summary>
        public bool TryPlace(Vector2Int cell, int player, out string reason)
        {
            if (!IsInside(cell))
            {
                reason = "Out of board";
                return false;
            }

            if (cells[cell.x, cell.y] != 0)
            {
                reason = "Already shot";
                return false;
            }

            cells[cell.x, cell.y] = player;
            reason = string.Empty;
            return true;
        }

        public bool HasWinnerFrom(Vector2Int origin, int player, int winLength)
        {
            return TryGetWinningLine(origin, player, winLength, null);
        }

        /// <summary>
        /// Finds a completed line through @p origin.
        ///
        /// A run longer than winLength counts. The server tests
        /// `run >= _winLength` in Board::CompletesLine, so six in a row is a win
        /// and there is no overline restriction to mirror.
        /// </summary>
        public bool TryGetWinningLine(Vector2Int origin, int player, int winLength, List<Vector2Int> winningCells)
        {
            if (!IsInside(origin) || player <= 0)
            {
                winningCells?.Clear();
                return false;
            }

            Vector2Int[] directions =
            {
                new Vector2Int(1, 0),
                new Vector2Int(0, 1),
                new Vector2Int(1, 1),
                new Vector2Int(1, -1)
            };

            foreach (Vector2Int direction in directions)
            {
                if (TryCollectWinningLine(origin, player, direction, winLength, winningCells))
                {
                    return true;
                }
            }

            winningCells?.Clear();
            return false;
        }

        private bool TryCollectWinningLine(Vector2Int origin, int player, Vector2Int direction, int winLength, List<Vector2Int> winningCells)
        {
            List<Vector2Int> line = new List<Vector2Int>();
            Vector2Int reverse = new Vector2Int(-direction.x, -direction.y);
            Vector2Int current = origin;

            while (IsInside(current + reverse) && cells[current.x + reverse.x, current.y + reverse.y] == player)
            {
                current += reverse;
            }

            int originIndex = 0;
            while (IsInside(current) && cells[current.x, current.y] == player)
            {
                if (current == origin)
                {
                    originIndex = line.Count;
                }

                line.Add(current);
                current += direction;
            }

            if (line.Count < winLength)
            {
                return false;
            }

            if (winningCells != null)
            {
                winningCells.Clear();
                int halfWindow = Mathf.Max(0, winLength / 2);
                int startIndex = Mathf.Clamp(originIndex - halfWindow, 0, Mathf.Max(0, line.Count - winLength));
                for (int i = 0; i < winLength; i++)
                {
                    winningCells.Add(line[startIndex + i]);
                }
            }

            return true;
        }

        private int Count(Vector2Int origin, int player, Vector2Int direction)
        {
            int count = 0;
            Vector2Int current = origin + direction;

            while (IsInside(current) && cells[current.x, current.y] == player)
            {
                count++;
                current += direction;
            }

            return count;
        }
    }
}

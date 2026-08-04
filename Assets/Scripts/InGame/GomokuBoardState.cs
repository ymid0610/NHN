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

        public bool TryPlace(Vector2Int cell, int player, bool blockOccupiedCells, out string reason)
        {
            if (!IsInside(cell))
            {
                reason = "Out of board";
                return false;
            }

            if (blockOccupiedCells && cells[cell.x, cell.y] != 0)
            {
                reason = "Already shot";
                return false;
            }

            cells[cell.x, cell.y] = player;
            reason = string.Empty;
            return true;
        }

        public bool HasWinnerFrom(Vector2Int origin, int player, int winLength, bool allowOverline)
        {
            if (!IsInside(origin) || player <= 0)
            {
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
                Vector2Int oppositeDirection = new Vector2Int(-direction.x, -direction.y);
                int count = 1 + Count(origin, player, direction) + Count(origin, player, oppositeDirection);
                if (allowOverline)
                {
                    if (count >= winLength)
                    {
                        return true;
                    }
                }
                else if (count == winLength)
                {
                    return true;
                }
            }

            return false;
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

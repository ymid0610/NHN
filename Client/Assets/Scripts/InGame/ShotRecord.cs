using UnityEngine;

namespace NHN.InGame
{
    public struct ShotRecord
    {
        public ShotRecord(Vector2Int cell, int player)
        {
            this.cell = cell;
            this.player = player;
        }

        public Vector2Int cell;
        public int player;
    }
}

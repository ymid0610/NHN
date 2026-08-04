using UnityEngine;

namespace NHN.InGame
{
    public sealed class PaperBoardWarp : MonoBehaviour
    {
        public PaperBoardTarget boardTarget;
        public PaperWindMotion windMotion;
        public bool enableWarp = true;
        public float phaseSpeed = 2.7f;
        public float horizontalBend = 0.16f;
        public float verticalBend = 0.1f;
        public float diagonalTwist = 0.08f;
        public float markerRotation = 10f;
        public float markerStretch = 0.16f;
        public float markerSquash = 0.12f;

        public Vector3 CellToLocal(Vector2Int cell)
        {
            if (boardTarget == null)
            {
                return Vector3.zero;
            }

            return GridNormalizedToLocal(boardTarget.CellToGridNormalized(cell));
        }

        public Vector3 GridNormalizedToLocal(Vector2 normalized)
        {
            if (boardTarget == null)
            {
                return Vector3.zero;
            }

            Vector3 flat = boardTarget.GridNormalizedToLocal(normalized);
            if (!ShouldWarp())
            {
                return flat;
            }

            float phase = GetPhase();
            float x = normalized.x * 2f - 1f;
            float y = normalized.y * 2f - 1f;
            float edgeWeight = Mathf.Lerp(0.35f, 1f, Mathf.Max(Mathf.Abs(x), Mathf.Abs(y)));

            float horizontal = Mathf.Sin(phase + y * Mathf.PI * 1.15f) * horizontalBend * edgeWeight;
            float vertical = Mathf.Sin(phase * 0.85f + x * Mathf.PI * 1.05f) * verticalBend * edgeWeight;
            float twistX = y * Mathf.Sin(phase * 0.7f + x * Mathf.PI) * diagonalTwist;
            float twistY = x * Mathf.Cos(phase * 0.65f + y * Mathf.PI) * diagonalTwist * 0.7f;

            flat.x += horizontal + twistX;
            flat.y += vertical + twistY;
            return flat;
        }

        public float CellRotationDegrees(Vector2Int cell)
        {
            if (boardTarget == null || !ShouldWarp())
            {
                return 0f;
            }

            Vector2 normalized = boardTarget.CellToGridNormalized(cell);
            float delta = boardTarget.boardSize <= 1 ? 0.01f : 1f / (boardTarget.boardSize - 1);
            Vector2 left = new Vector2(Mathf.Clamp01(normalized.x - delta), normalized.y);
            Vector2 right = new Vector2(Mathf.Clamp01(normalized.x + delta), normalized.y);
            Vector3 tangent = GridNormalizedToLocal(right) - GridNormalizedToLocal(left);
            float tangentAngle = Mathf.Atan2(tangent.y, tangent.x) * Mathf.Rad2Deg;
            float phaseLean = Mathf.Sin(GetPhase() + normalized.y * Mathf.PI * 2f) * markerRotation * 0.35f;
            return tangentAngle + phaseLean;
        }

        public Vector3 CellVisualScale(Vector2Int cell, Vector3 baseScale)
        {
            if (boardTarget == null || !ShouldWarp())
            {
                return baseScale;
            }

            Vector2 normalized = boardTarget.CellToGridNormalized(cell);
            float phase = GetPhase();
            float x = normalized.x * 2f - 1f;
            float y = normalized.y * 2f - 1f;
            float fold = Mathf.Sin(phase + x * Mathf.PI * 1.2f + y * Mathf.PI * 0.35f);
            float stretchX = 1f + fold * markerStretch;
            float squashY = 1f - Mathf.Abs(fold) * markerSquash;
            return new Vector3(baseScale.x * stretchX, baseScale.y * squashY, baseScale.z);
        }

        private bool ShouldWarp()
        {
            return enableWarp && (windMotion == null || !windMotion.IsResultView);
        }

        private float GetPhase()
        {
            if (windMotion != null)
            {
                return windMotion.MotionPhase * phaseSpeed;
            }

            return Time.time * phaseSpeed;
        }
    }
}

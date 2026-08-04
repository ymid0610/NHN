using UnityEngine;

namespace NHN.InGame
{
    public sealed class PaperBoardTarget : MonoBehaviour
    {
        [Min(3)] public int boardSize = 15;
        [Min(0.1f)] public Vector2 boardWorldSize = new Vector2(3.6f, 3.6f);
        [Range(0f, 0.35f)] public float gridInsetNormalized = 0.09f;
        public Rect gridNormalizedRect = new Rect(0.1f, 0.1f, 0.8f, 0.8f);
        public bool drawDebugGrid = true;

        public Rect GridNormalizedRect => GetSanitizedGridNormalizedRect();

        public bool TryWorldToCell(Vector3 worldPosition, out Vector2Int cell)
        {
            Vector3 local = transform.InverseTransformPoint(worldPosition);
            Rect boardRect = GetBoardRect();

            if (!boardRect.Contains(new Vector2(local.x, local.y)))
            {
                cell = default;
                return false;
            }

            Rect gridRect = GetGridRect();
            float normalizedX = Mathf.InverseLerp(gridRect.xMin, gridRect.xMax, local.x);
            float normalizedY = Mathf.InverseLerp(gridRect.yMin, gridRect.yMax, local.y);

            if (normalizedX < 0f || normalizedX > 1f || normalizedY < 0f || normalizedY > 1f)
            {
                cell = default;
                return false;
            }

            int x = Mathf.RoundToInt(normalizedX * (boardSize - 1));
            int y = Mathf.RoundToInt(normalizedY * (boardSize - 1));
            cell = new Vector2Int(Mathf.Clamp(x, 0, boardSize - 1), Mathf.Clamp(y, 0, boardSize - 1));
            return true;
        }

        public Vector3 CellToWorld(Vector2Int cell)
        {
            return transform.TransformPoint(CellToLocal(cell));
        }

        public Vector3 CellToLocal(Vector2Int cell)
        {
            return GridNormalizedToLocal(CellToGridNormalized(cell));
        }

        public Vector2 CellToGridNormalized(Vector2Int cell)
        {
            float normalizedX = boardSize <= 1 ? 0f : cell.x / (float)(boardSize - 1);
            float normalizedY = boardSize <= 1 ? 0f : cell.y / (float)(boardSize - 1);
            return new Vector2(normalizedX, normalizedY);
        }

        public Vector3 GridNormalizedToLocal(Vector2 normalized)
        {
            Rect gridRect = GetGridRect();
            return new Vector3(
                Mathf.Lerp(gridRect.xMin, gridRect.xMax, normalized.x),
                Mathf.Lerp(gridRect.yMin, gridRect.yMax, normalized.y),
                -0.02f);
        }

        private Rect GetBoardRect()
        {
            return new Rect(
                -boardWorldSize.x * 0.5f,
                -boardWorldSize.y * 0.5f,
                boardWorldSize.x,
                boardWorldSize.y);
        }

        private Rect GetGridRect()
        {
            Rect boardRect = GetBoardRect();
            Rect normalizedRect = GetSanitizedGridNormalizedRect();

            return new Rect(
                boardRect.xMin + boardRect.width * normalizedRect.xMin,
                boardRect.yMin + boardRect.height * normalizedRect.yMin,
                boardRect.width * normalizedRect.width,
                boardRect.height * normalizedRect.height);
        }

        private Rect GetSanitizedGridNormalizedRect()
        {
            if (gridNormalizedRect.width <= 0f || gridNormalizedRect.height <= 0f)
            {
                float inset = Mathf.Clamp01(gridInsetNormalized);
                return new Rect(inset, inset, 1f - inset * 2f, 1f - inset * 2f);
            }

            float xMin = Mathf.Clamp01(gridNormalizedRect.xMin);
            float yMin = Mathf.Clamp01(gridNormalizedRect.yMin);
            float xMax = Mathf.Clamp01(gridNormalizedRect.xMax);
            float yMax = Mathf.Clamp01(gridNormalizedRect.yMax);

            if (xMax <= xMin)
            {
                xMin = Mathf.Clamp01(gridInsetNormalized);
                xMax = 1f - xMin;
            }

            if (yMax <= yMin)
            {
                yMin = Mathf.Clamp01(gridInsetNormalized);
                yMax = 1f - yMin;
            }

            return Rect.MinMaxRect(xMin, yMin, xMax, yMax);
        }

        private void OnDrawGizmosSelected()
        {
            if (!drawDebugGrid || boardSize < 2)
            {
                return;
            }

            Rect gridRect = GetGridRect();
            Gizmos.color = new Color(0.1f, 0.9f, 0.3f, 0.45f);

            for (int i = 0; i < boardSize; i++)
            {
                float t = i / (float)(boardSize - 1);
                DrawLocalLine(new Vector3(Mathf.Lerp(gridRect.xMin, gridRect.xMax, t), gridRect.yMin, 0f),
                    new Vector3(Mathf.Lerp(gridRect.xMin, gridRect.xMax, t), gridRect.yMax, 0f));
                DrawLocalLine(new Vector3(gridRect.xMin, Mathf.Lerp(gridRect.yMin, gridRect.yMax, t), 0f),
                    new Vector3(gridRect.xMax, Mathf.Lerp(gridRect.yMin, gridRect.yMax, t), 0f));
            }
        }

        private void DrawLocalLine(Vector3 from, Vector3 to)
        {
            Gizmos.DrawLine(transform.TransformPoint(from), transform.TransformPoint(to));
        }
    }
}

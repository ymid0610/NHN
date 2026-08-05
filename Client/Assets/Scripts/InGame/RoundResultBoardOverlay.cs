using System.Collections.Generic;
using UnityEngine;

namespace NHN.InGame
{
    public sealed class RoundResultBoardOverlay : MonoBehaviour
    {
        public SpriteRenderer boardRenderer;
        public Transform markerRoot;
        public Sprite bulletHoleSprite;
        public Vector2 boardWorldSize = new Vector2(6.6f, 6.6f);
        [Range(0f, 0.35f)] public float gridInsetNormalized = 0.1f;
        public Rect gridNormalizedRect = new Rect(0.1f, 0.1f, 0.8f, 0.8f);
        public float gomokuMarkerWorldSize = 0.34f;
        public float ticTacToeMarkerWorldSize = 1.25f;
        public int markerSortingBase = 230;
        public Color[] playerColors =
        {
            new Color(0.3f, 1f, 0.35f, 1f),
            new Color(1f, 0.25f, 0.2f, 1f),
            new Color(0.25f, 0.55f, 1f, 1f),
            new Color(1f, 0.9f, 0.25f, 1f)
        };

        public bool IsVisible => gameObject.activeSelf;

        public void Show(Sprite boardSprite, IReadOnlyList<ShotRecord> shots, int boardSize, GameMode mode)
        {
            gameObject.SetActive(true);
            ClearMarkers();

            if (boardRenderer != null)
            {
                boardRenderer.sprite = boardSprite;
                boardRenderer.sortingOrder = markerSortingBase - 1;
                ScaleBoardVisual();
            }

            if (shots == null || bulletHoleSprite == null)
            {
                return;
            }

            for (int i = 0; i < shots.Count; i++)
            {
                SpawnMarker(shots[i], boardSize, mode);
            }
        }

        public void Hide()
        {
            ClearMarkers();
            gameObject.SetActive(false);
        }

        private void ScaleBoardVisual()
        {
            if (boardRenderer.sprite == null)
            {
                return;
            }

            float spriteSize = Mathf.Max(boardRenderer.sprite.bounds.size.x, boardRenderer.sprite.bounds.size.y, 0.01f);
            boardRenderer.transform.localScale = Vector3.one * (Mathf.Max(boardWorldSize.x, boardWorldSize.y) / spriteSize);
        }

        private void SpawnMarker(ShotRecord shot, int boardSize, GameMode mode)
        {
            GameObject marker = new GameObject($"Result_P{shot.player}_{shot.cell.x}_{shot.cell.y}");
            Transform markerTransform = marker.transform;
            markerTransform.SetParent(markerRoot != null ? markerRoot : transform, false);
            markerTransform.localPosition = CellToLocal(shot.cell, boardSize);
            markerTransform.localRotation = Quaternion.identity;

            GameObject visual = new GameObject("Bullet Hole Visual");
            Transform visualTransform = visual.transform;
            visualTransform.SetParent(markerTransform, false);

            SpriteRenderer renderer = visual.AddComponent<SpriteRenderer>();
            renderer.sprite = bulletHoleSprite;
            renderer.color = GetPlayerColor(shot.player);
            renderer.sortingOrder = markerSortingBase + shot.player;

            float targetSize = mode == GameMode.Gomoku ? gomokuMarkerWorldSize : ticTacToeMarkerWorldSize;
            float sourceSize = Mathf.Max(bulletHoleSprite.bounds.size.x, bulletHoleSprite.bounds.size.y, 0.01f);
            visualTransform.localScale = Vector3.one * (targetSize / sourceSize);

            SpriteBoundsCenterer centerer = visual.AddComponent<SpriteBoundsCenterer>();
            centerer.spriteRenderer = renderer;
            centerer.CenterNow();
        }

        private Vector3 CellToLocal(Vector2Int cell, int boardSize)
        {
            Rect gridRect = GetGridRect();
            float x = boardSize <= 1 ? 0f : cell.x / (float)(boardSize - 1);
            float y = boardSize <= 1 ? 0f : cell.y / (float)(boardSize - 1);

            return new Vector3(
                Mathf.Lerp(gridRect.xMin, gridRect.xMax, x),
                Mathf.Lerp(gridRect.yMin, gridRect.yMax, y),
                -0.05f);
        }

        private Rect GetGridRect()
        {
            Rect normalizedRect = GetSanitizedGridNormalizedRect();
            return new Rect(
                -boardWorldSize.x * 0.5f + boardWorldSize.x * normalizedRect.xMin,
                -boardWorldSize.y * 0.5f + boardWorldSize.y * normalizedRect.yMin,
                boardWorldSize.x * normalizedRect.width,
                boardWorldSize.y * normalizedRect.height);
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

        private Color GetPlayerColor(int player)
        {
            int index = Mathf.Clamp(player - 1, 0, playerColors.Length - 1);
            return playerColors[index];
        }

        private void ClearMarkers()
        {
            if (markerRoot == null)
            {
                return;
            }

            for (int i = markerRoot.childCount - 1; i >= 0; i--)
            {
                Destroy(markerRoot.GetChild(i).gameObject);
            }
        }
    }
}

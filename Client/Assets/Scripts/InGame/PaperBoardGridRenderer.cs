using UnityEngine;

namespace NHN.InGame
{
    public sealed class PaperBoardGridRenderer : MonoBehaviour
    {
        private const string GridRootName = "Generated Grid Lines";

        public PaperBoardTarget boardTarget;
        public bool syncFromTarget = true;
        [Min(2)] public int boardSize = 15;
        [Min(0.1f)] public Vector2 boardWorldSize = new Vector2(3.6f, 3.6f);
        [Range(0f, 0.35f)] public float gridInsetNormalized = 0.1f;
        public Rect gridNormalizedRect = new Rect(0.1f, 0.1f, 0.8f, 0.8f);
        [Min(0.001f)] public float lineWidth = 0.018f;
        public int sortingOrder = 16;
        public Color lineColor = new Color32(87, 52, 24, 210);

        private static Sprite solidSprite;
        private Transform gridRoot;
        private int renderedBoardSize;
        private Vector2 renderedBoardWorldSize;
        private Rect renderedGridRect;

        private void Awake()
        {
            Rebuild();
        }

        public void RefreshFromTarget()
        {
            if (syncFromTarget && boardTarget != null)
            {
                boardSize = boardTarget.boardSize;
                boardWorldSize = boardTarget.boardWorldSize;
                gridNormalizedRect = boardTarget.GridNormalizedRect;
            }

            if (renderedBoardSize == boardSize &&
                renderedBoardWorldSize == boardWorldSize &&
                Mathf.Approximately(renderedGridRect.x, gridNormalizedRect.x) &&
                Mathf.Approximately(renderedGridRect.y, gridNormalizedRect.y) &&
                Mathf.Approximately(renderedGridRect.width, gridNormalizedRect.width) &&
                Mathf.Approximately(renderedGridRect.height, gridNormalizedRect.height))
            {
                return;
            }

            Rebuild();
        }

        public void Rebuild()
        {
            if (syncFromTarget && boardTarget != null)
            {
                boardSize = boardTarget.boardSize;
                boardWorldSize = boardTarget.boardWorldSize;
                gridNormalizedRect = boardTarget.GridNormalizedRect;
            }

            EnsureGridRoot();
            ClearLines();

            if (boardSize < 2)
            {
                return;
            }

            Rect gridRect = GetGridRect();
            for (int i = 0; i < boardSize; i++)
            {
                float t = i / (float)(boardSize - 1);
                float x = Mathf.Lerp(gridRect.xMin, gridRect.xMax, t);
                float y = Mathf.Lerp(gridRect.yMin, gridRect.yMax, t);
                CreateLine("Grid Vertical " + i, new Vector3(x, gridRect.center.y, -0.035f), new Vector3(lineWidth, gridRect.height, 1f));
                CreateLine("Grid Horizontal " + i, new Vector3(gridRect.center.x, y, -0.035f), new Vector3(gridRect.width, lineWidth, 1f));
            }

            renderedBoardSize = boardSize;
            renderedBoardWorldSize = boardWorldSize;
            renderedGridRect = gridNormalizedRect;
        }

        public void SetVisible(bool visible)
        {
            EnsureGridRoot();
            gridRoot.gameObject.SetActive(visible);
        }

        private void EnsureGridRoot()
        {
            if (gridRoot != null)
            {
                return;
            }

            Transform existing = transform.Find(GridRootName);
            if (existing != null)
            {
                gridRoot = existing;
                return;
            }

            gridRoot = new GameObject(GridRootName).transform;
            gridRoot.SetParent(transform, false);
        }

        private void ClearLines()
        {
            if (gridRoot == null)
            {
                return;
            }

            for (int i = gridRoot.childCount - 1; i >= 0; i--)
            {
                GameObject child = gridRoot.GetChild(i).gameObject;
                if (Application.isPlaying)
                {
                    Destroy(child);
                }
                else
                {
                    DestroyImmediate(child);
                }
            }
        }

        private void CreateLine(string lineName, Vector3 localPosition, Vector3 localScale)
        {
            GameObject lineObject = new GameObject(lineName);
            Transform lineTransform = lineObject.transform;
            lineTransform.SetParent(gridRoot, false);
            lineTransform.localPosition = localPosition;
            lineTransform.localScale = localScale;

            SpriteRenderer renderer = lineObject.AddComponent<SpriteRenderer>();
            renderer.sprite = GetSolidSprite();
            renderer.color = lineColor;
            renderer.sortingOrder = sortingOrder;
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

        private static Sprite GetSolidSprite()
        {
            if (solidSprite != null)
            {
                return solidSprite;
            }

            Texture2D texture = new Texture2D(1, 1, TextureFormat.RGBA32, false);
            texture.SetPixel(0, 0, Color.white);
            texture.Apply();
            solidSprite = Sprite.Create(texture, new Rect(0f, 0f, 1f, 1f), new Vector2(0.5f, 0.5f), 1f);
            return solidSprite;
        }
    }
}

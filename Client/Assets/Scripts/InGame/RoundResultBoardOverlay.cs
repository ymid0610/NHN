using System.Collections.Generic;
using NHN.Network;
using UnityEngine;

namespace NHN.InGame
{
    public sealed class RoundResultBoardOverlay : MonoBehaviour
    {
        public SpriteRenderer boardRenderer;
        public PaperBoardGridRenderer gridRenderer;
        public Transform markerRoot;
        public Sprite bulletHoleSprite;
        public Sprite[] bulletImpactFrames;
        public GameObject bulletImpactMarkerTemplate;
        public Vector2 boardWorldSize = new Vector2(6.6f, 6.6f);
        public float boardImageScaleMultiplier = 1.18f;
        [Range(0f, 0.35f)] public float gridInsetNormalized = 0.1f;
        public Rect gridNormalizedRect = new Rect(0.1f, 0.1f, 0.8f, 0.8f);
        public float gomokuMarkerWorldSize = 0.34f;
        public float ticTacToeMarkerWorldSize = 1.25f;
        public int markerSortingBase = 230;
        public bool useBulletImpactSpritePivot = true;
        [Min(0.02f)] public float shotRevealInterval = 0.16f;
        [Min(1f)] public float impactFrameRate = 18f;
        [Min(0.1f)] public float impactAnimationScaleMultiplier = 0.45f;
        [Min(0.01f)] public float finalBulletHoleScaleMultiplier = 1f;
        public float victoryLineWidth = 0.08f;
        public Color[] playerColors =
        {
            new Color(0.3f, 1f, 0.35f, 1f),
            new Color(1f, 0.25f, 0.2f, 1f),
            new Color(0.25f, 0.55f, 1f, 1f),
            new Color(1f, 0.9f, 0.25f, 1f)
        };

        private readonly List<ShotRecord> replayShots = new List<ShotRecord>();
        private readonly List<Vector2Int> winningCells = new List<Vector2Int>();
        private int replayBoardSize;
        private ServerGameMode replayMode;
        private int nextRevealIndex;
        private int winnerPlayer;
        private string winnerName = string.Empty;
        private float revealTimer;
        private bool playbackComplete;
        private LineRenderer victoryLine;
        private GUIStyle victoryTextStyle;
        private GUIStyle victorySubTextStyle;
        private GUIStyle victoryBoxStyle;

        public bool IsVisible => gameObject.activeSelf;
        public bool IsPlaybackComplete => playbackComplete;

        public void Show(Sprite boardSprite, IReadOnlyList<ShotRecord> shots, int boardSize, ServerGameMode mode)
        {
            Show(boardSprite, shots, boardSize, mode, null, 0, string.Empty, null);
        }

        public void Show(
            Sprite boardSprite,
            IReadOnlyList<ShotRecord> shots,
            int boardSize,
            ServerGameMode mode,
            IReadOnlyList<Vector2Int> winLineCells,
            int winner,
            string winningPlayerName,
            Sprite[] impactFrames)
        {
            gameObject.SetActive(true);
            ClearMarkers();
            HideVictoryLine();

            replayShots.Clear();
            if (shots != null)
            {
                for (int i = 0; i < shots.Count; i++)
                {
                    replayShots.Add(shots[i]);
                }
            }

            winningCells.Clear();
            if (winLineCells != null)
            {
                for (int i = 0; i < winLineCells.Count; i++)
                {
                    winningCells.Add(winLineCells[i]);
                }
            }

            bulletImpactFrames = impactFrames != null && impactFrames.Length > 0 ? impactFrames : bulletImpactFrames;
            replayBoardSize = boardSize;
            replayMode = mode;
            winnerPlayer = winner;
            winnerName = string.IsNullOrWhiteSpace(winningPlayerName) ? $"P{winner}" : winningPlayerName;
            nextRevealIndex = 0;
            revealTimer = 0f;
            playbackComplete = false;

            if (boardRenderer != null)
            {
                boardRenderer.sprite = boardSprite;
                boardRenderer.sortingOrder = markerSortingBase - 1;
                ScaleBoardVisual();
            }

            RefreshGrid(boardSize);

            if (replayShots.Count == 0)
            {
                CompletePlayback();
            }
        }

        public void Hide()
        {
            ClearMarkers();
            HideVictoryLine();
            gameObject.SetActive(false);
        }

        private void Update()
        {
            if (playbackComplete || !gameObject.activeSelf)
            {
                return;
            }

            revealTimer -= Time.deltaTime;
            if (revealTimer <= 0f && nextRevealIndex < replayShots.Count)
            {
                SpawnMarker(replayShots[nextRevealIndex], replayBoardSize, replayMode, true);
                nextRevealIndex++;
                revealTimer += shotRevealInterval;
            }

            if (nextRevealIndex >= replayShots.Count)
            {
                CompletePlayback();
            }
        }

        private void CompletePlayback()
        {
            playbackComplete = true;
            if (winnerPlayer > 0 && winningCells.Count >= 2)
            {
                ShowVictoryLine();
            }
        }

        private void RefreshGrid(int boardSize)
        {
            if (gridRenderer == null)
            {
                gridRenderer = GetComponent<PaperBoardGridRenderer>();
            }

            if (gridRenderer == null)
            {
                return;
            }

            gridRenderer.syncFromTarget = false;
            gridRenderer.boardSize = boardSize;
            gridRenderer.boardWorldSize = boardWorldSize;
            gridRenderer.gridInsetNormalized = gridInsetNormalized;
            gridRenderer.gridNormalizedRect = GetSanitizedGridNormalizedRect();
            gridRenderer.sortingOrder = markerSortingBase;
            gridRenderer.lineWidth = 0.026f;
            gridRenderer.Rebuild();
            gridRenderer.SetVisible(true);
        }

        private void ScaleBoardVisual()
        {
            if (boardRenderer.sprite == null)
            {
                return;
            }

            float spriteSize = Mathf.Max(boardRenderer.sprite.bounds.size.x, boardRenderer.sprite.bounds.size.y, 0.01f);
            boardRenderer.transform.localScale = Vector3.one * (Mathf.Max(boardWorldSize.x, boardWorldSize.y) / spriteSize * Mathf.Max(0.1f, boardImageScaleMultiplier));
        }

        private void SpawnMarker(ShotRecord shot, int boardSize, ServerGameMode mode, bool playImpact)
        {
            Sprite markerSprite = GetFinalMarkerSprite();
            Sprite firstSprite = GetFirstImpactSprite();
            if (markerSprite == null && firstSprite == null)
            {
                return;
            }

            GameObject marker = new GameObject($"Result_P{shot.player}_{shot.cell.x}_{shot.cell.y}");
            Transform markerTransform = marker.transform;
            markerTransform.SetParent(markerRoot != null ? markerRoot : transform, false);
            markerTransform.localPosition = CellToLocal(shot.cell, boardSize);
            markerTransform.localRotation = Quaternion.identity;

            float targetSize = ModeRules.For(mode).UsesSmallMarker
                ? gomokuMarkerWorldSize
                : ticTacToeMarkerWorldSize;
            Vector3 finalScale = Vector3.one * (targetSize * finalBulletHoleScaleMultiplier / GetSpriteSourceSize(markerSprite != null ? markerSprite : firstSprite));
            Vector3 impactScale = Vector3.one * (targetSize * impactAnimationScaleMultiplier / GetSpriteSourceSize(GetLastImpactSprite() != null ? GetLastImpactSprite() : firstSprite));
            CreateBulletImpactMarkerVisual(
                markerTransform,
                firstSprite,
                markerSprite,
                playImpact && bulletImpactFrames != null && bulletImpactFrames.Length > 0,
                markerSortingBase + 2 + shot.player,
                GetPlayerColor(shot.player),
                impactScale,
                finalScale);
        }

        private void CreateBulletImpactMarkerVisual(
            Transform parent,
            Sprite firstImpactSprite,
            Sprite finalMarkerSprite,
            bool playImpact,
            int sortingOrder,
            Color color,
            Vector3 impactScale,
            Vector3 finalScale)
        {
            GameObject visual = bulletImpactMarkerTemplate != null ? Instantiate(bulletImpactMarkerTemplate, parent, false) : new GameObject("Bullet Impact Marker");
            visual.name = "Bullet Impact Marker";
            visual.transform.SetParent(parent, false);
            visual.transform.localPosition = Vector3.zero;
            visual.transform.localRotation = Quaternion.identity;
            visual.SetActive(true);

            SpriteRenderer renderer = visual.GetComponentInChildren<SpriteRenderer>(true);
            if (renderer == null)
            {
                renderer = visual.AddComponent<SpriteRenderer>();
            }

            renderer.gameObject.SetActive(true);
            renderer.sprite = playImpact && firstImpactSprite != null ? firstImpactSprite : finalMarkerSprite;
            renderer.color = color;
            renderer.sortingOrder = sortingOrder;

            Transform scaleTarget = renderer.transform;
            Vector3 templateScale = scaleTarget.localScale;
            scaleTarget.localScale = Vector3.Scale(templateScale, playImpact ? impactScale : finalScale);

            SpriteBoundsCenterer centerer = ConfigureBulletImpactCenterer(renderer);

            if (playImpact && bulletImpactFrames != null && bulletImpactFrames.Length > 0)
            {
                BulletImpactAnimator animator = renderer.GetComponent<BulletImpactAnimator>();
                if (animator == null)
                {
                    animator = renderer.gameObject.AddComponent<BulletImpactAnimator>();
                }

                animator.spriteRenderer = renderer;
                animator.frames = bulletImpactFrames;
                animator.finalSprite = finalMarkerSprite;
                animator.targetTransform = scaleTarget;
                animator.centerer = centerer != null && centerer.enabled ? centerer : null;
                animator.finalLocalScale = Vector3.Scale(templateScale, finalScale);
                animator.applyFinalLocalScale = finalMarkerSprite != null;
                animator.framesPerSecond = impactFrameRate;
                animator.Play();
            }
        }

        private SpriteBoundsCenterer ConfigureBulletImpactCenterer(SpriteRenderer renderer)
        {
            SpriteBoundsCenterer centerer = renderer.GetComponent<SpriteBoundsCenterer>();
            if (useBulletImpactSpritePivot)
            {
                if (centerer != null)
                {
                    centerer.enabled = false;
                }

                return null;
            }

            if (centerer == null)
            {
                centerer = renderer.gameObject.AddComponent<SpriteBoundsCenterer>();
            }

            centerer.enabled = true;
            centerer.spriteRenderer = renderer;
            centerer.CenterNow();
            return centerer;
        }

        private void ShowVictoryLine()
        {
            EnsureVictoryLine();
            victoryLine.gameObject.SetActive(true);
            victoryLine.positionCount = winningCells.Count;
            victoryLine.startColor = GetPlayerColor(winnerPlayer);
            victoryLine.endColor = GetPlayerColor(winnerPlayer);
            victoryLine.widthMultiplier = victoryLineWidth;

            for (int i = 0; i < winningCells.Count; i++)
            {
                Vector3 local = CellToLocal(winningCells[i], replayBoardSize);
                local.z = -0.12f;
                victoryLine.SetPosition(i, local);
            }
        }

        private void EnsureVictoryLine()
        {
            if (victoryLine != null)
            {
                return;
            }

            GameObject lineObject = new GameObject("Winning Five Line");
            lineObject.transform.SetParent(transform, false);
            lineObject.transform.localPosition = Vector3.zero;
            victoryLine = lineObject.AddComponent<LineRenderer>();
            victoryLine.useWorldSpace = false;
            victoryLine.numCapVertices = 8;
            victoryLine.numCornerVertices = 8;
            victoryLine.sortingOrder = markerSortingBase + 12;
            Shader shader = Shader.Find("Sprites/Default");
            if (shader != null)
            {
                victoryLine.material = new Material(shader);
            }
        }

        private void HideVictoryLine()
        {
            if (victoryLine != null)
            {
                victoryLine.positionCount = 0;
                victoryLine.gameObject.SetActive(false);
            }
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

        private Sprite GetFirstImpactSprite()
        {
            return bulletImpactFrames != null && bulletImpactFrames.Length > 0 ? bulletImpactFrames[0] : null;
        }

        private Sprite GetLastImpactSprite()
        {
            return bulletImpactFrames != null && bulletImpactFrames.Length > 0 ? bulletImpactFrames[bulletImpactFrames.Length - 1] : null;
        }

        private Sprite GetFinalMarkerSprite()
        {
            return bulletHoleSprite != null ? bulletHoleSprite : bulletImpactFrames != null && bulletImpactFrames.Length > 0 ? bulletImpactFrames[bulletImpactFrames.Length - 1] : null;
        }

        private static float GetSpriteSourceSize(Sprite sprite)
        {
            if (sprite == null)
            {
                return 1f;
            }

            return Mathf.Max(sprite.bounds.size.x, sprite.bounds.size.y, 0.01f);
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

        private void OnGUI()
        {
            if (!gameObject.activeSelf || !playbackComplete || winnerPlayer <= 0)
            {
                return;
            }

            EnsureGuiStyles();
            string displayName = string.IsNullOrWhiteSpace(winnerName) ? $"P{winnerPlayer}" : winnerName;
            Rect boxRect = new Rect((Screen.width - 720f) * 0.5f, Screen.height * 0.32f, 720f, 148f);
            Color previousColor = GUI.color;
            GUI.color = new Color(0.35f, 0.18f, 0.07f, 0.92f);
            GUI.Box(boxRect, GUIContent.none, victoryBoxStyle);
            GUI.color = previousColor;
            GUI.Label(new Rect(boxRect.x + 12f, boxRect.y + 20f, boxRect.width - 24f, 58f), "WANTED WINNER", victorySubTextStyle);
            GUI.Label(new Rect(boxRect.x + 12f, boxRect.y + 62f, boxRect.width - 24f, 72f), $"{displayName} 승리!", victoryTextStyle);
        }

        private void EnsureGuiStyles()
        {
            if (victoryTextStyle != null)
            {
                return;
            }

            victoryBoxStyle = new GUIStyle(GUI.skin.box)
            {
                normal =
                {
                    background = Texture2D.whiteTexture,
                    textColor = Color.white
                }
            };

            victorySubTextStyle = new GUIStyle(GUI.skin.label)
            {
                alignment = TextAnchor.MiddleCenter,
                fontStyle = FontStyle.Bold,
                fontSize = 30,
                normal = { textColor = new Color(0.28f, 0.15f, 0.06f, 1f) }
            };

            victoryTextStyle = new GUIStyle(victorySubTextStyle)
            {
                fontSize = 46,
                normal = { textColor = new Color(0.95f, 0.66f, 0.23f, 1f) }
            };
        }
    }
}

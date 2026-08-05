using System.Collections.Generic;
using UnityEngine;

#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

namespace NHN.InGame
{
    public sealed class LocalInGameController : MonoBehaviour
    {
        private const string PrefGameMode = "NHN.GameMode";
        private const string PrefMaxPlayers = "NHN.MaxPlayers";
        private const string PrefBlockOccupied = "NHN.BlockOccupiedCells";
        private const string PrefAllowOverline = "NHN.AllowOverline";
        private const string PrefItemsEnabled = "NHN.ItemsEnabled";

        [Header("Scene References")]
        public Camera targetCamera;
        public PaperBoardTarget boardTarget;
        public PaperWindMotion windMotion;
        public PaperBoardWarp boardWarp;
        public ScarecrowPaperCarrier scarecrowCarrier;
        public Transform markRoot;
        public Sprite bulletHoleSprite;
        public PrototypeSpriteAnimator cowboyAnimator;
        public RoundResultBoardOverlay resultOverlay;
        public Sprite resultBoardSprite;

        [Header("Generated Item Sprites")]
        public Sprite fryingPanItemSprite;
        public Sprite outlawBanditSprite;
        public Sprite tripleShotPowerupSprite;
        public Sprite camelCarrierSprite;

        [Header("Game Rules")]
        public GameMode gameMode = GameMode.Gomoku;
        [Range(1, 4)] public int maxPlayers = 4;
        public bool blockOccupiedCells = true;
        public bool allowOverline = true;
        public bool itemEnabled;
        public bool requireScarecrowAttached = true;

        [Header("Board Size")]
        public Vector2 inGameBoardWorldSize = new Vector2(3.6f, 3.6f);

        [Header("Marker")]
        public float gomokuMarkerWorldSize = 0.17f;
        public float ticTacToeMarkerWorldSize = 0.72f;
        public Color[] playerColors =
        {
            new Color(0.3f, 1f, 0.35f, 1f),
            new Color(1f, 0.25f, 0.2f, 1f),
            new Color(0.25f, 0.55f, 1f, 1f),
            new Color(1f, 0.9f, 0.25f, 1f)
        };

        private GomokuBoardState boardState;
        private readonly List<ShotRecord> shotRecords = new List<ShotRecord>();
        private int currentPlayer = 1;
        private int remainingShots;
        private int roundIndex = 1;
        private int winner;
        private string statusMessage = "Ready";
        private bool showingResult;

        public int CurrentPlayer => currentPlayer;
        public int RemainingShots => remainingShots;
        public int Winner => winner;
        public bool ShowingResult => showingResult;
        public string StatusMessage => statusMessage;
        public int RoundIndex => roundIndex;
        public int TargetHealth => scarecrowCarrier != null ? scarecrowCarrier.CurrentHealth : 0;
        public int TargetMaxHealth => scarecrowCarrier != null ? scarecrowCarrier.MaxHealth : 0;
        public bool TargetCanShoot => scarecrowCarrier == null || scarecrowCarrier.CanShoot;
        public int BoardSize => GetBoardSize();
        public int ShotsPerTurn => GetShotsPerTurn();

        private void Start()
        {
            ApplyMenuLaunchOptions();
            EnsureBoardWarp();
            EnsureScarecrowCarrier();
            EnsureResultOverlay();
            ResetMatch();
        }

        private void ApplyMenuLaunchOptions()
        {
            if (PlayerPrefs.HasKey(PrefGameMode))
            {
                gameMode = (GameMode)Mathf.Clamp(PlayerPrefs.GetInt(PrefGameMode, (int)gameMode), 0, 1);
            }

            maxPlayers = PlayerPrefs.GetInt(PrefMaxPlayers, maxPlayers);
            blockOccupiedCells = PlayerPrefs.GetInt(PrefBlockOccupied, blockOccupiedCells ? 1 : 0) == 1;
            allowOverline = PlayerPrefs.GetInt(PrefAllowOverline, allowOverline ? 1 : 0) == 1;
            itemEnabled = PlayerPrefs.GetInt(PrefItemsEnabled, itemEnabled ? 1 : 0) == 1;
        }

        private void Update()
        {
            if (WasResetPressed())
            {
                ResetMatch();
                return;
            }

            if (showingResult)
            {
                if (winner == 0 && WasAdvancePressed())
                {
                    StartNextRound();
                }

                return;
            }

            if (winner != 0)
            {
                return;
            }

            if (WasShootPressed())
            {
                TryShootAtPointer();
            }
        }

        public void ResetMatch()
        {
            int boardSize = GetBoardSize();
            boardState = new GomokuBoardState(boardSize);
            currentPlayer = 1;
            remainingShots = GetShotsPerTurn();
            roundIndex = 1;
            winner = 0;
            showingResult = false;
            statusMessage = "Ready";
            shotRecords.Clear();

            if (boardTarget != null)
            {
                boardTarget.boardSize = boardSize;
                ApplyBoardSizing();
            }

            if (windMotion != null)
            {
                windMotion.SetResultView(false);
            }

            if (scarecrowCarrier != null)
            {
                scarecrowCarrier.ResetCarrier();
            }

            if (resultOverlay != null)
            {
                resultOverlay.Hide();
            }

            ClearMarks();
        }

        public void AdvanceTurn()
        {
            if (winner != 0)
            {
                return;
            }

            if (showingResult)
            {
                StartNextRound();
                return;
            }

            MoveToNextPlayer();
        }

        public void StartNextRound()
        {
            if (winner != 0)
            {
                return;
            }

            showingResult = false;
            currentPlayer = 1;
            remainingShots = GetShotsPerTurn();
            roundIndex++;
            statusMessage = $"Round {roundIndex}";

            if (windMotion != null)
            {
                windMotion.SetResultView(false);
            }

            if (scarecrowCarrier != null)
            {
                scarecrowCarrier.ResetCarrier();
            }

            if (resultOverlay != null)
            {
                resultOverlay.Hide();
            }
        }

        public void SetMode(GameMode mode)
        {
            if (gameMode == mode)
            {
                return;
            }

            gameMode = mode;
            maxPlayers = mode == GameMode.Gomoku ? 4 : 2;
            ResetMatch();
        }

        private void TryShootAtPointer()
        {
            if (targetCamera == null)
            {
                targetCamera = Camera.main;
            }

            if (targetCamera == null || boardTarget == null)
            {
                statusMessage = "Missing camera or board";
                return;
            }

            if (requireScarecrowAttached && scarecrowCarrier != null && !scarecrowCarrier.CanShoot)
            {
                statusMessage = "Wait for paper";
                return;
            }

            Vector2 screenPosition = GetPointerScreenPosition();
            Vector3 worldPosition = targetCamera.ScreenToWorldPoint(new Vector3(screenPosition.x, screenPosition.y, -targetCamera.transform.position.z));

            if (!boardTarget.TryWorldToCell(worldPosition, out Vector2Int cell))
            {
                statusMessage = "Miss";
                return;
            }

            bool occupied = blockOccupiedCells && boardState.GetOwner(cell) != 0;
            bool placed = false;

            if (!occupied)
            {
                placed = boardState.TryPlace(cell, currentPlayer, blockOccupiedCells, out string reason);
                if (!placed)
                {
                    statusMessage = reason;
                    return;
                }
            }

            remainingShots = Mathf.Max(remainingShots - 1, 0);
            cowboyAnimator?.PlayOnce();
            bool targetDown = false;
            if (scarecrowCarrier != null && scarecrowCarrier.RegisterHit())
            {
                targetDown = !scarecrowCarrier.CanShoot;
            }

            if (!placed)
            {
                statusMessage = targetDown ? "Scarecrow down" : $"Already shot {cell.x + 1}, {cell.y + 1}";
                ResolveAmmoAfterShot();
                return;
            }

            shotRecords.Add(new ShotRecord(cell, currentPlayer));
            SpawnMarker(cell, currentPlayer);

            int winLength = gameMode == GameMode.Gomoku ? 5 : 3;
            bool overlineAllowed = gameMode != GameMode.Gomoku || allowOverline;
            if (boardState.HasWinnerFrom(cell, currentPlayer, winLength, overlineAllowed))
            {
                winner = currentPlayer;
                ShowRoundResult($"P{currentPlayer} Win");
                return;
            }

            statusMessage = targetDown ? "Scarecrow down" : $"Hit {cell.x + 1}, {cell.y + 1}";

            ResolveAmmoAfterShot();
        }

        private void MoveToNextPlayer()
        {
            currentPlayer++;
            if (currentPlayer > GetPlayerCount())
            {
                currentPlayer = 1;
            }

            remainingShots = GetShotsPerTurn();
            statusMessage = $"P{currentPlayer} turn";
        }

        private void ShowRoundResult(string message)
        {
            showingResult = true;
            statusMessage = message;
            windMotion?.SetResultView(true);
            EnsureResultOverlay();

            if (resultOverlay != null)
            {
                resultOverlay.bulletHoleSprite = bulletHoleSprite;
                if (boardTarget != null)
                {
                    resultOverlay.gridNormalizedRect = boardTarget.GridNormalizedRect;
                }

                resultOverlay.playerColors = playerColors;
                resultOverlay.gomokuMarkerWorldSize = gomokuMarkerWorldSize * 1.2f;
                resultOverlay.ticTacToeMarkerWorldSize = ticTacToeMarkerWorldSize * 1.3f;
                resultOverlay.Show(resultBoardSprite, shotRecords, GetBoardSize(), gameMode);
            }
        }

        private void ResolveAmmoAfterShot()
        {
            if (remainingShots > 0)
            {
                return;
            }

            if (currentPlayer >= GetPlayerCount())
            {
                ShowRoundResult("Round result");
            }
            else
            {
                MoveToNextPlayer();
            }
        }

        private void EnsureResultOverlay()
        {
            if (resultBoardSprite == null && windMotion != null && windMotion.frames != null && windMotion.frames.Length > 0)
            {
                resultBoardSprite = windMotion.frames[0];
            }

            if (resultOverlay != null)
            {
                return;
            }

            GameObject overlayObject = new GameObject("Round Result Board UI");
            overlayObject.transform.position = new Vector3(0f, 0.15f, -0.2f);

            GameObject visualObject = new GameObject("Result Paper Visual");
            visualObject.transform.SetParent(overlayObject.transform, false);

            SpriteRenderer boardRenderer = visualObject.AddComponent<SpriteRenderer>();
            boardRenderer.sprite = resultBoardSprite;
            boardRenderer.sortingOrder = 220;

            SpriteBoundsCenterer centerer = visualObject.AddComponent<SpriteBoundsCenterer>();
            centerer.spriteRenderer = boardRenderer;

            Transform resultMarkerRoot = new GameObject("Result Shot Marks").transform;
            resultMarkerRoot.SetParent(overlayObject.transform, false);

            resultOverlay = overlayObject.AddComponent<RoundResultBoardOverlay>();
            resultOverlay.boardRenderer = boardRenderer;
            resultOverlay.markerRoot = resultMarkerRoot;
            resultOverlay.bulletHoleSprite = bulletHoleSprite;
            resultOverlay.Hide();
        }

        private void EnsureBoardWarp()
        {
            if (boardTarget == null)
            {
                return;
            }

            if (boardWarp == null)
            {
                boardWarp = boardTarget.GetComponent<PaperBoardWarp>();
            }

            if (boardWarp == null)
            {
                boardWarp = boardTarget.gameObject.AddComponent<PaperBoardWarp>();
            }

            boardWarp.boardTarget = boardTarget;
            boardWarp.windMotion = windMotion;
        }

        private void EnsureScarecrowCarrier()
        {
            if (boardTarget == null)
            {
                return;
            }

            if (scarecrowCarrier == null)
            {
                scarecrowCarrier = FindFirstObjectByType<ScarecrowPaperCarrier>();
            }

            if (scarecrowCarrier == null)
            {
                GameObject carrierObject = new GameObject("Scarecrow Paper Carrier");
                scarecrowCarrier = carrierObject.AddComponent<ScarecrowPaperCarrier>();
            }

            scarecrowCarrier.boardTransform = boardTarget.transform;
            scarecrowCarrier.boardWindMotion = windMotion;
            scarecrowCarrier.boardWarp = boardWarp;
            scarecrowCarrier.paperAttachedScale = boardTarget.transform.localScale;
        }

        private void ApplyBoardSizing()
        {
            if (boardTarget == null)
            {
                return;
            }

            boardTarget.boardWorldSize = inGameBoardWorldSize;
            if (gomokuMarkerWorldSize > 0.22f)
            {
                gomokuMarkerWorldSize = 0.17f;
            }

            if (ticTacToeMarkerWorldSize > 0.8f)
            {
                ticTacToeMarkerWorldSize = 0.72f;
            }

            if (windMotion == null || windMotion.spriteRenderer == null || windMotion.spriteRenderer.sprite == null)
            {
                return;
            }

            float targetSize = Mathf.Max(inGameBoardWorldSize.x, inGameBoardWorldSize.y);
            float spriteSize = Mathf.Max(
                windMotion.spriteRenderer.sprite.bounds.size.x,
                windMotion.spriteRenderer.sprite.bounds.size.y,
                0.01f);

            Transform visualTransform = windMotion.spriteRenderer.transform;
            visualTransform.localScale = Vector3.one * (targetSize / spriteSize);

            SpriteBoundsCenterer centerer = visualTransform.GetComponent<SpriteBoundsCenterer>();
            if (centerer != null)
            {
                centerer.CenterNow();
            }
        }

        private void SpawnMarker(Vector2Int cell, int player)
        {
            if (bulletHoleSprite == null || boardTarget == null)
            {
                return;
            }

            GameObject marker = new GameObject($"P{player}_Shot_{cell.x}_{cell.y}");
            Transform markerTransform = marker.transform;
            markerTransform.SetParent(markRoot != null ? markRoot : boardTarget.transform, false);
            markerTransform.localPosition = boardTarget.CellToLocal(cell);
            markerTransform.localRotation = Quaternion.identity;

            GameObject visual = new GameObject("Bullet Hole Visual");
            Transform visualTransform = visual.transform;
            visualTransform.SetParent(markerTransform, false);

            SpriteRenderer renderer = visual.AddComponent<SpriteRenderer>();
            renderer.sprite = bulletHoleSprite;
            renderer.sortingOrder = 20 + player;
            renderer.color = GetPlayerColor(player);

            float targetWorldSize = gameMode == GameMode.Gomoku ? gomokuMarkerWorldSize : ticTacToeMarkerWorldSize;
            Vector2 spriteSize = bulletHoleSprite.bounds.size;
            float sourceSize = Mathf.Max(spriteSize.x, spriteSize.y, 0.01f);
            visualTransform.localScale = Vector3.one * (targetWorldSize / sourceSize);

            SpriteBoundsCenterer centerer = visual.AddComponent<SpriteBoundsCenterer>();
            centerer.spriteRenderer = renderer;
            centerer.CenterNow();

            WarpedBoardMarker warpedMarker = marker.AddComponent<WarpedBoardMarker>();
            warpedMarker.Configure(boardTarget, boardWarp, cell, visualTransform);
        }

        private Color GetPlayerColor(int player)
        {
            int index = Mathf.Clamp(player - 1, 0, playerColors.Length - 1);
            return playerColors[index];
        }

        private void ClearMarks()
        {
            if (markRoot == null)
            {
                return;
            }

            for (int i = markRoot.childCount - 1; i >= 0; i--)
            {
                Destroy(markRoot.GetChild(i).gameObject);
            }
        }

        private int GetBoardSize()
        {
            return gameMode == GameMode.Gomoku ? 15 : 3;
        }

        private int GetShotsPerTurn()
        {
            return gameMode == GameMode.Gomoku ? 6 : 1;
        }

        private int GetPlayerCount()
        {
            return gameMode == GameMode.Gomoku ? Mathf.Clamp(maxPlayers, 1, 4) : Mathf.Clamp(maxPlayers, 1, 2);
        }

        private bool WasShootPressed()
        {
#if ENABLE_INPUT_SYSTEM
            if (Mouse.current != null && Mouse.current.leftButton.wasPressedThisFrame)
            {
                return true;
            }
#endif

#if ENABLE_LEGACY_INPUT_MANAGER
            return Input.GetMouseButtonDown(0);
#else
            return false;
#endif
        }

        private bool WasAdvancePressed()
        {
#if ENABLE_INPUT_SYSTEM
            if (Keyboard.current != null &&
                (Keyboard.current.enterKey.wasPressedThisFrame || Keyboard.current.spaceKey.wasPressedThisFrame))
            {
                return true;
            }
#endif

#if ENABLE_LEGACY_INPUT_MANAGER
            return Input.GetKeyDown(KeyCode.Return) || Input.GetKeyDown(KeyCode.Space);
#else
            return false;
#endif
        }

        private bool WasResetPressed()
        {
#if ENABLE_INPUT_SYSTEM
            if (Keyboard.current != null && Keyboard.current.rKey.wasPressedThisFrame)
            {
                return true;
            }
#endif

#if ENABLE_LEGACY_INPUT_MANAGER
            return Input.GetKeyDown(KeyCode.R);
#else
            return false;
#endif
        }

        private Vector2 GetPointerScreenPosition()
        {
#if ENABLE_INPUT_SYSTEM
            if (Mouse.current != null)
            {
                return Mouse.current.position.ReadValue();
            }
#endif

#if ENABLE_LEGACY_INPUT_MANAGER
            return Input.mousePosition;
#else
            return Vector2.zero;
#endif
        }
    }
}

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
        private const string PaperIdleResourcePath = "Sprite/Generated/PaperBoardBlankIdleGenerated";
        private const string PaperFlyingResourcePath = "Sprite/Generated/PaperBoardBlankFlyingGenerated";
        private const string PaperAttachedResourcePath = "Sprite/Generated/PaperBoardBlankAttachedGenerated";
        private const string PaperFlightFrameResourcePrefix = "Sprite/Generated/PaperFlightFrames/PaperBoardFlyingFrame";
        private const string PaperPromptIdleFrameResourcePrefix = "Sprite/Generated/PaperPromptAnimations/Idle/PaperIdleFrame";
        private const string PaperPromptFlyingFrameResourcePrefix = "Sprite/Generated/PaperPromptAnimations/Flying/PaperFlyingFrame";
        private const string PaperPromptUnfoldFrameResourcePrefix = "Sprite/Generated/PaperPromptAnimations/Unfold/PaperUnfoldFrame";
        private const string ScarecrowIdleFrameResourcePrefix = "Sprite/Generated/ScarecrowPromptAnimations/Idle/ScarecrowIdleFrame";
        private const string ScarecrowAttackedFrameResourcePrefix = "Sprite/Generated/ScarecrowPromptAnimations/Attacked/ScarecrowAttackedFrame";
        private const string ScarecrowDeathFrameResourcePrefix = "Sprite/Generated/ScarecrowPromptAnimations/Death/ScarecrowDeathFrame";
        private const string BulletImpactFrameResourcePrefix = "Sprite/Generated/BulletImpactAnimation/Frames/BulletImpactFrame";

        [Header("Scene References")]
        public Camera targetCamera;
        public PaperBoardTarget boardTarget;
        public PaperWindMotion windMotion;
        public PaperBoardWarp boardWarp;
        public PaperBoardGridRenderer boardGridRenderer;
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
        public bool preferAssignedScarecrowSprites = true;

        [Header("Game Rules")]
        public GameMode gameMode = GameMode.Gomoku;
        [Range(1, 4)] public int maxPlayers = 4;
        public bool blockOccupiedCells = true;
        public bool allowOverline = true;
        public bool itemEnabled;
        public bool requireScarecrowAttached = true;

        [Header("Board / Grid Tuning")]
        public bool applyInGameBoardWorldSize = true;
        public bool syncResultGridFromBoardTarget = true;
        public bool useResultGridOverride;
        public Vector2 inGameBoardWorldSize = new Vector2(3.6f, 3.6f);
        public Rect resultGridNormalizedRectOverride = new Rect(0.1f, 0.1f, 0.8f, 0.8f);
        public float resultBoardImageScaleMultiplier = 1.18f;
        [Min(0.1f)] public float resultGomokuMarkerWorldSizeMultiplier = 1.2f;
        [Min(0.1f)] public float resultTicTacToeMarkerWorldSizeMultiplier = 1.3f;
        [Min(0.1f)] public float resultImpactAnimationScaleMultiplier = 0.45f;
        [Min(0.01f)] public float resultFinalBulletHoleScaleMultiplier = 2f;

        [Header("Marker")]
        public GameObject bulletImpactMarkerTemplate;
        public GameObject resultBulletImpactMarkerTemplate;
        public Sprite[] bulletImpactFrames;
        public bool preferAssignedBulletImpactSprites = true;
        public float gomokuMarkerWorldSize = 0.17f;
        public float ticTacToeMarkerWorldSize = 0.72f;
        public bool useBulletImpactSpritePivot = true;
        [Min(1f)] public float impactFrameRate = 18f;
        [Min(0.1f)] public float impactAnimationScaleMultiplier = 0.225f;
        [Min(0.01f)] public float finalBulletHoleScaleMultiplier = 1f;
        public Color[] playerColors =
        {
            new Color(0.3f, 1f, 0.35f, 1f),
            new Color(1f, 0.25f, 0.2f, 1f),
            new Color(0.25f, 0.55f, 1f, 1f),
            new Color(1f, 0.9f, 0.25f, 1f)
        };

        [Header("Frying Pan Item")]
        public float fryingPanSpawnMinDelay = 3.5f;
        public float fryingPanSpawnMaxDelay = 7.5f;
        public float fryingPanFlightDuration = 1.35f;
        public float fryingPanHitRadius = 0.55f;
        public float fryingPanWorldSize = 0.9f;

        private GomokuBoardState boardState;
        private readonly List<ShotRecord> shotRecords = new List<ShotRecord>();
        private readonly List<Vector2Int> winningLineCells = new List<Vector2Int>();
        private readonly int[] playerStunTurns = new int[4];
        private int currentPlayer = 1;
        private int remainingShots;
        private int roundIndex = 1;
        private int winner;
        private string statusMessage = "Ready";
        private bool showingResult;
        private int generatedIdleFrameIndex;
        private int generatedIdleFrameCount = 1;
        private int generatedFlyingFrameStartIndex = 1;
        private int generatedFlyingFrameCount = 1;
        private int generatedUnfoldFrameStartIndex = 2;
        private int generatedUnfoldFrameCount = 1;
        private int generatedAttachedFrameIndex = 2;
        private Sprite[] generatedScarecrowIdleFrames = new Sprite[0];
        private Sprite[] generatedScarecrowAttackedFrames = new Sprite[0];
        private Sprite[] generatedScarecrowDeathFrames = new Sprite[0];
        private Sprite[] generatedBulletImpactFrames = new Sprite[0];
        private GameObject fryingPanObject;
        private SpriteRenderer fryingPanRenderer;
        private Vector3 fryingPanStart;
        private Vector3 fryingPanEnd;
        private float fryingPanTimer;
        private float fryingPanNextSpawnTimer;
        private bool fryingPanActive;
        private bool fryingPanReflecting;

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
        public int PlayerCount => GetPlayerCount();

        private void Start()
        {
            ApplyMenuLaunchOptions();
            EnsureBoardWarp();
            EnsureGeneratedPaperSprites();
            EnsureBoardGridRenderer();
            EnsureGeneratedBulletImpactSprites();
            EnsureGeneratedScarecrowSprites();
            EnsureScarecrowCarrier();
            EnsureFryingPanVisual();
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
            SyncWorldShotMarksVisibility();

            if (WasResetPressed())
            {
                ResetMatch();
                return;
            }

            UpdateFryingPanItem();

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
            winningLineCells.Clear();
            for (int i = 0; i < playerStunTurns.Length; i++)
            {
                playerStunTurns[i] = 0;
            }

            ScheduleNextFryingPan();
            SetFryingPanVisible(false);

            if (boardTarget != null)
            {
                boardTarget.boardSize = boardSize;
                ApplyBoardSizing();
            }

            if (windMotion != null)
            {
                windMotion.SetResultView(false);
            }

            SetBoardPaperVisible(true);

            if (scarecrowCarrier != null)
            {
                scarecrowCarrier.ResetCarrier();
            }

            if (resultOverlay != null)
            {
                resultOverlay.Hide();
            }

            ClearMarks();
            SyncWorldShotMarksVisibility();
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
            winningLineCells.Clear();
            ScheduleNextFryingPan();
            SetFryingPanVisible(false);
            SkipStunnedCurrentPlayerIfNeeded();

            if (resultOverlay != null)
            {
                resultOverlay.Hide();
            }

            SetBoardPaperVisible(true);

            if (windMotion != null)
            {
                windMotion.SetResultView(false);
            }

            if (scarecrowCarrier != null)
            {
                scarecrowCarrier.ResetCarrier();
            }

            SyncWorldShotMarksVisibility();
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

            Vector2 screenPosition = GetPointerScreenPosition();
            Vector3 worldPosition = targetCamera.ScreenToWorldPoint(new Vector3(screenPosition.x, screenPosition.y, -targetCamera.transform.position.z));

            if (itemEnabled && TryHitFryingPan(worldPosition))
            {
                HandleFryingPanShot();
                return;
            }

            if (requireScarecrowAttached && scarecrowCarrier != null && !scarecrowCarrier.CanShoot)
            {
                statusMessage = "Wait for paper";
                return;
            }

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
                SyncWorldShotMarksVisibility();
                return;
            }

            shotRecords.Add(new ShotRecord(cell, currentPlayer));
            SpawnMarker(cell, currentPlayer);

            int winLength = gameMode == GameMode.Gomoku ? 5 : 3;
            bool overlineAllowed = gameMode != GameMode.Gomoku || allowOverline;
            if (boardState.TryGetWinningLine(cell, currentPlayer, winLength, overlineAllowed, winningLineCells))
            {
                winner = currentPlayer;
                ShowRoundResult($"{GetPlayerDisplayName(currentPlayer)} Win");
                return;
            }

            statusMessage = targetDown ? "Scarecrow down" : $"Hit {cell.x + 1}, {cell.y + 1}";

            ResolveAmmoAfterShot();
            SyncWorldShotMarksVisibility();
        }

        private void MoveToNextPlayer()
        {
            int playerCount = GetPlayerCount();
            currentPlayer++;
            if (currentPlayer > playerCount)
            {
                currentPlayer = 1;
            }

            statusMessage = $"P{currentPlayer} turn";
            SkipStunnedCurrentPlayerIfNeeded();
        }

        private void SkipStunnedCurrentPlayerIfNeeded()
        {
            int playerCount = GetPlayerCount();
            int skippedCount = 0;

            while (skippedCount < playerCount)
            {
                int playerIndex = Mathf.Clamp(currentPlayer - 1, 0, playerStunTurns.Length - 1);
                if (playerStunTurns[playerIndex] <= 0)
                {
                    break;
                }

                playerStunTurns[playerIndex]--;
                skippedCount++;
                currentPlayer++;
                if (currentPlayer > playerCount)
                {
                    currentPlayer = 1;
                }
            }

            remainingShots = GetShotsPerTurn();
            if (skippedCount >= playerCount)
            {
                statusMessage = "All players recovered";
            }
            else if (skippedCount > 0)
            {
                statusMessage = $"P{currentPlayer} turn after stun skip";
            }
        }

        private void ShowRoundResult(string message)
        {
            showingResult = true;
            statusMessage = message;
            windMotion?.SetResultView(true);
            boardGridRenderer?.SetVisible(false);
            SetFryingPanVisible(false);
            SetWorldShotMarksVisible(false);
            EnsureResultOverlay();

            if (resultOverlay != null)
            {
                resultOverlay.bulletHoleSprite = bulletHoleSprite;
                if (useResultGridOverride)
                {
                    resultOverlay.gridNormalizedRect = resultGridNormalizedRectOverride;
                }
                else if (syncResultGridFromBoardTarget && boardTarget != null)
                {
                    resultOverlay.gridNormalizedRect = boardTarget.GridNormalizedRect;
                }

                resultOverlay.playerColors = playerColors;
                resultOverlay.boardImageScaleMultiplier = resultBoardImageScaleMultiplier;
                resultOverlay.gomokuMarkerWorldSize = gomokuMarkerWorldSize * resultGomokuMarkerWorldSizeMultiplier;
                resultOverlay.ticTacToeMarkerWorldSize = ticTacToeMarkerWorldSize * resultTicTacToeMarkerWorldSizeMultiplier;
                resultOverlay.bulletImpactFrames = generatedBulletImpactFrames;
                resultOverlay.impactFrameRate = impactFrameRate;
                resultOverlay.impactAnimationScaleMultiplier = resultImpactAnimationScaleMultiplier;
                resultOverlay.finalBulletHoleScaleMultiplier = resultFinalBulletHoleScaleMultiplier;
                resultOverlay.useBulletImpactSpritePivot = useBulletImpactSpritePivot;
                resultOverlay.bulletImpactMarkerTemplate = GetResultBulletImpactMarkerTemplate();
                resultOverlay.Show(
                    resultBoardSprite,
                    shotRecords,
                    GetBoardSize(),
                    gameMode,
                    winningLineCells,
                    winner,
                    winner > 0 ? GetPlayerDisplayName(winner) : string.Empty,
                    generatedBulletImpactFrames);
            }

            SetBoardPaperVisible(false);
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
                if (resultOverlay.gridRenderer == null)
                {
                    resultOverlay.gridRenderer = resultOverlay.GetComponent<PaperBoardGridRenderer>();
                }

                if (resultOverlay.gridRenderer == null)
                {
                    resultOverlay.gridRenderer = resultOverlay.gameObject.AddComponent<PaperBoardGridRenderer>();
                    resultOverlay.gridRenderer.syncFromTarget = false;
                }

                resultOverlay.boardImageScaleMultiplier = resultBoardImageScaleMultiplier;
                resultOverlay.bulletImpactFrames = generatedBulletImpactFrames;
                resultOverlay.impactFrameRate = impactFrameRate;
                resultOverlay.impactAnimationScaleMultiplier = resultImpactAnimationScaleMultiplier;
                resultOverlay.finalBulletHoleScaleMultiplier = resultFinalBulletHoleScaleMultiplier;
                resultOverlay.useBulletImpactSpritePivot = useBulletImpactSpritePivot;
                resultOverlay.bulletImpactMarkerTemplate = GetResultBulletImpactMarkerTemplate();
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
            resultOverlay.bulletImpactFrames = generatedBulletImpactFrames;
            resultOverlay.impactFrameRate = impactFrameRate;
            resultOverlay.impactAnimationScaleMultiplier = resultImpactAnimationScaleMultiplier;
            resultOverlay.finalBulletHoleScaleMultiplier = resultFinalBulletHoleScaleMultiplier;
            resultOverlay.useBulletImpactSpritePivot = useBulletImpactSpritePivot;
            resultOverlay.bulletImpactMarkerTemplate = GetResultBulletImpactMarkerTemplate();
            resultOverlay.boardImageScaleMultiplier = resultBoardImageScaleMultiplier;
            resultOverlay.gridRenderer = overlayObject.AddComponent<PaperBoardGridRenderer>();
            resultOverlay.gridRenderer.syncFromTarget = false;
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

        private void EnsureGeneratedPaperSprites()
        {
            Sprite idleSprite = Resources.Load<Sprite>(PaperIdleResourcePath);
            Sprite flyingSprite = Resources.Load<Sprite>(PaperFlyingResourcePath);
            Sprite attachedSprite = Resources.Load<Sprite>(PaperAttachedResourcePath);
            List<Sprite> idleSprites = LoadGeneratedFrames(PaperPromptIdleFrameResourcePrefix, 16);
            List<Sprite> flyingSprites = LoadGeneratedFrames(PaperPromptFlyingFrameResourcePrefix, 32);
            List<Sprite> unfoldSprites = LoadGeneratedFrames(PaperPromptUnfoldFrameResourcePrefix, 16);
            List<Sprite> legacyFlyingSprites = flyingSprites.Count == 0 ? LoadGeneratedFlightFrames() : new List<Sprite>();

            if (idleSprite == null && flyingSprite == null && attachedSprite == null &&
                idleSprites.Count == 0 && flyingSprites.Count == 0 && unfoldSprites.Count == 0 && legacyFlyingSprites.Count == 0)
            {
                return;
            }

            List<Sprite> frames = new List<Sprite>();
            generatedIdleFrameIndex = frames.Count;
            if (idleSprites.Count > 0)
            {
                frames.AddRange(idleSprites);
                generatedIdleFrameCount = idleSprites.Count;
            }
            else if (idleSprite != null)
            {
                frames.Add(idleSprite);
                generatedIdleFrameCount = 1;
            }

            generatedFlyingFrameStartIndex = frames.Count;
            if (flyingSprites.Count > 0)
            {
                frames.AddRange(flyingSprites);
                generatedFlyingFrameCount = flyingSprites.Count;
            }
            else if (legacyFlyingSprites.Count > 0)
            {
                frames.AddRange(legacyFlyingSprites);
                generatedFlyingFrameCount = legacyFlyingSprites.Count;
            }
            else if (flyingSprite != null)
            {
                frames.Add(flyingSprite);
                generatedFlyingFrameCount = 1;
            }

            generatedUnfoldFrameStartIndex = frames.Count;
            if (unfoldSprites.Count > 0)
            {
                frames.AddRange(unfoldSprites);
                generatedUnfoldFrameCount = unfoldSprites.Count;
                generatedAttachedFrameIndex = frames.Count - 1;
            }
            else if (attachedSprite != null)
            {
                frames.Add(attachedSprite);
                generatedUnfoldFrameCount = 1;
                generatedAttachedFrameIndex = frames.Count - 1;
            }
            else if (frames.Count > 0)
            {
                generatedUnfoldFrameStartIndex = frames.Count - 1;
                generatedUnfoldFrameCount = 1;
                generatedAttachedFrameIndex = frames.Count - 1;
            }

            if (windMotion != null)
            {
                windMotion.frames = frames.ToArray();
                if (windMotion.spriteRenderer != null && frames.Count > 0)
                {
                    windMotion.spriteRenderer.sprite = frames[Mathf.Clamp(generatedIdleFrameIndex, 0, frames.Count - 1)];
                }
            }

            if (frames.Count > 0)
            {
                resultBoardSprite = frames[Mathf.Clamp(generatedIdleFrameIndex, 0, frames.Count - 1)];
            }
        }

        private static List<Sprite> LoadGeneratedFlightFrames()
        {
            return LoadGeneratedFrames(PaperFlightFrameResourcePrefix, 32);
        }

        private void EnsureGeneratedScarecrowSprites()
        {
            generatedScarecrowIdleFrames = LoadGeneratedFrames(ScarecrowIdleFrameResourcePrefix, 16).ToArray();
            generatedScarecrowAttackedFrames = LoadGeneratedFrames(ScarecrowAttackedFrameResourcePrefix, 16).ToArray();
            generatedScarecrowDeathFrames = LoadGeneratedFrames(ScarecrowDeathFrameResourcePrefix, 16).ToArray();
        }

        private void EnsureGeneratedBulletImpactSprites()
        {
            if (preferAssignedBulletImpactSprites && HasSprites(bulletImpactFrames))
            {
                generatedBulletImpactFrames = GetValidSprites(bulletImpactFrames);
                return;
            }

            generatedBulletImpactFrames = LoadGeneratedFrames(BulletImpactFrameResourcePrefix, 16).ToArray();
        }

        private static List<Sprite> LoadGeneratedFrames(string resourcePrefix, int maxFrameCount)
        {
            List<Sprite> sprites = new List<Sprite>();
            for (int index = 0; index < maxFrameCount; index++)
            {
                string resourcePath = $"{resourcePrefix}{index:00}";
                Sprite sprite = Resources.Load<Sprite>(resourcePath);
                if (sprite == null)
                {
                    sprite = SelectLargestSprite(Resources.LoadAll<Sprite>(resourcePath));
                }

                if (sprite == null)
                {
                    Texture2D texture = Resources.Load<Texture2D>(resourcePath);
                    if (texture != null)
                    {
                        sprite = Sprite.Create(
                            texture,
                            new Rect(0f, 0f, texture.width, texture.height),
                            new Vector2(0.5f, 0.5f),
                            100f);
                        sprite.name = texture.name;
                    }
                }

                if (sprite != null)
                {
                    sprites.Add(sprite);
                }
            }

            return sprites;
        }

        private static Sprite SelectLargestSprite(Sprite[] sprites)
        {
            Sprite selected = null;
            float selectedArea = 0f;
            if (sprites == null)
            {
                return null;
            }

            for (int i = 0; i < sprites.Length; i++)
            {
                Sprite sprite = sprites[i];
                if (sprite == null)
                {
                    continue;
                }

                float area = sprite.rect.width * sprite.rect.height;
                if (selected == null || area > selectedArea)
                {
                    selected = sprite;
                    selectedArea = area;
                }
            }

            return selected;
        }

        private void EnsureBoardGridRenderer()
        {
            if (boardTarget == null)
            {
                return;
            }

            if (boardGridRenderer == null)
            {
                boardGridRenderer = boardTarget.GetComponent<PaperBoardGridRenderer>();
            }

            if (boardGridRenderer == null)
            {
                boardGridRenderer = boardTarget.gameObject.AddComponent<PaperBoardGridRenderer>();
            }

            boardGridRenderer.boardTarget = boardTarget;
            boardGridRenderer.syncFromTarget = true;
            boardGridRenderer.RefreshFromTarget();
            boardGridRenderer.SetVisible(false);
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
            scarecrowCarrier.boardGridRenderer = boardGridRenderer;
            scarecrowCarrier.targetCamera = targetCamera != null ? targetCamera : Camera.main;
            scarecrowCarrier.paperAttachedScale = boardTarget.transform.localScale;
            scarecrowCarrier.idlePaperFrameIndex = generatedIdleFrameIndex;
            scarecrowCarrier.idlePaperFrameCount = Mathf.Max(1, generatedIdleFrameCount);
            scarecrowCarrier.flyingPaperFrameIndex = generatedFlyingFrameStartIndex;
            scarecrowCarrier.flyingPaperFrameStartIndex = generatedFlyingFrameStartIndex;
            scarecrowCarrier.flyingPaperFrameCount = Mathf.Max(1, generatedFlyingFrameCount);
            scarecrowCarrier.unfoldPaperFrameStartIndex = generatedUnfoldFrameStartIndex;
            scarecrowCarrier.unfoldPaperFrameCount = Mathf.Max(1, generatedUnfoldFrameCount);
            scarecrowCarrier.attachedPaperFrameIndex = generatedAttachedFrameIndex;

            if (generatedScarecrowIdleFrames.Length > 0 && (!preferAssignedScarecrowSprites || !HasSprites(scarecrowCarrier.scarecrowIdleFrames)))
            {
                scarecrowCarrier.scarecrowIdleFrames = generatedScarecrowIdleFrames;
                scarecrowCarrier.scarecrowSprite = generatedScarecrowIdleFrames[0];
            }

            if (generatedScarecrowAttackedFrames.Length > 0 && (!preferAssignedScarecrowSprites || !HasSprites(scarecrowCarrier.scarecrowAttackedFrames)))
            {
                scarecrowCarrier.scarecrowAttackedFrames = generatedScarecrowAttackedFrames;
            }

            if (generatedScarecrowDeathFrames.Length > 0 && (!preferAssignedScarecrowSprites || !HasSprites(scarecrowCarrier.scarecrowDeathFrames)))
            {
                scarecrowCarrier.scarecrowDeathFrames = generatedScarecrowDeathFrames;
                scarecrowCarrier.knockedDownSprite = generatedScarecrowDeathFrames[generatedScarecrowDeathFrames.Length - 1];
            }
        }

        private static bool HasSprites(Sprite[] sprites)
        {
            if (sprites == null || sprites.Length == 0)
            {
                return false;
            }

            for (int i = 0; i < sprites.Length; i++)
            {
                if (sprites[i] != null)
                {
                    return true;
                }
            }

            return false;
        }

        private static Sprite[] GetValidSprites(Sprite[] sprites)
        {
            if (sprites == null || sprites.Length == 0)
            {
                return new Sprite[0];
            }

            List<Sprite> validSprites = new List<Sprite>();
            for (int i = 0; i < sprites.Length; i++)
            {
                if (sprites[i] != null)
                {
                    validSprites.Add(sprites[i]);
                }
            }

            return validSprites.ToArray();
        }

        private void EnsureFryingPanVisual()
        {
            if (fryingPanObject != null)
            {
                return;
            }

            fryingPanObject = new GameObject("Frying Pan Item");
            fryingPanObject.transform.position = new Vector3(0f, 0f, -0.4f);
            fryingPanRenderer = fryingPanObject.AddComponent<SpriteRenderer>();
            fryingPanRenderer.sprite = fryingPanItemSprite;
            fryingPanRenderer.sortingOrder = 80;

            if (fryingPanItemSprite == null)
            {
                fryingPanRenderer.sprite = CreateRuntimeCircleSprite(new Color(0.14f, 0.13f, 0.12f, 1f), new Color(0.78f, 0.67f, 0.5f, 1f));
            }

            float sourceSize = fryingPanRenderer.sprite != null
                ? Mathf.Max(fryingPanRenderer.sprite.bounds.size.x, fryingPanRenderer.sprite.bounds.size.y, 0.01f)
                : 1f;
            fryingPanObject.transform.localScale = Vector3.one * (fryingPanWorldSize / sourceSize);
            SetFryingPanVisible(false);
        }

        private void UpdateFryingPanItem()
        {
            if (!itemEnabled || showingResult || winner != 0)
            {
                SetFryingPanVisible(false);
                return;
            }

            EnsureFryingPanVisual();
            if (fryingPanObject == null)
            {
                return;
            }

            if (!fryingPanActive && !fryingPanReflecting)
            {
                fryingPanNextSpawnTimer -= Time.deltaTime;
                if (fryingPanNextSpawnTimer <= 0f)
                {
                    SpawnFryingPanFlight();
                }

                return;
            }

            fryingPanTimer += Time.deltaTime;
            float duration = fryingPanReflecting ? 0.42f : Mathf.Max(0.2f, fryingPanFlightDuration);
            float t = Mathf.Clamp01(fryingPanTimer / duration);
            float eased = Mathf.SmoothStep(0f, 1f, t);
            fryingPanObject.transform.position = Vector3.Lerp(fryingPanStart, fryingPanEnd, eased);
            fryingPanObject.transform.Rotate(0f, 0f, (fryingPanReflecting ? 900f : 520f) * Time.deltaTime);

            if (t >= 1f)
            {
                SetFryingPanVisible(false);
                ScheduleNextFryingPan();
            }
        }

        private void SpawnFryingPanFlight()
        {
            if (targetCamera == null)
            {
                targetCamera = Camera.main;
            }

            if (targetCamera == null)
            {
                return;
            }

            float halfHeight = targetCamera.orthographicSize;
            float halfWidth = halfHeight * targetCamera.aspect;
            bool leftToRight = Random.value > 0.5f;
            float y = Random.Range(-halfHeight * 0.35f, halfHeight * 0.55f);
            fryingPanStart = new Vector3(leftToRight ? -halfWidth - 1.2f : halfWidth + 1.2f, y, -0.4f);
            fryingPanEnd = new Vector3(leftToRight ? halfWidth + 1.2f : -halfWidth - 1.2f, y + Random.Range(-0.8f, 0.8f), -0.4f);
            fryingPanTimer = 0f;
            fryingPanReflecting = false;
            fryingPanActive = true;
            fryingPanObject.transform.position = fryingPanStart;
            fryingPanObject.transform.rotation = Quaternion.Euler(0f, 0f, Random.Range(0f, 360f));
            fryingPanObject.SetActive(true);
        }

        private bool TryHitFryingPan(Vector3 worldPosition)
        {
            if (!fryingPanActive || fryingPanObject == null || !fryingPanObject.activeSelf)
            {
                return false;
            }

            return Vector2.Distance(worldPosition, fryingPanObject.transform.position) <= fryingPanHitRadius;
        }

        private void HandleFryingPanShot()
        {
            remainingShots = Mathf.Max(remainingShots - 1, 0);
            cowboyAnimator?.PlayOnce();

            int stunnedPlayer = PickReflectedPlayer();
            if (stunnedPlayer > 0)
            {
                playerStunTurns[stunnedPlayer - 1] = Mathf.Max(playerStunTurns[stunnedPlayer - 1], 1);
                statusMessage = $"Frying pan reflected! P{stunnedPlayer} stunned";
            }
            else
            {
                statusMessage = "Frying pan reflected!";
            }

            Vector2 direction = Random.insideUnitCircle.normalized;
            if (direction.sqrMagnitude < 0.01f)
            {
                direction = Vector2.right;
            }

            fryingPanStart = fryingPanObject != null ? fryingPanObject.transform.position : Vector3.zero;
            fryingPanEnd = fryingPanStart + new Vector3(direction.x, direction.y, 0f) * 4.5f;
            fryingPanTimer = 0f;
            fryingPanActive = false;
            fryingPanReflecting = true;
            if (fryingPanObject != null)
            {
                fryingPanObject.SetActive(true);
            }

            ResolveAmmoAfterShot();
            SyncWorldShotMarksVisibility();
        }

        private int PickReflectedPlayer()
        {
            int playerCount = GetPlayerCount();
            if (playerCount <= 1)
            {
                return currentPlayer;
            }

            int target = currentPlayer;
            for (int attempt = 0; attempt < 12 && target == currentPlayer; attempt++)
            {
                target = Random.Range(1, playerCount + 1);
            }

            return target;
        }

        private void SetFryingPanVisible(bool visible)
        {
            fryingPanActive = visible && fryingPanActive;
            fryingPanReflecting = visible && fryingPanReflecting;
            if (fryingPanObject != null && fryingPanObject.activeSelf != visible)
            {
                fryingPanObject.SetActive(visible);
            }
        }

        private void ScheduleNextFryingPan()
        {
            fryingPanActive = false;
            fryingPanReflecting = false;
            fryingPanNextSpawnTimer = Random.Range(
                Mathf.Max(0.5f, fryingPanSpawnMinDelay),
                Mathf.Max(fryingPanSpawnMinDelay + 0.1f, fryingPanSpawnMaxDelay));
        }

        private void ApplyBoardSizing()
        {
            if (boardTarget == null)
            {
                return;
            }

            if (applyInGameBoardWorldSize)
            {
                boardTarget.boardWorldSize = inGameBoardWorldSize;
            }
            if (boardGridRenderer == null)
            {
                boardGridRenderer = boardTarget.GetComponent<PaperBoardGridRenderer>();
            }

            if (boardGridRenderer != null)
            {
                boardGridRenderer.boardTarget = boardTarget;
                boardGridRenderer.RefreshFromTarget();
                if (scarecrowCarrier != null)
                {
                    scarecrowCarrier.boardGridRenderer = boardGridRenderer;
                }
            }

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

            Vector2 activeBoardWorldSize = applyInGameBoardWorldSize ? inGameBoardWorldSize : boardTarget.boardWorldSize;
            float targetSize = Mathf.Max(activeBoardWorldSize.x, activeBoardWorldSize.y);
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
            Sprite markerSprite = GetFinalBulletMarkerSprite();
            Sprite firstImpactSprite = GetFirstBulletImpactSprite();
            if ((markerSprite == null && firstImpactSprite == null) || boardTarget == null)
            {
                return;
            }

            GameObject marker = new GameObject($"P{player}_Shot_{cell.x}_{cell.y}");
            Transform markerTransform = marker.transform;
            markerTransform.SetParent(markRoot != null ? markRoot : boardTarget.transform, false);
            markerTransform.localPosition = boardTarget.CellToLocal(cell);
            markerTransform.localRotation = Quaternion.identity;

            float targetWorldSize = gameMode == GameMode.Gomoku ? gomokuMarkerWorldSize : ticTacToeMarkerWorldSize;
            Vector3 finalScale = Vector3.one * (targetWorldSize * finalBulletHoleScaleMultiplier / GetSpriteSourceSize(markerSprite != null ? markerSprite : firstImpactSprite));
            Vector3 impactScale = Vector3.one * (targetWorldSize * impactAnimationScaleMultiplier / GetSpriteSourceSize(GetLastBulletImpactSprite() != null ? GetLastBulletImpactSprite() : firstImpactSprite));
            Transform visualTransform = CreateBulletImpactMarkerVisual(
                markerTransform,
                bulletImpactMarkerTemplate,
                firstImpactSprite,
                markerSprite,
                generatedBulletImpactFrames,
                generatedBulletImpactFrames.Length > 0,
                20 + player,
                GetPlayerColor(player),
                impactScale,
                finalScale);

            WarpedBoardMarker warpedMarker = marker.AddComponent<WarpedBoardMarker>();
            warpedMarker.Configure(boardTarget, boardWarp, cell, visualTransform);
            SyncWorldShotMarksVisibility();
        }

        private Transform CreateBulletImpactMarkerVisual(
            Transform parent,
            GameObject template,
            Sprite firstImpactSprite,
            Sprite finalMarkerSprite,
            Sprite[] impactFrames,
            bool playImpact,
            int sortingOrder,
            Color color,
            Vector3 impactScale,
            Vector3 finalScale)
        {
            GameObject visual = template != null ? Instantiate(template, parent, false) : new GameObject("Bullet Impact Marker");
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
            renderer.sortingOrder = sortingOrder;
            renderer.color = color;

            Transform scaleTarget = renderer.transform;
            Vector3 templateScale = scaleTarget.localScale;
            scaleTarget.localScale = Vector3.Scale(templateScale, playImpact ? impactScale : finalScale);

            SpriteBoundsCenterer centerer = ConfigureBulletImpactCenterer(renderer);

            if (playImpact && impactFrames != null && impactFrames.Length > 0)
            {
                BulletImpactAnimator animator = renderer.GetComponent<BulletImpactAnimator>();
                if (animator == null)
                {
                    animator = renderer.gameObject.AddComponent<BulletImpactAnimator>();
                }

                animator.spriteRenderer = renderer;
                animator.frames = impactFrames;
                animator.finalSprite = finalMarkerSprite;
                animator.targetTransform = scaleTarget;
                animator.centerer = centerer != null && centerer.enabled ? centerer : null;
                animator.finalLocalScale = Vector3.Scale(templateScale, finalScale);
                animator.applyFinalLocalScale = finalMarkerSprite != null;
                animator.framesPerSecond = impactFrameRate;
                animator.Play();
            }

            return scaleTarget;
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

        private GameObject GetResultBulletImpactMarkerTemplate()
        {
            if (resultBulletImpactMarkerTemplate != null)
            {
                return resultBulletImpactMarkerTemplate;
            }

            return bulletImpactMarkerTemplate;
        }

        private Sprite GetFirstBulletImpactSprite()
        {
            return generatedBulletImpactFrames.Length > 0 ? generatedBulletImpactFrames[0] : null;
        }

        private Sprite GetLastBulletImpactSprite()
        {
            return generatedBulletImpactFrames.Length > 0 ? generatedBulletImpactFrames[generatedBulletImpactFrames.Length - 1] : null;
        }

        private Sprite GetFinalBulletMarkerSprite()
        {
            return bulletHoleSprite != null ? bulletHoleSprite : generatedBulletImpactFrames.Length > 0 ? generatedBulletImpactFrames[generatedBulletImpactFrames.Length - 1] : null;
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

        private void SyncWorldShotMarksVisibility()
        {
            SetWorldShotMarksVisible(ShouldShowWorldShotMarks());
        }

        private bool ShouldShowWorldShotMarks()
        {
            if (showingResult || winner != 0)
            {
                return false;
            }

            if (scarecrowCarrier != null && !scarecrowCarrier.CanShoot)
            {
                return false;
            }

            return true;
        }

        private void SetWorldShotMarksVisible(bool visible)
        {
            if (markRoot == null)
            {
                return;
            }

            GameObject markRootObject = markRoot.gameObject;
            if (markRootObject.activeSelf != visible)
            {
                markRootObject.SetActive(visible);
            }
        }

        private void SetBoardPaperVisible(bool visible)
        {
            if (boardTarget == null)
            {
                return;
            }

            GameObject boardObject = boardTarget.gameObject;
            if (boardObject.activeSelf != visible)
            {
                boardObject.SetActive(visible);
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

        public string GetPlayerDisplayName(int player)
        {
            if (player == 1)
            {
                return PlayerPrefs.GetString("NHN.PlayerName", "Player");
            }

            return $"P{player}";
        }

        public int GetDisplayedAmmoForPlayer(int player)
        {
            if (player < 1 || player > GetPlayerCount() || showingResult || winner != 0)
            {
                return 0;
            }

            if (IsPlayerStunned(player))
            {
                return 0;
            }

            if (player == currentPlayer)
            {
                return remainingShots;
            }

            return player > currentPlayer ? GetShotsPerTurn() : 0;
        }

        public bool IsPlayerStunned(int player)
        {
            if (player < 1 || player > playerStunTurns.Length)
            {
                return false;
            }

            return playerStunTurns[player - 1] > 0;
        }

        private static Sprite CreateRuntimeCircleSprite(Color fillColor, Color rimColor)
        {
            const int size = 96;
            Texture2D texture = new Texture2D(size, size, TextureFormat.RGBA32, false);
            texture.name = "RuntimeFryingPanPlaceholder";
            texture.filterMode = FilterMode.Point;
            float center = (size - 1) * 0.5f;

            for (int y = 0; y < size; y++)
            {
                for (int x = 0; x < size; x++)
                {
                    Vector2 delta = new Vector2(x - center, y - center);
                    float distance = delta.magnitude / center;
                    if (distance > 0.86f)
                    {
                        texture.SetPixel(x, y, Color.clear);
                    }
                    else if (distance > 0.7f)
                    {
                        texture.SetPixel(x, y, rimColor);
                    }
                    else
                    {
                        texture.SetPixel(x, y, fillColor);
                    }
                }
            }

            texture.Apply();
            return Sprite.Create(texture, new Rect(0f, 0f, size, size), new Vector2(0.5f, 0.5f), size);
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

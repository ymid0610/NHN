using System.Collections.Generic;
using NHN.Network;
using UnityEngine;

#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

namespace NHN.InGame
{
    /// <summary>
    /// Plays the match the server is running.
    ///
    /// This class deliberately contains NO game rules. It does not decide who
    /// owns a cell, whether a line was completed, when a wave ends, or how much
    /// ammo anyone has left — the instance server decides all of that, and a
    /// second opinion here would only ever disagree. Everything below is either
    /// drawing what the server replicated or turning a click into a C_Fire.
    ///
    /// It is also why the game is simultaneous rather than turn-based: there is
    /// no current player, every player shoots throughout the wave, and a cell
    /// goes to whoever's shot the server resolves first.
    ///
    /// LocalInGameController stays in the scene for its art and its wiring; it
    /// is switched into networkDriven mode so its own simulation never runs.
    /// </summary>
    public sealed class NetworkInGameController : MonoBehaviour
    {
        [Header("Scene References")]
        public Camera targetCamera;
        /// Optional. When present its loaded sprites and tuning are borrowed, so
        /// the networked game looks like the local prototype without the scene
        /// having to be wired twice.
        public LocalInGameController artSource;
        public PrototypeSpriteAnimator cowboyAnimator;

        [Header("Field Mapping")]
        /// Fit the 16:9 field inside the view rather than stretching it to fill.
        /// Stretching distorts the target sheet and moves where cell edges land,
        /// which changes hits into misses near a boundary.
        public bool stretchFieldToFill;

        [Header("Presentation")]
        public Sprite paperSprite;
        public Sprite bulletHoleSprite;
        public Sprite fryingPanSprite;
        public Sprite decoySprite;
        public Sprite speedLoaderSprite;
        [Min(0.05f)] public float markerCellFraction = 0.72f;
        public Color[] slotColors =
        {
            new Color(0.3f, 1f, 0.35f, 1f),
            new Color(1f, 0.25f, 0.2f, 1f),
            new Color(0.25f, 0.55f, 1f, 1f),
            new Color(1f, 0.9f, 0.25f, 1f)
        };

        public string StatusMessage { get; private set; } = "인스턴스 연결 중";
        public InstanceClient Client { get; private set; }
        public bool HasMatch { get { return Client != null && Client.HasSetup; } }
        public byte MySlot { get { return Client != null ? Client.Slot : GameSlots.NoOwner; } }
        public int RoundNumber { get { return Client != null ? Client.Round.RoundIndex + 1 : 0; } }
        public int RoundCount { get { return Client != null ? Client.Round.RoundCount : 0; } }
        public bool MatchOver { get; private set; }

        private FieldSpace field;
        private Transform sheetRoot;
        private SpriteRenderer sheetRenderer;
        private PaperBoardGridRenderer sheetGrid;
        private Transform markRoot;
        private Transform itemRoot;

        private readonly Dictionary<int, GameObject> markers = new Dictionary<int, GameObject>();
        private readonly Dictionary<uint, GameObject> itemViews = new Dictionary<uint, GameObject>();
        private readonly List<uint> seenEntities = new List<uint>();
        private readonly Dictionary<ServerItemKind, Sprite> placeholderSprites =
            new Dictionary<ServerItemKind, Sprite>();

        private bool sheetVisible;
        private float sheetCellSizeField;
        private float sheetHalfWidthField;
        private float sheetHalfHeightField;

        private void Awake()
        {
            if (artSource == null)
            {
                artSource = FindFirstObjectByType<LocalInGameController>();
            }

            // Awake runs before every Start, so the local prototype is told to
            // stand down before it has a chance to set itself up.
            if (artSource != null && NetworkGameSession.IsNetworked)
            {
                artSource.networkDriven = true;
            }
        }

        private void Start()
        {
            if (targetCamera == null)
            {
                targetCamera = artSource != null && artSource.targetCamera != null
                    ? artSource.targetCamera
                    : Camera.main;
            }

            if (!NetworkGameSession.IsNetworked)
            {
                // Launched straight into the scene without a match — leave the
                // local prototype to run and get out of the way.
                StatusMessage = "로컬 모드 (서버 세션 없음)";
                enabled = false;
                return;
            }

            BorrowArt();
            BuildViews();

            Client = NetworkGameSession.Current.Client;
            Subscribe();
            CatchUp();
        }

        private void OnDestroy()
        {
            Unsubscribe();
        }

        private void Update()
        {
            if (Client == null)
            {
                return;
            }

            field = FieldSpace.FromCamera(targetCamera, stretchFieldToFill);
            if (!field.IsValid)
            {
                return;
            }

            RenderSheet();
            RenderItems();

            if (WasShootPressed())
            {
                TryFireAtPointer();
            }
        }

        // -- input ---------------------------------------------------------------

        private void TryFireAtPointer()
        {
            if (!Client.IsAuthenticated || !Client.WaveActive || MatchOver)
            {
                return;
            }

            if (Client.AmIStunned)
            {
                StatusMessage = "기절 상태 — 이번 웨이브는 사격 불가";
                return;
            }

            // The server also enforces this; checking here only avoids sending a
            // packet that is certain to be rejected.
            ServerMatchConfig config = Client.Setup.Config;
            if (config.AmmoPerWave != GameField.Unlimited && Client.MyAmmo == 0)
            {
                StatusMessage = "탄약 없음 — 다음 웨이브까지 대기";
                return;
            }

            Vector2 fieldPoint;
            if (!field.TryScreenToField(targetCamera, GetPointerScreenPosition(), out fieldPoint))
            {
                return;
            }

            ushort sequence;
            if (Client.Fire(Mathf.RoundToInt(fieldPoint.x), Mathf.RoundToInt(fieldPoint.y), out sequence))
            {
                cowboyAnimator?.PlayOnce();
            }
        }

        // -- rendering -----------------------------------------------------------

        private void RenderSheet()
        {
            ServerEntityState sheet;
            if (!Client.WaveActive || !Client.TryGetSheet(out sheet))
            {
                SetSheetVisible(false);
                return;
            }

            SetSheetVisible(true);

            float worldWidth = field.ToWorldSize(sheet.HalfWidth * 2f);
            float worldHeight = field.ToWorldSize(sheet.HalfHeight * 2f);

            sheetRoot.position = field.FieldToWorld(sheet.X, sheet.Y, 0f);

            if (sheetRenderer.sprite != null)
            {
                Vector2 spriteSize = sheetRenderer.sprite.bounds.size;
                sheetRenderer.transform.localScale = new Vector3(
                    worldWidth / Mathf.Max(spriteSize.x, 0.0001f),
                    worldHeight / Mathf.Max(spriteSize.y, 0.0001f),
                    1f);
            }

            int boardWidth = Mathf.Max(1, Client.Setup.Board.Width);
            int boardHeight = Mathf.Max(1, Client.Setup.Board.Height);

            sheetHalfWidthField = sheet.HalfWidth;
            sheetHalfHeightField = sheet.HalfHeight;
            sheetCellSizeField = sheet.HalfWidth * 2f / boardWidth;

            // boardWidth + 1 lines draws the cell BOUNDARIES. The server tiles
            // the sheet with cell squares; the local prototype's grid renderer
            // normally draws intersections, which would sit half a cell off.
            if (sheetGrid.boardSize != boardWidth + 1 ||
                sheetGrid.boardWorldSize != new Vector2(worldWidth, worldHeight))
            {
                sheetGrid.boardSize = boardWidth + 1;
                sheetGrid.boardWorldSize = new Vector2(worldWidth, worldHeight);
                sheetGrid.gridNormalizedRect = new Rect(0f, 0f, 1f, 1f);
                sheetGrid.lineWidth = Mathf.Max(0.004f, worldWidth / boardWidth * 0.04f);
                sheetGrid.Rebuild();
            }

            PositionMarkers(boardWidth, boardHeight);
        }

        private void PositionMarkers(int boardWidth, int boardHeight)
        {
            float markerSize = field.ToWorldSize(sheetCellSizeField) * markerCellFraction;

            foreach (KeyValuePair<int, GameObject> entry in markers)
            {
                int cellX, cellY;
                Client.Setup.Board.SplitCellIndex(entry.Key, out cellX, out cellY);

                Vector3 local = CellCentreLocal(cellX, cellY);
                entry.Value.transform.localPosition = local;

                SpriteRenderer renderer = entry.Value.GetComponent<SpriteRenderer>();
                if (renderer != null && renderer.sprite != null)
                {
                    float source = Mathf.Max(renderer.sprite.bounds.size.x, renderer.sprite.bounds.size.y, 0.0001f);
                    entry.Value.transform.localScale = Vector3.one * (markerSize / source);
                }
            }
        }

        /// Offset of a cell's centre from the sheet's centre, in world units.
        ///
        /// The server's cell (0,0) is the sheet's TOP-left and its y grows
        /// downward, so the y term is negated on the way into Unity space.
        private Vector3 CellCentreLocal(int cellX, int cellY)
        {
            float dx = (cellX + 0.5f) * sheetCellSizeField - sheetHalfWidthField;
            float dy = (cellY + 0.5f) * sheetCellSizeField - sheetHalfHeightField;
            return new Vector3(field.ToWorldSize(dx), -field.ToWorldSize(dy), -0.02f);
        }

        private void RenderItems()
        {
            seenEntities.Clear();

            if (Client.WaveActive)
            {
                for (int i = 0; i < Client.Entities.Count; i++)
                {
                    ServerEntityState entity = Client.Entities[i];
                    if (entity.Kind != ServerEntityKind.Item)
                    {
                        continue;
                    }

                    seenEntities.Add(entity.EntityId);

                    GameObject view;
                    if (!itemViews.TryGetValue(entity.EntityId, out view))
                    {
                        view = CreateItemView(entity.Item);
                        itemViews[entity.EntityId] = view;
                    }

                    view.transform.position = field.FieldToWorld(entity.X, entity.Y, -0.3f);

                    SpriteRenderer renderer = view.GetComponent<SpriteRenderer>();
                    if (renderer != null && renderer.sprite != null)
                    {
                        float target = field.ToWorldSize(Mathf.Max(entity.HalfWidth, entity.HalfHeight) * 2f);
                        float source = Mathf.Max(renderer.sprite.bounds.size.x, renderer.sprite.bounds.size.y, 0.0001f);
                        view.transform.localScale = Vector3.one * (target / source);
                    }
                }
            }

            // Anything the server stopped replicating has been shot or has left
            // the field.
            List<uint> stale = null;
            foreach (KeyValuePair<uint, GameObject> entry in itemViews)
            {
                if (!seenEntities.Contains(entry.Key))
                {
                    (stale ?? (stale = new List<uint>())).Add(entry.Key);
                }
            }

            if (stale != null)
            {
                for (int i = 0; i < stale.Count; i++)
                {
                    Destroy(itemViews[stale[i]]);
                    itemViews.Remove(stale[i]);
                }
            }
        }

        private GameObject CreateItemView(ServerItemKind kind)
        {
            GameObject view = new GameObject("Item " + kind);
            view.transform.SetParent(itemRoot, false);

            SpriteRenderer renderer = view.AddComponent<SpriteRenderer>();
            renderer.sprite = GetItemSprite(kind);
            renderer.sortingOrder = 90;
            return view;
        }

        private Sprite GetItemSprite(ServerItemKind kind)
        {
            switch (kind)
            {
                case ServerItemKind.FryingPan:
                    if (fryingPanSprite != null) return fryingPanSprite;
                    break;
                case ServerItemKind.WantedPoster:
                    if (decoySprite != null) return decoySprite;
                    break;
                case ServerItemKind.SpeedLoader:
                    if (speedLoaderSprite != null) return speedLoaderSprite;
                    break;
            }

            // Items with no art yet get an honest placeholder rather than a
            // borrowed sprite: a wrong picture reads as a different item.
            Sprite placeholder;
            if (!placeholderSprites.TryGetValue(kind, out placeholder))
            {
                placeholder = CreateDiscSprite(PlaceholderColor(kind));
                placeholderSprites[kind] = placeholder;
            }

            return placeholder;
        }

        private static Color PlaceholderColor(ServerItemKind kind)
        {
            switch (kind)
            {
                case ServerItemKind.Tumbleweed: return new Color(0.62f, 0.48f, 0.24f, 1f);
                case ServerItemKind.WantedPoster: return new Color(0.85f, 0.78f, 0.6f, 1f);
                case ServerItemKind.FryingPan: return new Color(0.18f, 0.17f, 0.16f, 1f);
                case ServerItemKind.Balloon: return new Color(0.9f, 0.3f, 0.45f, 1f);
                case ServerItemKind.PaintCan: return new Color(0.3f, 0.5f, 0.9f, 1f);
                case ServerItemKind.Grenade: return new Color(0.32f, 0.38f, 0.25f, 1f);
                case ServerItemKind.SpeedLoader: return new Color(0.85f, 0.7f, 0.25f, 1f);
                default: return new Color(0.7f, 0.7f, 0.7f, 1f);
            }
        }

        private void SetSheetVisible(bool visible)
        {
            if (sheetVisible == visible)
            {
                return;
            }

            sheetVisible = visible;
            sheetRoot.gameObject.SetActive(visible);
        }

        // -- server events -------------------------------------------------------

        private void Subscribe()
        {
            Client.StatusChanged += OnStatus;
            Client.MatchSetupReceived += OnMatchSetup;
            Client.RoundStarted += OnRoundStarted;
            Client.RoundEnded += OnRoundEnded;
            Client.WaveStarted += OnWaveStarted;
            Client.WaveEnded += OnWaveEnded;
            Client.CellClaimed += OnCellClaimed;
            Client.ShotResolved += OnShotResolved;
            Client.FireRejected += OnFireRejected;
            Client.ItemTriggered += OnItemTriggered;
            Client.PlayerStatusChanged += OnPlayerStatus;
            Client.GameOver += OnGameOver;
            Client.InstanceEnded += OnInstanceEnded;
        }

        private void Unsubscribe()
        {
            if (Client == null)
            {
                return;
            }

            Client.StatusChanged -= OnStatus;
            Client.MatchSetupReceived -= OnMatchSetup;
            Client.RoundStarted -= OnRoundStarted;
            Client.RoundEnded -= OnRoundEnded;
            Client.WaveStarted -= OnWaveStarted;
            Client.WaveEnded -= OnWaveEnded;
            Client.CellClaimed -= OnCellClaimed;
            Client.ShotResolved -= OnShotResolved;
            Client.FireRejected -= OnFireRejected;
            Client.ItemTriggered -= OnItemTriggered;
            Client.PlayerStatusChanged -= OnPlayerStatus;
            Client.GameOver -= OnGameOver;
            Client.InstanceEnded -= OnInstanceEnded;
        }

        /// <summary>
        /// Adopts whatever the client already knows.
        ///
        /// The connection is made in the menu and this scene loads afterwards,
        /// so the match is routinely already running by the time anything here
        /// subscribes. Reading the retained state first is what stops the first
        /// round from being invisible.
        /// </summary>
        private void CatchUp()
        {
            if (!Client.HasSetup)
            {
                return;
            }

            OnMatchSetup(Client.Setup);

            for (int cellIndex = 0; cellIndex < Client.CellOwners.Length; cellIndex++)
            {
                byte owner = Client.CellOwners[cellIndex];
                if (owner != GameSlots.NoOwner)
                {
                    OnCellClaimed(cellIndex, owner);
                }
            }
        }

        private void OnStatus(string message)
        {
            StatusMessage = message;
        }

        private void OnMatchSetup(ServerMatchSetup setup)
        {
            ClearMarkers();
            StatusMessage = string.Format("{0} · {1}목 · {2}라운드",
                ServerProtocolText.ToDisplay(setup.Mode), setup.Board.WinLength, setup.Config.Rounds);
        }

        private void OnRoundStarted(ServerRoundStart round)
        {
            ClearMarkers();
            MatchOver = false;
            StatusMessage = string.Format("라운드 {0}/{1} 시작", round.RoundIndex + 1, round.RoundCount);
        }

        private void OnRoundEnded(ServerRoundEnd round)
        {
            if (round.WinnerSlot == GameSlots.NoOwner)
            {
                StatusMessage = string.Format("라운드 {0} 종료 — 무득점", round.RoundIndex + 1);
                return;
            }

            StatusMessage = string.Format("라운드 {0} 종료 — {1} 승",
                round.RoundIndex + 1, DescribeSlot(round.WinnerSlot));
        }

        private void OnWaveStarted(ServerWaveStart wave)
        {
            StatusMessage = wave.Ammo == GameField.Unlimited
                ? string.Format("웨이브 {0} — 탄약 무제한", wave.WaveIndex + 1)
                : string.Format("웨이브 {0} — 탄약 {1}", wave.WaveIndex + 1, wave.Ammo);
        }

        private void OnWaveEnded(ushort waveIndex)
        {
            ClearItemViews();
        }

        private void OnCellClaimed(int cellIndex, byte ownerSlot)
        {
            if (markers.ContainsKey(cellIndex))
            {
                return;
            }

            Sprite sprite = bulletHoleSprite;
            if (sprite == null)
            {
                sprite = CreateDiscSprite(Color.white);
                bulletHoleSprite = sprite;
            }

            GameObject marker = new GameObject("Cell " + cellIndex + " slot " + ownerSlot);
            marker.transform.SetParent(markRoot, false);

            SpriteRenderer renderer = marker.AddComponent<SpriteRenderer>();
            renderer.sprite = sprite;
            renderer.color = GetSlotColor(ownerSlot);
            renderer.sortingOrder = 40 + ownerSlot;

            markers[cellIndex] = marker;
        }

        private void OnShotResolved(ServerShotResolved shot)
        {
            bool mine = Client.Slot != GameSlots.NoOwner &&
                        NetworkGameSession.Current != null &&
                        shot.Shooter == NetworkGameSession.Current.LocalSessionId;
            if (!mine)
            {
                return;
            }

            switch (shot.Kind)
            {
                case ServerHitKind.Cell:
                    StatusMessage = "명중";
                    break;
                case ServerHitKind.CellTaken:
                    StatusMessage = "이미 먹힌 칸";
                    break;
                case ServerHitKind.Decoy:
                    StatusMessage = "현상수배지 — 탄약만 소모";
                    break;
                case ServerHitKind.Blocker:
                    StatusMessage = "회전초에 막힘";
                    break;
                case ServerHitKind.Item:
                    StatusMessage = "아이템 명중";
                    break;
                default:
                    StatusMessage = "빗나감";
                    break;
            }
        }

        private void OnFireRejected(ushort sequence, ServerResultCode result)
        {
            StatusMessage = "사격 거부: " + result;
        }

        private void OnItemTriggered(ServerItemTriggered item)
        {
            if (item.VictimSlot == ServerItemTriggered.NoVictim)
            {
                return;
            }

            string victim = item.VictimSlot == Client.Slot ? "나" : DescribeSlot(item.VictimSlot);
            switch (item.Item)
            {
                case ServerItemKind.FryingPan:
                    StatusMessage = victim + " 기절";
                    break;
                case ServerItemKind.PaintCan:
                    StatusMessage = victim + " 시야 차단";
                    break;
                case ServerItemKind.SpeedLoader:
                    StatusMessage = victim + " 재장전";
                    break;
            }
        }

        private void OnPlayerStatus(byte slot, ServerStatusFlags flags)
        {
            if (slot != Client.Slot)
            {
                return;
            }

            if ((flags & ServerStatusFlags.Stunned) != 0)
            {
                StatusMessage = "기절 — 이번 웨이브 사격 불가";
            }
        }

        private void OnGameOver(ServerGameOver over)
        {
            MatchOver = true;
            ClearItemViews();
            SetSheetVisible(false);

            if (over.WinnerSlots == null || over.WinnerSlots.Count == 0)
            {
                StatusMessage = "경기 종료";
                return;
            }

            if (over.WinnerSlots.Count > 1)
            {
                StatusMessage = "무승부";
                return;
            }

            StatusMessage = over.WinnerSlots[0] == Client.Slot
                ? "승리!"
                : DescribeSlot(over.WinnerSlots[0]) + " 승";
        }

        private void OnInstanceEnded(string reason)
        {
            MatchOver = true;
            StatusMessage = "경기 종료: " + reason;
        }

        // -- helpers -------------------------------------------------------------

        public string DescribeSlot(byte slot)
        {
            List<ServerRoomMember> players = Client != null ? Client.Setup.Players : null;
            if (players != null)
            {
                for (int i = 0; i < players.Count; i++)
                {
                    if (players[i].Slot == slot)
                    {
                        return players[i].Nickname;
                    }
                }
            }

            return "P" + (slot + 1);
        }

        public Color GetSlotColor(byte slot)
        {
            if (slotColors == null || slotColors.Length == 0)
            {
                return Color.white;
            }

            return slotColors[Mathf.Clamp(slot, 0, slotColors.Length - 1)];
        }

        private void BorrowArt()
        {
            if (artSource == null)
            {
                return;
            }

            if (bulletHoleSprite == null)
            {
                bulletHoleSprite = artSource.bulletHoleSprite;
            }

            if (fryingPanSprite == null)
            {
                fryingPanSprite = artSource.fryingPanItemSprite;
            }

            if (decoySprite == null)
            {
                decoySprite = artSource.outlawBanditSprite;
            }

            if (cowboyAnimator == null)
            {
                cowboyAnimator = artSource.cowboyAnimator;
            }

            // The flat, front-facing paper frame, not the fluttering one: the
            // server's hit box is an axis-aligned rectangle, so a curled sheet
            // would put the grid on a surface the server does not have.
            if (paperSprite == null && artSource.windMotion != null &&
                artSource.windMotion.frames != null && artSource.windMotion.frames.Length > 0)
            {
                paperSprite = artSource.windMotion.frames[0];
            }

            if (artSource.playerColors != null && artSource.playerColors.Length > 0)
            {
                slotColors = artSource.playerColors;
            }
        }

        private void BuildViews()
        {
            sheetRoot = new GameObject("Server Target Sheet").transform;
            sheetRoot.SetParent(transform, false);

            GameObject visual = new GameObject("Sheet Visual");
            visual.transform.SetParent(sheetRoot, false);
            sheetRenderer = visual.AddComponent<SpriteRenderer>();
            sheetRenderer.sprite = paperSprite;
            sheetRenderer.sortingOrder = 20;

            sheetGrid = sheetRoot.gameObject.AddComponent<PaperBoardGridRenderer>();
            sheetGrid.syncFromTarget = false;
            sheetGrid.sortingOrder = 30;

            markRoot = new GameObject("Cell Marks").transform;
            markRoot.SetParent(sheetRoot, false);

            itemRoot = new GameObject("Items").transform;
            itemRoot.SetParent(transform, false);

            sheetRoot.gameObject.SetActive(false);
            sheetVisible = false;
        }

        private void ClearMarkers()
        {
            foreach (KeyValuePair<int, GameObject> entry in markers)
            {
                Destroy(entry.Value);
            }

            markers.Clear();
        }

        private void ClearItemViews()
        {
            foreach (KeyValuePair<uint, GameObject> entry in itemViews)
            {
                Destroy(entry.Value);
            }

            itemViews.Clear();
        }

        private static Sprite CreateDiscSprite(Color color)
        {
            const int size = 64;
            Texture2D texture = new Texture2D(size, size, TextureFormat.RGBA32, false);
            texture.name = "RuntimeDisc";
            float centre = (size - 1) * 0.5f;

            for (int y = 0; y < size; y++)
            {
                for (int x = 0; x < size; x++)
                {
                    float distance = new Vector2(x - centre, y - centre).magnitude / centre;
                    texture.SetPixel(x, y, distance > 0.92f ? Color.clear : color);
                }
            }

            texture.Apply();
            return Sprite.Create(texture, new Rect(0f, 0f, size, size), new Vector2(0.5f, 0.5f), size);
        }

        private static bool WasShootPressed()
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

        private static Vector2 GetPointerScreenPosition()
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

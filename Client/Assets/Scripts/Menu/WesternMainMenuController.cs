using System;
using System.Collections.Generic;
using System.Text;
using NHN.InGame;
using NHN.Network;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.UI;
#endif

namespace NHN.Menu
{
    public sealed class WesternMainMenuController : MonoBehaviour
    {
        private enum MenuView
        {
            Title,
            Name,
            Hub,
            CreateRoom,
            Room,
            FindRoom,
            Matchmaking,
            Settings
        }

        private sealed class SegmentOption<T>
        {
            public Button Button;
            public Image Background;
            public Text Label;
            public T Value;
        }

        public const string PrefPlayerName = "NHN.PlayerName";
        public const string PrefGameMode = "NHN.GameMode";
        public const string PrefMaxPlayers = "NHN.MaxPlayers";
        public const string PrefItemsEnabled = "NHN.ItemsEnabled";
        public const string PrefMasterVolume = "NHN.MasterVolume";
        public const string PrefAimSensitivity = "NHN.AimSensitivity";
        public const string PrefScreenShake = "NHN.ScreenShake";
        public const string PrefFullScreen = "NHN.FullScreen";
        public const string PrefResolutionWidth = "NHN.ResolutionWidth";
        public const string PrefResolutionHeight = "NHN.ResolutionHeight";

        [Header("Scene")]
        public string inGameSceneName = "InGamePrototype";
        public string tutorialSceneName = "Tutorial";

        [Header("Sprites")]
        public Sprite paperSprite;
        public Sprite cowboySprite;
        public Sprite cylinderSprite;
        public Sprite scarecrowSprite;

        [Header("UI Assets")]
        public Sprite menuBackdropSprite;
        public Sprite titleBackdropSprite;
        public Sprite lobbyBackdropSprite;
        public Sprite panelSprite;
        public Sprite darkPanelSprite;
        public Sprite headerSprite;
        public Sprite titlePlaqueSprite;
        public Sprite dividerSprite;
        public Sprite buttonGoldSprite;
        public Sprite buttonGoldHoverSprite;
        public Sprite buttonGoldPressedSprite;
        public Sprite buttonDarkSprite;
        public Sprite buttonDarkHoverSprite;
        public Sprite buttonDarkPressedSprite;
        public Sprite buttonGreenSprite;
        public Sprite buttonGreenHoverSprite;
        public Sprite buttonGreenPressedSprite;
        public Sprite buttonRedSprite;
        public Sprite buttonRedHoverSprite;
        public Sprite buttonRedPressedSprite;
        public Sprite buttonDisabledSprite;
        public Sprite inputSprite;
        public Sprite checkboxSprite;
        public Sprite checkboxCheckSprite;
        public Sprite sliderTrackSprite;
        public Sprite sliderFillSprite;
        public Sprite sliderHandleSprite;

        [Header("Defaults")]
        public string defaultPlayerName = "방랑자";

        [Header("Server")]
        public string matchServerHost = "127.0.0.1";
        [Range(1, 65535)] public int matchServerPort = 7777;

        private readonly Color backgroundColor = new Color32(31, 19, 13, 255);
        private readonly Color panelColor = new Color32(56, 36, 25, 235);
        private readonly Color panelDeepColor = new Color32(34, 23, 18, 245);
        private readonly Color brassColor = new Color32(221, 164, 73, 255);
        private readonly Color brassDarkColor = new Color32(123, 79, 36, 255);
        private readonly Color parchmentColor = new Color32(232, 195, 132, 255);
        private readonly Color parchmentMutedColor = new Color32(180, 141, 88, 255);
        private readonly Color redAccentColor = new Color32(188, 55, 40, 255);
        private readonly Color greenAccentColor = new Color32(122, 181, 91, 255);
        private readonly Color inkColor = new Color32(25, 17, 13, 255);
        private readonly Color disabledColor = new Color32(83, 72, 62, 190);

        private readonly List<SegmentOption<ServerGameMode>> createModeSegments = new List<SegmentOption<ServerGameMode>>();
        private readonly List<SegmentOption<ServerGameMode>> matchmakingModeSegments = new List<SegmentOption<ServerGameMode>>();
        private readonly List<SegmentOption<int>> playerCountSegments = new List<SegmentOption<int>>();
        private readonly List<SegmentOption<int>> resolutionSegments = new List<SegmentOption<int>>();
        private static readonly Vector2Int[] ResolutionPresets =
        {
            new Vector2Int(1280, 720),
            new Vector2Int(1600, 900),
            new Vector2Int(1920, 1080),
            new Vector2Int(2560, 1440)
        };
        private const int DefaultResolutionIndex = 2;

        private Canvas canvas;
        private Font menuFont;
        private Image backgroundImage;
        private GameObject titleScreen;
        private GameObject nameScreen;
        private GameObject lobbyScreen;
        private GameObject hubPanel;
        private GameObject roomPanel;
        private GameObject createRoomPanel;
        private GameObject findRoomPanel;
        private GameObject matchmakingPanel;
        private GameObject settingsPanel;
        private InputField nameInput;
        private InputField roomCodeInput;
        private Text nameBadgeText;
        private Text statusText;
        private Text roomCodeText;
        private Text createSummaryText;
        private Text findStatusText;
        private InputField roomSearchInput;
        private RectTransform roomResultRoot;
        private Toggle hideFullToggle;
        private Toggle hideLockedToggle;
        /// Distinguishes a name search, whose hits are listed, from the code
        /// lookup, which joins the exact match by itself.
        private bool searchingByName;
        private Text matchmakingStatusText;
        private Text matchmakingButtonText;
        private Text serverRoomText;
        private RectTransform slotListRoot;
        private Text roomHeaderText;
        private RectTransform itemToggleRow;
        private Button applyConfigButton;
        private Button readyButton;
        private Button startButton;
        private readonly List<SegmentOption<int>> roundSegments = new List<SegmentOption<int>>();
        private readonly List<SegmentOption<int>> ammoSegments = new List<SegmentOption<int>>();
        private readonly List<SegmentOption<int>> waveSegments = new List<SegmentOption<int>>();
        private readonly List<SegmentOption<int>> paperMinSegments = new List<SegmentOption<int>>();
        private readonly List<SegmentOption<int>> paperMaxSegments = new List<SegmentOption<int>>();
        private readonly Dictionary<ServerItemKind, Toggle> itemToggles =
            new Dictionary<ServerItemKind, Toggle>();
        private ServerMatchConfig draftConfig = ServerMatchConfig.CreateDefault(ServerGameMode.Gomoku9, true);
        private bool suppressConfigEcho;
        private Toggle itemsToggle;
        private Toggle fullScreenToggle;
        private Toggle screenShakeToggle;
        private Slider volumeSlider;
        private Slider sensitivitySlider;
        private MenuView currentView;
        private string playerName;
        private string generatedRoomCode = "미생성";
        private ServerGameMode selectedMode = ServerGameMode.Gomoku9;
        private ServerGameMode matchmakingMode = ServerGameMode.Gomoku9;
        private int createPlayerCount = 4;
        private bool itemsEnabled = true;
        private bool fullScreenEnabled;
        private bool screenShakeEnabled = true;
        private int selectedResolutionIndex = DefaultResolutionIndex;
        private float masterVolume = 0.9f;
        private float aimSensitivity = 1f;
        private bool queuedForMatchmaking;
        private float matchmakingStartTime;
        private MatchServerClient matchClient;
        private Action pendingServerAction;
        private string pendingJoinCode;
        /// Bots still to add once the practice room comes back from the server.
        private int pendingBotFill;

        private void Awake()
        {
            LoadPreferences();
            LoadUiAssetSprites();
            EnsureEventSystem();
            EnsureMatchClient();
            BuildInterface();
            ApplySettingsValuesToUi();
            ShowView(MenuView.Title);
        }

        private void Update()
        {
            if (WasSubmitPressed())
            {
                if (currentView == MenuView.Title)
                {
                    ShowView(MenuView.Name);
                    return;
                }

                if (currentView == MenuView.Name)
                {
                    ConfirmPlayerName();
                    return;
                }
            }

            UpdateMatchmakingStatus();
        }

        private void LoadPreferences()
        {
            playerName = PlayerPrefs.GetString(PrefPlayerName, defaultPlayerName);
            // Sanitise rather than clamp: the modes were renumbered to the
            // server's values, so an old preference can name one that is gone.
            selectedMode = ModeRules.Sanitise((ServerGameMode)PlayerPrefs.GetInt(PrefGameMode, (int)ServerGameMode.Gomoku9));
            matchmakingMode = selectedMode;
            createPlayerCount = PlayerPrefs.GetInt(PrefMaxPlayers, ModeRules.For(selectedMode).Capacity);
            itemsEnabled = PlayerPrefs.GetInt(PrefItemsEnabled, 1) == 1;
            masterVolume = Mathf.Clamp01(PlayerPrefs.GetFloat(PrefMasterVolume, 0.9f));
            aimSensitivity = Mathf.Clamp(PlayerPrefs.GetFloat(PrefAimSensitivity, 1f), 0.5f, 2f);
            screenShakeEnabled = PlayerPrefs.GetInt(PrefScreenShake, 1) == 1;
            fullScreenEnabled = PlayerPrefs.GetInt(PrefFullScreen, Screen.fullScreen ? 1 : 0) == 1;

            bool hasSavedResolution = PlayerPrefs.HasKey(PrefResolutionWidth) && PlayerPrefs.HasKey(PrefResolutionHeight);
            int fallbackWidth = Screen.currentResolution.width > 0 ? Screen.currentResolution.width : Screen.width;
            int fallbackHeight = Screen.currentResolution.height > 0 ? Screen.currentResolution.height : Screen.height;
            int savedWidth = PlayerPrefs.GetInt(PrefResolutionWidth, fallbackWidth);
            int savedHeight = PlayerPrefs.GetInt(PrefResolutionHeight, fallbackHeight);
            selectedResolutionIndex = FindClosestResolutionPreset(savedWidth, savedHeight);
            if (hasSavedResolution || PlayerPrefs.HasKey(PrefFullScreen))
            {
                ApplySelectedResolution();
            }

            ClampPlayerCountForMode();
        }

        private void SavePreferences()
        {
            PlayerPrefs.SetString(PrefPlayerName, playerName);
            PlayerPrefs.SetInt(PrefGameMode, (int)selectedMode);
            PlayerPrefs.SetInt(PrefMaxPlayers, createPlayerCount);
            PlayerPrefs.SetInt(PrefItemsEnabled, itemsEnabled ? 1 : 0);
            PlayerPrefs.SetFloat(PrefMasterVolume, masterVolume);
            PlayerPrefs.SetFloat(PrefAimSensitivity, aimSensitivity);
            PlayerPrefs.SetInt(PrefScreenShake, screenShakeEnabled ? 1 : 0);
            PlayerPrefs.SetInt(PrefFullScreen, fullScreenEnabled ? 1 : 0);
            Vector2Int selectedResolution = GetSelectedResolution();
            PlayerPrefs.SetInt(PrefResolutionWidth, selectedResolution.x);
            PlayerPrefs.SetInt(PrefResolutionHeight, selectedResolution.y);
            PlayerPrefs.Save();

            AudioListener.volume = masterVolume;
            ApplySelectedResolution();
        }

        private void LoadUiAssetSprites()
        {
            const string generatedButtonsPath = "GeneratedButtons/";
            Vector4 buttonBorder = new Vector4(32f, 26f, 32f, 26f);
            Vector4 inputBorder = new Vector4(32f, 26f, 32f, 26f);
            Vector4 checkboxBorder = new Vector4(12f, 12f, 12f, 12f);

            menuBackdropSprite = ResolveUiSprite(menuBackdropSprite, "WesternMenuBackdrop", Vector4.zero);
            titleBackdropSprite = LoadUiSprite("WesternMainMenuBackdropNoButtonsGenerated", Vector4.zero) ??
                ResolveUiSprite(titleBackdropSprite, "WesternMainMenuBackdropNoButtonsGenerated", Vector4.zero);
            lobbyBackdropSprite = LoadUiSprite("WesternLobbyBackgroundGenerated", Vector4.zero) ??
                ResolveUiSprite(lobbyBackdropSprite, "WesternLobbyBackgroundGenerated", Vector4.zero);
            panelSprite = ResolveUiSprite(panelSprite, "WesternPanel", new Vector4(34f, 34f, 34f, 34f));
            darkPanelSprite = ResolveUiSprite(darkPanelSprite, "WesternPanelDark", new Vector4(34f, 34f, 34f, 34f));
            headerSprite = ResolveUiSprite(headerSprite, "WesternHeader", new Vector4(34f, 34f, 34f, 34f));
            titlePlaqueSprite = ResolveUiSprite(titlePlaqueSprite, "WesternTitlePlaque", new Vector4(52f, 44f, 52f, 44f));
            dividerSprite = ResolveUiSprite(dividerSprite, "WesternDivider", Vector4.zero);
            buttonGoldSprite = ResolvePreferredUiSprite(buttonGoldSprite, generatedButtonsPath + "WesternButtonGoldGenerated", "WesternButtonGold", buttonBorder);
            buttonGoldHoverSprite = ResolveUiSprite(buttonGoldHoverSprite, generatedButtonsPath + "WesternButtonGoldHoverGenerated", buttonBorder);
            buttonGoldPressedSprite = ResolveUiSprite(buttonGoldPressedSprite, generatedButtonsPath + "WesternButtonGoldPressedGenerated", buttonBorder);
            buttonDarkSprite = ResolvePreferredUiSprite(buttonDarkSprite, generatedButtonsPath + "WesternButtonDarkGenerated", "WesternButtonDark", buttonBorder);
            buttonDarkHoverSprite = ResolveUiSprite(buttonDarkHoverSprite, generatedButtonsPath + "WesternButtonDarkHoverGenerated", buttonBorder);
            buttonDarkPressedSprite = ResolveUiSprite(buttonDarkPressedSprite, generatedButtonsPath + "WesternButtonDarkPressedGenerated", buttonBorder);
            buttonGreenSprite = ResolvePreferredUiSprite(buttonGreenSprite, generatedButtonsPath + "WesternButtonGreenGenerated", "WesternButtonGreen", buttonBorder);
            buttonGreenHoverSprite = ResolveUiSprite(buttonGreenHoverSprite, generatedButtonsPath + "WesternButtonGreenHoverGenerated", buttonBorder);
            buttonGreenPressedSprite = ResolveUiSprite(buttonGreenPressedSprite, generatedButtonsPath + "WesternButtonGreenPressedGenerated", buttonBorder);
            buttonRedSprite = ResolvePreferredUiSprite(buttonRedSprite, generatedButtonsPath + "WesternButtonRedGenerated", "WesternButtonRed", buttonBorder);
            buttonRedHoverSprite = ResolveUiSprite(buttonRedHoverSprite, generatedButtonsPath + "WesternButtonRedHoverGenerated", buttonBorder);
            buttonRedPressedSprite = ResolveUiSprite(buttonRedPressedSprite, generatedButtonsPath + "WesternButtonRedPressedGenerated", buttonBorder);
            buttonDisabledSprite = ResolveUiSprite(buttonDisabledSprite, generatedButtonsPath + "WesternButtonDisabledGenerated", buttonBorder);
            inputSprite = ResolvePreferredUiSprite(inputSprite, generatedButtonsPath + "WesternInputGenerated", "WesternInput", inputBorder);
            checkboxSprite = ResolvePreferredUiSprite(checkboxSprite, generatedButtonsPath + "WesternCheckboxGenerated", "WesternCheckbox", checkboxBorder);
            checkboxCheckSprite = ResolveUiSprite(checkboxCheckSprite, generatedButtonsPath + "WesternCheckboxCheckGenerated", Vector4.zero);
            sliderTrackSprite = ResolveUiSprite(sliderTrackSprite, "WesternSliderTrack", new Vector4(18f, 12f, 18f, 12f));
            sliderFillSprite = ResolveUiSprite(sliderFillSprite, "WesternSliderFill", new Vector4(18f, 12f, 18f, 12f));
            sliderHandleSprite = ResolveUiSprite(sliderHandleSprite, "WesternSliderHandle", new Vector4(12f, 16f, 12f, 16f));
        }

        private Sprite ResolvePreferredUiSprite(Sprite current, string preferredAssetName, string fallbackAssetName, Vector4 border)
        {
            Sprite preferred = LoadUiSprite(preferredAssetName, border);
            if (preferred != null)
            {
                return preferred;
            }

            return ResolveUiSprite(current, fallbackAssetName, border);
        }

        private Sprite ResolveUiSprite(Sprite current, string assetName, Vector4 border)
        {
            if (current == null)
            {
                return LoadUiSprite(assetName, border);
            }

            if (border == Vector4.zero || current.border != Vector4.zero || current.texture == null)
            {
                return current;
            }

            return CreateUiSprite(current.texture, border);
        }

        private Sprite LoadUiSprite(string assetName, Vector4 border)
        {
            string resourcePath = "UI/" + assetName;
            Sprite sprite = Resources.Load<Sprite>(resourcePath);
            Texture2D texture = sprite != null ? sprite.texture : Resources.Load<Texture2D>(resourcePath);

            if (texture == null)
            {
                return sprite;
            }

            if (sprite != null && (border == Vector4.zero || sprite.border != Vector4.zero))
            {
                return sprite;
            }

            return CreateUiSprite(texture, border);
        }

        private Sprite CreateUiSprite(Texture2D texture, Vector4 border)
        {
            return Sprite.Create(
                texture,
                new Rect(0f, 0f, texture.width, texture.height),
                new Vector2(0.5f, 0.5f),
                100f,
                0,
                SpriteMeshType.FullRect,
                border);
        }

        private void BuildInterface()
        {
            menuFont = ResolveFont();

            GameObject canvasObject = new GameObject("Western Main Menu Canvas", typeof(RectTransform), typeof(Canvas), typeof(CanvasScaler), typeof(GraphicRaycaster));
            canvasObject.transform.SetParent(transform, false);

            canvas = canvasObject.GetComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;

            CanvasScaler scaler = canvasObject.GetComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1920f, 1080f);
            scaler.screenMatchMode = CanvasScaler.ScreenMatchMode.MatchWidthOrHeight;
            scaler.matchWidthOrHeight = 0.5f;

            RectTransform root = canvasObject.GetComponent<RectTransform>();
            CreateBackground(root);

            titleScreen = CreateScreen(root, "Title Screen");
            nameScreen = CreateScreen(root, "Name Screen");
            lobbyScreen = CreateScreen(root, "Lobby Screen");

            BuildTitleScreen(titleScreen.GetComponent<RectTransform>());
            BuildNameScreen(nameScreen.GetComponent<RectTransform>());
            BuildLobbyScreen(lobbyScreen.GetComponent<RectTransform>());
        }

        private void CreateBackground(RectTransform root)
        {
            RectTransform background = CreateRect("Background", root);
            Stretch(background, 0f, 0f, 0f, 0f);
            backgroundImage = background.gameObject.AddComponent<Image>();
            ApplyUiSprite(backgroundImage, GetBackdropForView(MenuView.Title), backgroundColor);
            backgroundImage.raycastTarget = false;

            CreateBand(root, "Top Brass Rule", true, 16f, brassColor);
            CreateBand(root, "Bottom Brass Rule", false, 16f, brassDarkColor);
            CreateBand(root, "Upper Dust Band", true, 94f, new Color32(45, 29, 21, 210));
            CreateBand(root, "Lower Dust Band", false, 82f, new Color32(27, 18, 14, 230));

            CreateDecorSprite(root, "Cowboy Decor", cowboySprite, new Vector2(0f, 0f), new Vector2(0f, 0f), new Vector2(230f, 260f), new Vector2(360f, 520f), new Color(1f, 1f, 1f, 0.38f));
            CreateDecorSprite(root, "Scarecrow Decor", scarecrowSprite, new Vector2(1f, 0f), new Vector2(1f, 0f), new Vector2(-250f, 105f), new Vector2(300f, 450f), new Color(1f, 1f, 1f, 0.28f));
            CreateDecorSprite(root, "Paper Decor", paperSprite, new Vector2(1f, 1f), new Vector2(1f, 1f), new Vector2(-165f, -190f), new Vector2(250f, 250f), new Color(1f, 1f, 1f, 0.42f));
            CreateDecorSprite(root, "Cylinder Decor", cylinderSprite, new Vector2(0f, 1f), new Vector2(0f, 1f), new Vector2(150f, -145f), new Vector2(145f, 145f), new Color(1f, 1f, 1f, 0.5f));
        }

        private void BuildTitleScreen(RectTransform root)
        {
            if (titlePlaqueSprite != null)
            {
                RectTransform plaque = CreateRect("Title Plaque", root);
                Anchor(plaque, new Vector2(0.5f, 0.58f), new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(1040f, 260f));
                Image plaqueImage = plaque.gameObject.AddComponent<Image>();
                ApplyUiSprite(plaqueImage, titlePlaqueSprite, Color.white);
                plaqueImage.raycastTarget = false;
            }

            RectTransform stack = CreateRect("Title Stack", root);
            Anchor(stack, new Vector2(0.5f, 0.55f), new Vector2(0.5f, 0.5f), new Vector2(0f, 0f), new Vector2(980f, 540f));
            AddVertical(stack, 16, 16, 8, 8, 22f, TextAnchor.MiddleCenter);

            Text eyebrow = CreateLabel(stack, "Eyebrow", "WESTERN BOARD SHOOTOUT", 24, brassColor, TextAnchor.MiddleCenter, 42f, FontStyle.Bold);
            AddTextShadow(eyebrow, inkColor, new Vector2(2f, -2f));

            Text title = CreateLabel(stack, "Title", "황야의 오목", 84, parchmentColor, TextAnchor.MiddleCenter, 128f, FontStyle.Bold);
            AddTextOutline(title, inkColor, new Vector2(3f, -3f));

            Text subtitle = CreateLabel(stack, "Subtitle", "바람에 날리는 종이판을 맞춰 선을 완성해라", 30, parchmentMutedColor, TextAnchor.MiddleCenter, 54f, FontStyle.Bold);
            AddTextShadow(subtitle, inkColor, new Vector2(2f, -2f));

            CreateDivider(stack, "Title Divider");
            CreateSpacer(stack, 20f);

            Button startButton = CreateButton(stack, "Start Button", "ENTER 시작", 270f, 68f, brassColor, inkColor, delegate { ShowView(MenuView.Name); });
            AddTextOutline(startButton.GetComponentInChildren<Text>(), new Color32(255, 226, 158, 255), new Vector2(1f, -1f));

            Text hint = CreateLabel(stack, "Hint", "서버 연결 전 클라이언트 메뉴 프로토타입", 22, parchmentMutedColor, TextAnchor.MiddleCenter, 48f, FontStyle.Normal);
            hint.color = new Color(parchmentMutedColor.r, parchmentMutedColor.g, parchmentMutedColor.b, 0.82f);
        }

        private void BuildNameScreen(RectTransform root)
        {
            RectTransform panel = CreateFixedPanel(root, "Name Panel", new Vector2(0.5f, 0.54f), new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(760f, 420f));
            AddVertical(panel, 42, 42, 34, 34, 18f, TextAnchor.UpperCenter);

            CreateLabel(panel, "Name Title", "이름 입력", 48, parchmentColor, TextAnchor.MiddleCenter, 72f, FontStyle.Bold);
            CreateLabel(panel, "Name Copy", "로비와 방 목록에 표시될 닉네임", 22, parchmentMutedColor, TextAnchor.MiddleCenter, 38f, FontStyle.Normal);

            nameInput = CreateInputField(panel, "Player Name Input", "닉네임", playerName, 18);

            RectTransform row = CreateRow(panel, "Name Button Row", 66f, 14f);
            CreateButton(row, "Back Title Button", "뒤로", 0f, 58f, panelDeepColor, parchmentColor, delegate { ShowView(MenuView.Title); });
            CreateButton(row, "Confirm Name Button", "로비 입장", 0f, 58f, brassColor, inkColor, ConfirmPlayerName);
        }

        private void BuildLobbyScreen(RectTransform root)
        {
            RectTransform header = CreatePanel(root, "Lobby Header", panelDeepColor, brassDarkColor, headerSprite);
            SetTop(header, 0f, 0f, 0f, 118f);

            Text title = CreateText(header, "Header Title", "황야의 오목", 38, parchmentColor, TextAnchor.MiddleLeft, FontStyle.Bold);
            Stretch(title.rectTransform, 48f, 1120f, 0f, 0f);
            AddTextShadow(title, inkColor, new Vector2(2f, -2f));

            nameBadgeText = CreateText(header, "Name Badge", string.Empty, 22, parchmentColor, TextAnchor.MiddleRight, FontStyle.Bold);
            Stretch(nameBadgeText.rectTransform, 880f, 48f, 18f, 58f);

            statusText = CreateText(header, "Status", string.Empty, 20, parchmentMutedColor, TextAnchor.MiddleRight, FontStyle.Normal);
            Stretch(statusText.rectTransform, 880f, 48f, 60f, 18f);

            RectTransform nav = CreatePanel(root, "Navigation", panelColor, brassDarkColor, darkPanelSprite);
            SetLeft(nav, 42f, 154f, 56f, 310f);
            AddVertical(nav, 18, 18, 20, 20, 12f, TextAnchor.UpperCenter);

            CreateLabel(nav, "Menu Label", "LOBBY", 20, brassColor, TextAnchor.MiddleLeft, 36f, FontStyle.Bold);
            CreateButton(nav, "Hub Nav", "메인 로비", 0f, 58f, panelDeepColor, parchmentColor, delegate { ShowView(MenuView.Hub); });
            CreateButton(nav, "Create Room Nav", "방 만들기", 0f, 58f, panelDeepColor, parchmentColor, delegate { ShowView(MenuView.CreateRoom); });
            CreateButton(nav, "Find Room Nav", "방 찾기", 0f, 58f, panelDeepColor, parchmentColor, delegate { ShowView(MenuView.FindRoom); });
            CreateButton(nav, "Matchmaking Nav", "매치메이킹", 0f, 58f, panelDeepColor, parchmentColor, delegate { ShowView(MenuView.Matchmaking); });
            CreateButton(nav, "Settings Nav", "설정", 0f, 58f, panelDeepColor, parchmentColor, delegate { ShowView(MenuView.Settings); });
            CreateButton(nav, "Tutorial Nav", "튜토리얼", 0f, 58f, panelDeepColor, parchmentColor, StartTutorialScene);
            CreateButton(nav, "Server Ready Button", "준비 완료", 0f, 54f, greenAccentColor, inkColor, SendReadyToServer);
            CreateButton(nav, "Server Start Button", "서버 게임 시작", 0f, 54f, brassColor, inkColor, StartServerRoom);
            CreateButton(nav, "Server Leave Button", "방 나가기", 0f, 54f, panelDeepColor, parchmentColor, LeaveServerRoom);
            CreateFlexibleSpacer(nav);
            CreateButton(nav, "Practice Button", "봇과 연습", 0f, 62f, redAccentColor, Color.white, StartBotPractice);

            RectTransform contentRoot = CreateRect("Content Root", root);
            Stretch(contentRoot, 386f, 54f, 154f, 56f);

            hubPanel = CreateContentPanel(contentRoot, "Hub Panel").gameObject;
            createRoomPanel = CreateContentPanel(contentRoot, "Create Room Panel").gameObject;
            roomPanel = CreateContentPanel(contentRoot, "Room Panel").gameObject;
            findRoomPanel = CreateContentPanel(contentRoot, "Find Room Panel").gameObject;
            matchmakingPanel = CreateContentPanel(contentRoot, "Matchmaking Panel").gameObject;
            settingsPanel = CreateContentPanel(contentRoot, "Settings Panel").gameObject;

            BuildHubPanel(hubPanel.GetComponent<RectTransform>());
            BuildCreateRoomPanel(createRoomPanel.GetComponent<RectTransform>());
            BuildRoomPanel(roomPanel.GetComponent<RectTransform>());
            BuildFindRoomPanel(findRoomPanel.GetComponent<RectTransform>());
            BuildMatchmakingPanel(matchmakingPanel.GetComponent<RectTransform>());
            BuildSettingsPanel(settingsPanel.GetComponent<RectTransform>());
        }

        private void BuildHubPanel(RectTransform panel)
        {
            CreateLabel(panel, "Hub Title", "메인 로비", 48, parchmentColor, TextAnchor.MiddleLeft, 70f, FontStyle.Bold);
            CreateLabel(panel, "Hub Copy", "방을 만들거나 코드를 입력해서 합류하고, 서버가 붙기 전에는 로컬 테스트로 바로 인게임 흐름을 확인할 수 있어.", 24, parchmentMutedColor, TextAnchor.MiddleLeft, 72f, FontStyle.Normal);
            CreateDivider(panel, "Hub Divider");

            RectTransform actionRow = CreateRow(panel, "Hub Action Row", 180f, 18f);
            CreateLargeAction(actionRow, "Create Room Big", "방 만들기", "규칙을 정하고 방 코드를 만든다", brassColor, inkColor, delegate { ShowView(MenuView.CreateRoom); });
            CreateLargeAction(actionRow, "Find Room Big", "방 찾기", "친구가 준 코드를 입력한다", greenAccentColor, inkColor, delegate { ShowView(MenuView.FindRoom); });
            CreateLargeAction(actionRow, "Matchmaking Big", "매치메이킹", "같은 모드의 플레이어를 찾는다", redAccentColor, Color.white, delegate { ShowView(MenuView.Matchmaking); });

            CreateSpacer(panel, 18f);

            RectTransform rulePanel = CreateSection(panel, "Rule Snapshot", 250f);
            CreateLabel(rulePanel, "Rules Title", "현재 핵심 규칙", 28, brassColor, TextAnchor.MiddleLeft, 42f, FontStyle.Bold);
            CreateLabel(rulePanel, "Rules Text", "오목은 최대 4명, 한 턴 6발. 틱택토는 최대 2명, 한 턴 1발. 모두 발사하면 큰 보드판으로 탄착 위치를 확인하고 다음 판으로 넘어간다.", 23, parchmentColor, TextAnchor.UpperLeft, 98f, FontStyle.Normal);
            serverRoomText = CreateLabel(rulePanel, "Server Text", "서버 방 없음", 21, parchmentMutedColor, TextAnchor.UpperLeft, 70f, FontStyle.Normal);
        }

        /// <summary>
        /// The room screen: who is in which slot, and the rules for the match.
        ///
        /// Laid out as capacity slots rather than a member list, so an empty
        /// place reads as somewhere a bot can go rather than as absence. The
        /// settings below mirror what the server actually validates - rounds,
        /// ammo, wave cap, paper band and the item set - instead of the two
        /// invented toggles the create screen used to offer.
        /// </summary>
        private void BuildRoomPanel(RectTransform panel)
        {
            roomHeaderText = CreateLabel(panel, "Room Header", "방", 40, parchmentColor,
                TextAnchor.MiddleLeft, 56f, FontStyle.Bold);
            CreateDivider(panel, "Room Divider");

            RectTransform slotSection = CreateSection(panel, "Slot Section", 232f);
            CreateLabel(slotSection, "Slot Title", "플레이어 슬롯", 26, brassColor,
                TextAnchor.MiddleLeft, 36f, FontStyle.Bold);
            slotListRoot = CreateSection(slotSection, "Slot List", 190f);

            CreateSpacer(panel, 10f);
            RectTransform ruleSection = CreateSection(panel, "Room Rule Section", 340f);
            CreateLabel(ruleSection, "Room Rule Title", "세부 룰 (방장만 변경)", 26, brassColor,
                TextAnchor.MiddleLeft, 36f, FontStyle.Bold);

            BuildConfigRow(ruleSection, "라운드", roundSegments, new[] { 3, 5, 7 },
                delegate(int v) { draftConfig.Rounds = (byte)v; });
            // Zero is the server's "unlimited" sentinel for both of these.
            BuildConfigRow(ruleSection, "탄약", ammoSegments, new[] { 1, 2, 3, 6, 0 },
                delegate(int v) { draftConfig.AmmoPerWave = (byte)v; });
            BuildConfigRow(ruleSection, "웨이브 상한", waveSegments, new[] { 10, 20, 0 },
                delegate(int v) { draftConfig.WaveLimit = (byte)v; });
            BuildConfigRow(ruleSection, "종이 최소", paperMinSegments, new[] { 1, 2, 3, 4, 5 },
                delegate(int v) { draftConfig.PaperSizeMin = (byte)v; });
            BuildConfigRow(ruleSection, "종이 최대", paperMaxSegments, new[] { 1, 2, 3, 4, 5 },
                delegate(int v) { draftConfig.PaperSizeMax = (byte)v; });

            CreateLabel(ruleSection, "Item Title", "아이템", 22, brassColor, TextAnchor.MiddleLeft,
                30f, FontStyle.Bold);
            itemToggleRow = CreateRow(ruleSection, "Item Row", 44f, 8f);
            foreach (ServerItemKind kind in AllItemKinds)
            {
                ServerItemKind captured = kind;
                itemToggles[kind] = CreateToggle(itemToggleRow, ItemName(kind), true,
                    delegate(bool on) { SetDraftItem(captured, on); });
            }

            RectTransform actionRow = CreateRow(panel, "Room Action Row", 60f, 12f);
            applyConfigButton = CreateButton(actionRow, "Apply Config", "룰 적용", 0f, 54f,
                brassColor, inkColor, delegate { ApplyDraftConfig(); });
            readyButton = CreateButton(actionRow, "Ready Button", "준비", 0f, 54f, greenAccentColor,
                inkColor, delegate { ToggleReady(); });
            startButton = CreateButton(actionRow, "Start Button", "게임 시작", 0f, 54f,
                redAccentColor, Color.white, StartServerRoom);
            CreateButton(actionRow, "Room Leave", "나가기", 0f, 54f, panelDeepColor, parchmentColor,
                LeaveServerRoom);
        }

        private static readonly ServerItemKind[] AllItemKinds =
        {
            ServerItemKind.Tumbleweed, ServerItemKind.WantedPoster, ServerItemKind.FryingPan,
            ServerItemKind.Balloon, ServerItemKind.PaintCan, ServerItemKind.Grenade,
            ServerItemKind.SpeedLoader
        };

        private static string ItemName(ServerItemKind kind)
        {
            switch (kind)
            {
                case ServerItemKind.Tumbleweed: return "회전초";
                case ServerItemKind.WantedPoster: return "현상수배지";
                case ServerItemKind.FryingPan: return "프라이팬";
                case ServerItemKind.Balloon: return "풍선";
                case ServerItemKind.PaintCan: return "물감통";
                case ServerItemKind.Grenade: return "수류탄";
                default: return "스피드로더";
            }
        }

        private void BuildConfigRow(RectTransform parent, string label, List<SegmentOption<int>> into,
            int[] values, UnityEngine.Events.UnityAction<int> onPick)
        {
            RectTransform row = CreateRow(parent, label + " Row", 46f, 8f);
            CreateLabel(row, label + " Label", label, 21, parchmentMutedColor, TextAnchor.MiddleLeft,
                42f, FontStyle.Normal);
            foreach (int value in values)
            {
                int captured = value;
                // Zero means unlimited on the wire; showing the number would
                // read as "no ammo at all".
                string text = captured == 0 ? "무한" : captured.ToString();
                into.Add(CreateSegment(row, text, captured, delegate
                {
                    onPick(captured);
                    RefreshRoomPanel();
                }));
            }
        }

        private void SetDraftItem(ServerItemKind kind, bool enabled)
        {
            if (suppressConfigEcho)
            {
                return;
            }

            uint bit = 1u << (int)kind;
            draftConfig.ItemMask = enabled ? draftConfig.ItemMask | bit : draftConfig.ItemMask & ~bit;
        }

        private void ApplyDraftConfig()
        {
            if (matchClient == null || !matchClient.CurrentRoom.IsValid)
            {
                return;
            }

            // Sent as-is. The server clamps anything this mode cannot run and
            // echoes the result back, which is what the panel then shows.
            matchClient.SetRoomConfig(draftConfig);
        }

        private void ToggleReady()
        {
            if (matchClient == null || !matchClient.CurrentRoom.IsValid)
            {
                return;
            }

            bool ready = false;
            foreach (ServerRoomMember member in matchClient.CurrentRoom.Members)
            {
                if (member.SessionId == matchClient.SessionId)
                {
                    ready = member.IsReady;
                    break;
                }
            }

            matchClient.SetReady(!ready);
        }

        private void AddBotToRoom(byte difficulty)
        {
            if (matchClient == null || !matchClient.CurrentRoom.IsValid)
            {
                SetStatus("방에 들어간 뒤에 봇을 추가할 수 있습니다");
                return;
            }

            matchClient.AddBot(difficulty);
        }

        private void NudgeBotDifficulty(ulong botSessionId, int delta)
        {
            ServerRoomDetail room = matchClient != null ? matchClient.CurrentRoom : default(ServerRoomDetail);
            if (room.Members == null)
            {
                return;
            }

            foreach (ServerRoomMember member in room.Members)
            {
                if (member.SessionId != botSessionId)
                {
                    continue;
                }

                int next = Mathf.Clamp(member.BotDifficulty + delta,
                    BotLimits.MinDifficulty, BotLimits.MaxDifficulty);
                if (next != member.BotDifficulty)
                {
                    matchClient.SetBotDifficulty(botSessionId, (byte)next);
                }

                return;
            }
        }

        /// <summary>
        /// Redraws the slot list and re-selects the settings that are in force.
        ///
        /// Rebuilt wholesale rather than patched: a room holds at most four
        /// slots, and reconciling a list that small in place would be more code
        /// than it saves.
        /// </summary>
        private void RefreshRoomPanel()
        {
            if (slotListRoot == null || matchClient == null)
            {
                return;
            }

            ServerRoomDetail room = matchClient.CurrentRoom;
            bool isHost = room.IsValid && room.HostSessionId == matchClient.SessionId;
            bool waiting = room.State == ServerRoomState.Waiting;

            if (roomHeaderText != null)
            {
                roomHeaderText.text = room.IsValid
                    ? string.Format("{0}  ·  코드 {1}  ·  {2}", ModeRules.For(room.Mode).DisplayName,
                        room.Name, room.State)
                    : "방 없음";
            }

            for (int i = slotListRoot.childCount - 1; i >= 0; i--)
            {
                Destroy(slotListRoot.GetChild(i).gameObject);
            }

            if (!room.IsValid)
            {
                return;
            }

            List<ServerRoomMember> members = room.Members ?? new List<ServerRoomMember>();
            for (int slot = 0; slot < room.Capacity; slot++)
            {
                bool filled = false;
                foreach (ServerRoomMember member in members)
                {
                    if (member.Slot != slot)
                    {
                        continue;
                    }

                    filled = true;
                    BuildFilledSlotRow(slot, member, isHost, waiting);
                    break;
                }

                if (!filled)
                {
                    BuildEmptySlotRow(slot, isHost, waiting);
                }
            }

            RefreshSegmentList(roundSegments, (int)draftConfig.Rounds);
            RefreshSegmentList(ammoSegments, (int)draftConfig.AmmoPerWave);
            RefreshSegmentList(waveSegments, (int)draftConfig.WaveLimit);
            RefreshSegmentList(paperMinSegments, (int)draftConfig.PaperSizeMin);
            RefreshSegmentList(paperMaxSegments, (int)draftConfig.PaperSizeMax);

            foreach (KeyValuePair<ServerItemKind, Toggle> entry in itemToggles)
            {
                entry.Value.interactable = isHost && waiting;
            }

            if (applyConfigButton != null) applyConfigButton.interactable = isHost && waiting;
            if (startButton != null) startButton.interactable = isHost && waiting;
            if (readyButton != null) readyButton.interactable = waiting;
        }

        private void BuildFilledSlotRow(int slot, ServerRoomMember member, bool isHost, bool waiting)
        {
            RectTransform row = CreateRow(slotListRoot, "Slot " + slot, 44f, 8f);
            string tag = member.IsHost ? " [방장]" : member.IsReady ? " [준비]" : string.Empty;
            CreateLabel(row, "Slot Name", string.Format("{0}P  {1}{2}", slot + 1, member.Nickname, tag),
                21, member.IsBot ? brassColor : parchmentColor, TextAnchor.MiddleLeft, 40f,
                FontStyle.Bold);

            if (!member.IsBot || !isHost || !waiting)
            {
                return;
            }

            ulong botId = member.SessionId;
            CreateButton(row, "Bot Down", "◀", 46f, 40f, brassDarkColor, parchmentColor,
                delegate { NudgeBotDifficulty(botId, -1); });
            CreateLabel(row, "Bot Level", "난이도 " + member.BotDifficulty, 20, brassColor,
                TextAnchor.MiddleCenter, 40f, FontStyle.Bold);
            CreateButton(row, "Bot Up", "▶", 46f, 40f, brassDarkColor, parchmentColor,
                delegate { NudgeBotDifficulty(botId, 1); });
            CreateButton(row, "Bot Remove", "비우기", 76f, 40f, redAccentColor, Color.white,
                delegate { matchClient.RemoveBot(botId); });
        }

        private void BuildEmptySlotRow(int slot, bool isHost, bool waiting)
        {
            RectTransform row = CreateRow(slotListRoot, "Slot " + slot, 44f, 8f);
            CreateLabel(row, "Slot Empty", string.Format("{0}P  비어 있음", slot + 1), 21,
                parchmentMutedColor, TextAnchor.MiddleLeft, 40f, FontStyle.Normal);

            if (!isHost || !waiting)
            {
                return;
            }

            Button add = CreateButton(row, "Slot Add Bot", "봇 추가", 110f, 40f, greenAccentColor,
                inkColor, delegate { AddBotToRoom(BotLimits.DefaultDifficulty); });
            add.name = "Slot Add Bot " + slot;
        }

        private void BuildCreateRoomPanel(RectTransform panel)
        {
            CreateLabel(panel, "Create Title", "방 만들기", 46, parchmentColor, TextAnchor.MiddleLeft, 68f, FontStyle.Bold);
            CreateLabel(panel, "Create Copy", "게임 모드와 룰 옵션을 정한 뒤 방 코드를 생성한다.", 23, parchmentMutedColor, TextAnchor.MiddleLeft, 44f, FontStyle.Normal);
            CreateDivider(panel, "Create Divider");

            RectTransform modeSection = CreateSection(panel, "Mode Section", 126f);
            CreateLabel(modeSection, "Mode Label", "게임 모드", 22, brassColor, TextAnchor.MiddleLeft, 32f, FontStyle.Bold);
            RectTransform modeRow = CreateRow(modeSection, "Mode Row", 58f, 12f);
            foreach (ServerGameMode option in ModeRules.Playable)
            {
                ServerGameMode captured = option;
                createModeSegments.Add(CreateSegment(modeRow, ModeRules.For(captured).DisplayName, captured,
                    delegate { SetCreateMode(captured); }));
            }

            RectTransform playerSection = CreateSection(panel, "Player Count Section", 124f);
            CreateLabel(playerSection, "Player Count Label", "플레이어 수", 22, brassColor, TextAnchor.MiddleLeft, 32f, FontStyle.Bold);
            RectTransform playerRow = CreateRow(playerSection, "Player Count Row", 58f, 10f);
            for (int i = 1; i <= 4; i++)
            {
                int count = i;
                playerCountSegments.Add(CreateSegment(playerRow, count + "P", count, delegate { SetCreatePlayerCount(count); }));
            }

            RectTransform optionSection = CreateSection(panel, "Options Section", 192f);
            CreateLabel(optionSection, "Options Label", "방 옵션", 22, brassColor, TextAnchor.MiddleLeft, 32f, FontStyle.Bold);
            itemsToggle = CreateToggle(optionSection, "아이템 사용", itemsEnabled, delegate(bool value) { itemsEnabled = value; UpdateCreateSummary(); });

            RectTransform codeSection = CreateSection(panel, "Room Code Section", 168f);
            roomCodeText = CreateLabel(codeSection, "Room Code Text", "ROOM CODE  " + generatedRoomCode, 36, parchmentColor, TextAnchor.MiddleLeft, 58f, FontStyle.Bold);
            createSummaryText = CreateLabel(codeSection, "Create Summary", string.Empty, 20, parchmentMutedColor, TextAnchor.MiddleLeft, 42f, FontStyle.Normal);
            RectTransform codeButtonRow = CreateRow(codeSection, "Room Code Buttons", 54f, 12f);
            CreateButton(codeButtonRow, "Generate Room Button", "방 코드 생성", 0f, 52f, brassColor, inkColor, CreateRoomCode);
            CreateButton(codeButtonRow, "Create Practice Button", "이 설정으로 봇 연습", 0f, 52f, redAccentColor, Color.white, StartBotPractice);

            RefreshSegments();
            UpdateCreateSummary();
        }

        private void BuildFindRoomPanel(RectTransform panel)
        {
            CreateLabel(panel, "Find Title", "방 찾기", 46, parchmentColor, TextAnchor.MiddleLeft, 68f, FontStyle.Bold);
            CreateLabel(panel, "Find Copy", "코드를 정확히 입력해 바로 들어가거나, 이름 일부로 검색해서 목록에서 고른다.", 23, parchmentMutedColor, TextAnchor.MiddleLeft, 44f, FontStyle.Normal);
            CreateDivider(panel, "Find Divider");

            RectTransform codePanel = CreateSection(panel, "Find Code Section", 190f);
            CreateLabel(codePanel, "Code Label", "초대 코드로 바로 입장", 22, brassColor, TextAnchor.MiddleLeft, 34f, FontStyle.Bold);
            roomCodeInput = CreateInputField(codePanel, "Room Code Input", "예: DUST42", string.Empty, 10);
            RectTransform buttonRow = CreateRow(codePanel, "Find Button Row", 58f, 12f);
            CreateButton(buttonRow, "Check Code Button", "코드 확인", 0f, 54f, brassColor, inkColor, CheckRoomCode);
            CreateButton(buttonRow, "Join Mock Button", "방 입장", 0f, 54f, greenAccentColor, inkColor, JoinRoomMock);

            CreateSpacer(panel, 10f);
            RectTransform searchPanel = CreateSection(panel, "Find Search Section", 300f);
            CreateLabel(searchPanel, "Search Label", "이름으로 검색", 22, brassColor, TextAnchor.MiddleLeft, 34f, FontStyle.Bold);
            roomSearchInput = CreateInputField(searchPanel, "Room Search Input", "이름 일부 (비우면 전체)", string.Empty, 24);
            RectTransform searchRow = CreateRow(searchPanel, "Search Row", 54f, 12f);
            CreateButton(searchRow, "Search Button", "검색", 0f, 50f, brassColor, inkColor, SearchRoomsByName);
            hideFullToggle = CreateToggle(searchRow, "가득 찬 방 숨기기", true, delegate { SearchRoomsByName(); });
            hideLockedToggle = CreateToggle(searchRow, "잠긴 방 숨기기", false, delegate { SearchRoomsByName(); });

            // One row per result, each with its own join button.
            roomResultRoot = CreateSection(searchPanel, "Room Results", 176f);

            findStatusText = CreateLabel(panel, "Find Status", "검색을 눌러 방 목록을 불러온다.", 24, parchmentMutedColor, TextAnchor.UpperLeft, 60f, FontStyle.Normal);
        }

        private void SearchRoomsByName()
        {
            string filter = roomSearchInput != null ? roomSearchInput.text.Trim() : string.Empty;
            searchingByName = true;

            RunWhenConnected(delegate
            {
                findStatusText.text = string.IsNullOrEmpty(filter)
                    ? "전체 방 검색 중"
                    : "'" + filter + "' 검색 중";
                matchClient.RequestRoomList(
                    ServerGameMode.None,
                    filter,
                    hideFullToggle != null && hideFullToggle.isOn,
                    hideLockedToggle != null && hideLockedToggle.isOn,
                    0,
                    20);
            });
        }

        /// <summary>
        /// Draws one row per search hit, each with its own join button.
        ///
        /// Separate from the code path on purpose: a code is an exact address
        /// and joins immediately, whereas a name is a guess that can match
        /// several rooms and has to be chosen from.
        /// </summary>
        private void ShowRoomResults(List<ServerRoomSummary> rooms)
        {
            if (roomResultRoot == null)
            {
                return;
            }

            for (int i = roomResultRoot.childCount - 1; i >= 0; i--)
            {
                Destroy(roomResultRoot.GetChild(i).gameObject);
            }

            if (rooms == null || rooms.Count == 0)
            {
                findStatusText.text = "검색 결과 없음";
                return;
            }

            int shown = Mathf.Min(rooms.Count, 4);
            findStatusText.text = string.Format("{0}개 방 발견 (상위 {1}개 표시)", rooms.Count, shown);

            for (int i = 0; i < shown; i++)
            {
                ServerRoomSummary room = rooms[i];
                ulong roomId = room.RoomId;
                bool locked = room.HasPassword;
                bool full = room.MemberCount >= room.Capacity;

                RectTransform row = CreateRow(roomResultRoot, "Result " + roomId, 40f, 8f);
                CreateLabel(row, "Result Name", string.Format("{0}  {1}  {2}/{3}{4}",
                        room.Name,
                        ModeRules.For(room.Mode).DisplayName,
                        room.MemberCount,
                        room.Capacity,
                        locked ? "  [비밀]" : string.Empty),
                    20, parchmentColor, TextAnchor.MiddleLeft, 36f, FontStyle.Normal);

                Button join = CreateButton(row, "Result Join", full ? "가득 참" : "입장", 96f, 36f,
                    full ? panelDeepColor : greenAccentColor, full ? parchmentMutedColor : inkColor,
                    delegate { JoinSearchResult(roomId, locked); });
                join.interactable = !full;
            }
        }

        private void JoinSearchResult(ulong roomId, bool locked)
        {
            // A locked room still needs its password, and the only field we have
            // for one is the code box.
            string password = locked && roomCodeInput != null ? roomCodeInput.text.Trim() : string.Empty;
            if (locked && string.IsNullOrEmpty(password))
            {
                findStatusText.text = "비밀방입니다. 위 코드 칸에 비밀번호를 넣고 다시 눌러주세요.";
                return;
            }

            RunWhenConnected(delegate { matchClient.JoinRoom(roomId, password); });
        }

        private void BuildMatchmakingPanel(RectTransform panel)
        {
            CreateLabel(panel, "Matchmaking Title", "매치메이킹", 46, parchmentColor, TextAnchor.MiddleLeft, 68f, FontStyle.Bold);
            CreateLabel(panel, "Matchmaking Copy", "선택한 모드로 큐에 등록한다. 지금은 서버 연결 전 대기 상태를 목업으로 보여준다.", 23, parchmentMutedColor, TextAnchor.MiddleLeft, 54f, FontStyle.Normal);
            CreateDivider(panel, "Matchmaking Divider");

            RectTransform modeSection = CreateSection(panel, "Match Mode Section", 126f);
            CreateLabel(modeSection, "Match Mode Label", "매칭 모드", 22, brassColor, TextAnchor.MiddleLeft, 32f, FontStyle.Bold);
            RectTransform modeRow = CreateRow(modeSection, "Match Mode Row", 58f, 12f);
            foreach (ServerGameMode option in ModeRules.Playable)
            {
                ServerGameMode captured = option;
                matchmakingModeSegments.Add(CreateSegment(modeRow, ModeRules.For(captured).DisplayName, captured,
                    delegate { SetMatchmakingMode(captured); }));
            }

            RectTransform statusSection = CreateSection(panel, "Match Status Section", 260f);
            matchmakingStatusText = CreateLabel(statusSection, "Match Status Text", "큐 대기 전", 32, parchmentColor, TextAnchor.MiddleCenter, 116f, FontStyle.Bold);
            Button matchmakingButton = CreateButton(statusSection, "Matchmaking Button", "매칭 시작", 0f, 62f, redAccentColor, Color.white, ToggleMatchmaking);
            matchmakingButtonText = matchmakingButton.GetComponentInChildren<Text>();
            CreateLabel(statusSection, "Matchmaking Hint", "매칭 성공 이벤트는 서버 개발자가 콜백으로 이어주면 된다.", 20, parchmentMutedColor, TextAnchor.MiddleCenter, 44f, FontStyle.Normal);

            CreateSpacer(panel, 18f);
            CreateButton(panel, "Match Practice Button", "봇과 연습", 0f, 58f, brassColor, inkColor, StartBotPractice);

            RefreshSegments();
        }

        private void BuildSettingsPanel(RectTransform panel)
        {
            CreateLabel(panel, "Settings Title", "설정", 46, parchmentColor, TextAnchor.MiddleLeft, 68f, FontStyle.Bold);
            CreateLabel(panel, "Settings Copy", "클라이언트에서 바로 필요한 조작감과 화면 옵션을 저장한다.", 23, parchmentMutedColor, TextAnchor.MiddleLeft, 44f, FontStyle.Normal);
            CreateDivider(panel, "Settings Divider");

            RectTransform audioSection = CreateSection(panel, "Audio Section", 156f);
            CreateLabel(audioSection, "Audio Label", "소리와 조작", 22, brassColor, TextAnchor.MiddleLeft, 32f, FontStyle.Bold);
            volumeSlider = CreateSliderControl(audioSection, "마스터 볼륨", 0f, 1f, masterVolume, delegate(float value) { masterVolume = value; });
            sensitivitySlider = CreateSliderControl(audioSection, "조준 감도", 0.5f, 2f, aimSensitivity, delegate(float value) { aimSensitivity = value; });

            RectTransform displaySection = CreateSection(panel, "Display Section", 260f);
            CreateLabel(displaySection, "Display Label", "화면", 22, brassColor, TextAnchor.MiddleLeft, 32f, FontStyle.Bold);
            CreateLabel(displaySection, "Resolution Label", "해상도", 19, parchmentColor, TextAnchor.MiddleLeft, 30f, FontStyle.Bold);
            RectTransform resolutionRow = CreateRow(displaySection, "Resolution Row", 54f, 10f);
            for (int index = 0; index < ResolutionPresets.Length; index++)
            {
                int capturedIndex = index;
                resolutionSegments.Add(CreateSegment(resolutionRow, GetResolutionLabel(index), index, delegate { SetResolutionIndex(capturedIndex); }));
            }

            fullScreenToggle = CreateToggle(displaySection, "전체 화면", fullScreenEnabled, delegate(bool value) { fullScreenEnabled = value; });
            screenShakeToggle = CreateToggle(displaySection, "피격 흔들림", screenShakeEnabled, delegate(bool value) { screenShakeEnabled = value; });

            CreateSpacer(panel, 16f);
            RectTransform buttonRow = CreateRow(panel, "Settings Buttons", 62f, 12f);
            CreateButton(buttonRow, "Save Settings Button", "설정 저장", 0f, 58f, brassColor, inkColor, delegate
            {
                SavePreferences();
                SetStatus("설정을 저장했다.");
            });
            CreateButton(buttonRow, "Reset Settings Button", "기본값", 0f, 58f, panelDeepColor, parchmentColor, ResetSettingsToDefault);
        }

        private void ShowView(MenuView view)
        {
            currentView = view;
            bool isLobby = view != MenuView.Title && view != MenuView.Name;

            titleScreen.SetActive(view == MenuView.Title);
            nameScreen.SetActive(view == MenuView.Name);
            lobbyScreen.SetActive(isLobby);

            hubPanel.SetActive(view == MenuView.Hub);
            createRoomPanel.SetActive(view == MenuView.CreateRoom);
            roomPanel.SetActive(view == MenuView.Room);
            findRoomPanel.SetActive(view == MenuView.FindRoom);
            matchmakingPanel.SetActive(view == MenuView.Matchmaking);
            settingsPanel.SetActive(view == MenuView.Settings);

            if (view == MenuView.Name && nameInput != null)
            {
                nameInput.text = playerName;
                nameInput.ActivateInputField();
            }

            if (isLobby)
            {
                UpdateBackdrop(view);
                UpdateHeader();
                if (view == MenuView.Hub)
                {
                    SetStatus("로비 대기 중");
                }
            }
            else
            {
                UpdateBackdrop(view);
            }
        }

        private void ConfirmPlayerName()
        {
            string value = nameInput != null ? nameInput.text.Trim() : string.Empty;
            if (string.IsNullOrEmpty(value))
            {
                value = defaultPlayerName;
            }

            playerName = value;
            SavePreferences();
            ShowView(MenuView.Hub);
            SetStatus(playerName + " 입장 완료");
            ConnectToMatchServer();
        }

        private void SetCreateMode(ServerGameMode mode)
        {
            selectedMode = mode;
            ClampPlayerCountForMode();
            RefreshSegments();
            UpdateCreateSummary();
            UpdateHeader();
        }

        private void SetMatchmakingMode(ServerGameMode mode)
        {
            matchmakingMode = mode;
            RefreshSegments();
            matchmakingStatusText.text = queuedForMatchmaking ? "큐 갱신 중" : GetModeName(matchmakingMode) + " 큐 대기 전";
        }

        private void SetCreatePlayerCount(int count)
        {
            createPlayerCount = count;
            ClampPlayerCountForMode();
            RefreshSegments();
            UpdateCreateSummary();
            UpdateHeader();
        }

        private void ClampPlayerCountForMode()
        {
            int max = ModeRules.For(selectedMode).Capacity;
            createPlayerCount = Mathf.Clamp(createPlayerCount, 1, max);
        }

        private void RefreshSegments()
        {
            RefreshSegmentList(createModeSegments, selectedMode);
            RefreshSegmentList(matchmakingModeSegments, matchmakingMode);
            RefreshSegmentList(resolutionSegments, selectedResolutionIndex);

            int allowedMax = ModeRules.For(selectedMode).Capacity;
            foreach (SegmentOption<int> option in playerCountSegments)
            {
                bool allowed = option.Value <= allowedMax;
                bool selected = allowed && option.Value == createPlayerCount;
                SetSegmentVisual(option, selected, allowed);
            }
        }

        private void RefreshSegmentList<T>(List<SegmentOption<T>> options, T selectedValue)
        {
            foreach (SegmentOption<T> option in options)
            {
                bool selected = EqualityComparer<T>.Default.Equals(option.Value, selectedValue);
                SetSegmentVisual(option, selected, true);
            }
        }

        private void SetSegmentVisual<T>(SegmentOption<T> option, bool selected, bool allowed)
        {
            option.Button.interactable = allowed;
            ApplyButtonSkin(option.Button, option.Background, selected ? brassColor : panelDeepColor, !allowed);
            option.Label.color = selected ? inkColor : allowed ? parchmentColor : parchmentMutedColor;
        }

        private void SetResolutionIndex(int index)
        {
            selectedResolutionIndex = Mathf.Clamp(index, 0, ResolutionPresets.Length - 1);
            RefreshSegments();
        }

        private void UpdateCreateSummary()
        {
            if (createSummaryText == null)
            {
                return;
            }

            // Re-shooting and overline are not offered: the server decides both
            // and neither is configurable, so the summary states the board
            // instead of options that do not exist.
            ModeRules rules = ModeRules.For(selectedMode);
            string item = itemsEnabled ? "아이템 ON" : "아이템 OFF";
            createSummaryText.text = string.Format("{0} | {1}x{1} {2}목 | {3}P | {4}",
                rules.DisplayName, rules.BoardSize, rules.WinLength, createPlayerCount, item);

            if (roomCodeText != null)
            {
                roomCodeText.text = "ROOM CODE  " + generatedRoomCode;
            }
        }

        private void CreateRoomCode()
        {
            generatedRoomCode = GenerateRoomCode();
            SavePreferences();
            UpdateCreateSummary();
            SetStatus("서버 방 생성 요청: " + generatedRoomCode);

            RunWhenConnected(delegate
            {
                ServerGameMode mode = selectedMode;
                if (matchClient.CreateRoom(generatedRoomCode, mode, string.Empty))
                {
                    matchClient.SetRoomConfig(ServerMatchConfig.CreateDefault(mode, itemsEnabled));
                }
            });
        }

        private string GenerateRoomCode()
        {
            const string alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
            StringBuilder builder = new StringBuilder(6);
            for (int i = 0; i < 6; i++)
            {
                int index = UnityEngine.Random.Range(0, alphabet.Length);
                builder.Append(alphabet[index]);
            }

            return builder.ToString();
        }

        private void CheckRoomCode()
        {
            string code = NormalizeRoomCode(roomCodeInput != null ? roomCodeInput.text : string.Empty);
            if (string.IsNullOrEmpty(code))
            {
                findStatusText.text = "코드를 입력해야 방을 찾을 수 있다.";
                SetStatus("방 코드 없음");
                return;
            }

            pendingJoinCode = null;
            findStatusText.text = "코드 " + code + " 조회 중";
            SetStatus("방 코드 확인: " + code);
            RunWhenConnected(delegate
            {
                matchClient.RequestRoomList(ServerGameMode.None, code, true, false, 0, 20);
            });
        }

        private void JoinRoomMock()
        {
            string code = NormalizeRoomCode(roomCodeInput != null ? roomCodeInput.text : string.Empty);
            if (string.IsNullOrEmpty(code))
            {
                findStatusText.text = "입장할 방 코드를 먼저 입력해줘.";
                SetStatus("입장 실패");
                return;
            }

            SavePreferences();
            pendingJoinCode = code;
            findStatusText.text = "코드 " + code + " 입장할 방 검색 중";
            SetStatus("방 입장 요청: " + code);
            RunWhenConnected(delegate
            {
                matchClient.RequestRoomList(ServerGameMode.None, code, true, false, 0, 20);
            });
        }

        private string NormalizeRoomCode(string raw)
        {
            if (string.IsNullOrEmpty(raw))
            {
                return string.Empty;
            }

            StringBuilder builder = new StringBuilder(raw.Length);
            for (int i = 0; i < raw.Length; i++)
            {
                char c = raw[i];
                if (char.IsLetterOrDigit(c))
                {
                    builder.Append(char.ToUpperInvariant(c));
                }
            }

            return builder.ToString();
        }

        private void ToggleMatchmaking()
        {
            queuedForMatchmaking = !queuedForMatchmaking;
            if (queuedForMatchmaking)
            {
                matchmakingStartTime = Time.unscaledTime;
                matchmakingButtonText.text = "매칭 취소";
                SetStatus(GetModeName(matchmakingMode) + " 매칭 대기 시작");
                RunWhenConnected(delegate
                {
                    matchClient.QuickMatch(matchmakingMode);
                });
            }
            else
            {
                matchmakingButtonText.text = "매칭 시작";
                matchmakingStatusText.text = "큐 취소됨";
                SetStatus("매칭 취소");
                if (matchClient != null && matchClient.IsAuthenticated && matchClient.CurrentRoom.IsValid)
                {
                    matchClient.LeaveRoom();
                }
            }
        }

        private void UpdateMatchmakingStatus()
        {
            if (!queuedForMatchmaking || matchmakingStatusText == null)
            {
                return;
            }

            float elapsed = Time.unscaledTime - matchmakingStartTime;
            int dotCount = Mathf.FloorToInt(elapsed * 2f) % 4;
            string dots = new string('.', dotCount);
            matchmakingStatusText.text = string.Format("{0} 큐 검색 중{1}\n대기 {2:0}s", GetModeName(matchmakingMode), dots, elapsed);
        }

        /// <summary>
        /// Practice against bots, through the real server.
        ///
        /// This replaces the offline hot-seat prototype as the way in. That
        /// prototype takes turns - one mouse, one player at a time - which is
        /// not the game: the server runs every player simultaneously through a
        /// wave. Practising against a rule set nobody plays by taught the wrong
        /// habits, and now that bots exist there is no reason to.
        /// </summary>
        private void StartBotPractice()
        {
            SavePreferences();

            RunWhenConnected(delegate
            {
                if (matchClient.CurrentRoom.IsValid)
                {
                    SetStatus("이미 방에 있습니다. 슬롯에서 봇을 추가하세요.");
                    ShowView(MenuView.Room);
                    return;
                }

                generatedRoomCode = GenerateRoomCode();
                SetStatus("연습 방 생성 중: " + generatedRoomCode);
                pendingBotFill = ModeRules.For(selectedMode).MinimumToStart - 1;
                matchClient.CreateRoom(generatedRoomCode, selectedMode, string.Empty);
            });
        }

        /// Tops the new practice room up to the minimum the mode needs, so the
        /// host can press start immediately.
        private void FillPracticeBots()
        {
            if (pendingBotFill <= 0)
            {
                return;
            }

            int wanted = pendingBotFill;
            pendingBotFill = 0;
            for (int i = 0; i < wanted; i++)
            {
                matchClient.AddBot(BotLimits.DefaultDifficulty);
            }

            SetStatus("봇 " + wanted + "명 추가됨. 난이도를 조정하고 시작하세요.");
        }

        private void StartTutorialScene()
        {
            SavePreferences();
            SceneManager.LoadScene(tutorialSceneName);
        }

        private void ResetSettingsToDefault()
        {
            masterVolume = 0.9f;
            aimSensitivity = 1f;
            fullScreenEnabled = Screen.fullScreen;
            screenShakeEnabled = true;
            selectedResolutionIndex = DefaultResolutionIndex;
            ApplySettingsValuesToUi();
            SavePreferences();
            SetStatus("설정을 기본값으로 되돌렸다.");
        }

        private void ApplySettingsValuesToUi()
        {
            if (itemsToggle != null)
            {
                itemsToggle.isOn = itemsEnabled;
            }

            if (fullScreenToggle != null)
            {
                fullScreenToggle.isOn = fullScreenEnabled;
            }

            if (screenShakeToggle != null)
            {
                screenShakeToggle.isOn = screenShakeEnabled;
            }

            if (volumeSlider != null)
            {
                volumeSlider.value = masterVolume;
            }

            if (sensitivitySlider != null)
            {
                sensitivitySlider.value = aimSensitivity;
            }

            RefreshSegments();
        }

        private Vector2Int GetSelectedResolution()
        {
            int index = Mathf.Clamp(selectedResolutionIndex, 0, ResolutionPresets.Length - 1);
            return ResolutionPresets[index];
        }

        private static string GetResolutionLabel(int index)
        {
            Vector2Int resolution = ResolutionPresets[Mathf.Clamp(index, 0, ResolutionPresets.Length - 1)];
            return resolution.x + " x " + resolution.y;
        }

        private static int FindClosestResolutionPreset(int width, int height)
        {
            int bestIndex = DefaultResolutionIndex;
            int bestScore = int.MaxValue;
            for (int index = 0; index < ResolutionPresets.Length; index++)
            {
                Vector2Int preset = ResolutionPresets[index];
                int score = Mathf.Abs(preset.x - width) + Mathf.Abs(preset.y - height);
                if (score < bestScore)
                {
                    bestIndex = index;
                    bestScore = score;
                }
            }

            return bestIndex;
        }

        private void ApplySelectedResolution()
        {
            Vector2Int resolution = GetSelectedResolution();
            if (resolution.x <= 0 || resolution.y <= 0)
            {
                return;
            }

            Screen.SetResolution(resolution.x, resolution.y, fullScreenEnabled);
        }

        private void EnsureMatchClient()
        {
            if (matchClient != null)
            {
                return;
            }

            matchClient = GetComponent<MatchServerClient>();
            if (matchClient == null)
            {
                matchClient = gameObject.AddComponent<MatchServerClient>();
            }

            matchClient.StatusChanged += OnMatchStatusChanged;
            matchClient.HelloAck += OnMatchHelloAck;
            matchClient.RoomChanged += OnMatchRoomChanged;
            matchClient.RoomConfigChanged += OnMatchConfigChanged;
            matchClient.RoomListReceived += OnMatchRoomListReceived;
            matchClient.GameStarting += OnMatchGameStarting;
        }

        private void ConnectToMatchServer()
        {
            EnsureMatchClient();
            if (matchClient.IsAuthenticated)
            {
                UpdateServerRoomText();
                return;
            }

            matchClient.ConnectAndHello(matchServerHost, matchServerPort, playerName);
        }

        private void RunWhenConnected(Action action)
        {
            EnsureMatchClient();
            if (matchClient.IsAuthenticated)
            {
                if (action != null)
                {
                    action();
                }

                return;
            }

            pendingServerAction = action;
            ConnectToMatchServer();
        }

        private void OnMatchStatusChanged(string message)
        {
            SetStatus(message);
        }

        private void OnMatchHelloAck(ServerResultCode result, ulong sessionId, string nickname)
        {
            if (result != ServerResultCode.Ok)
            {
                pendingServerAction = null;
                SetStatus("서버 인증 실패: " + ServerProtocolText.ToDisplay(result));
                UpdateServerRoomText();
                return;
            }

            playerName = string.IsNullOrEmpty(nickname) ? playerName : nickname;
            UpdateHeader();
            UpdateServerRoomText();

            Action action = pendingServerAction;
            pendingServerAction = null;
            if (action != null)
            {
                action();
            }
        }

        private void OnMatchRoomChanged(ServerRoomDetail room)
        {
            if (queuedForMatchmaking && room.IsValid)
            {
                queuedForMatchmaking = false;
                if (matchmakingButtonText != null)
                {
                    matchmakingButtonText.text = "매칭 시작";
                }

                if (matchmakingStatusText != null)
                {
                    matchmakingStatusText.text = "매칭 완료: " + room.Name;
                }
            }

            UpdateServerRoomText();

            // The room is a place, so it gets a screen. Entering one moves you
            // there; being ejected - kicked, closed, or having left - moves you
            // back rather than leaving a dead panel on screen.
            if (room.IsValid && currentView != MenuView.Room)
            {
                draftConfig = matchClient.CurrentConfig;
                ShowView(MenuView.Room);
                FillPracticeBots();
            }
            else if (!room.IsValid && currentView == MenuView.Room)
            {
                ShowView(MenuView.Hub);
            }
        }

        private void OnMatchConfigChanged(ServerMatchConfig config, uint effectiveItems)
        {
            // Adopt whatever the server settled on. It clamps illegal
            // combinations, so echoing its answer back into the panel is the
            // only way the controls can show what is really in force.
            draftConfig = config;

            foreach (KeyValuePair<ServerItemKind, Toggle> entry in itemToggles)
            {
                uint bit = 1u << (int)entry.Key;
                bool on = (config.ItemMask & bit) != 0;
                if (entry.Value.isOn != on)
                {
                    // Setting isOn fires the change callback, which would write
                    // straight back into the draft we are loading.
                    suppressConfigEcho = true;
                    entry.Value.isOn = on;
                    suppressConfigEcho = false;
                }
            }

            RefreshRoomPanel();
        }

        private void OnMatchRoomListReceived(List<ServerRoomSummary> rooms, uint totalCount, ushort page)
        {
            if (!string.IsNullOrEmpty(pendingJoinCode))
            {
                ServerRoomSummary target = default(ServerRoomSummary);
                bool found = false;
                for (int i = 0; i < rooms.Count; i++)
                {
                    if (string.Equals(rooms[i].Name, pendingJoinCode, StringComparison.OrdinalIgnoreCase))
                    {
                        target = rooms[i];
                        found = true;
                        break;
                    }
                }

                if (!found && rooms.Count == 1)
                {
                    target = rooms[0];
                    found = true;
                }

                string code = pendingJoinCode;
                pendingJoinCode = null;
                if (found)
                {
                    if (findStatusText != null)
                    {
                        findStatusText.text = "코드 " + code + " 방 발견, 입장 요청 중";
                    }

                    matchClient.JoinRoom(target.RoomId, string.Empty);
                    return;
                }

                if (findStatusText != null)
                {
                    findStatusText.text = "코드 " + code + " 방을 찾지 못했다.";
                }

                SetStatus("방 검색 실패");
                return;
            }

            if (findStatusText == null)
            {
                return;
            }

            if (searchingByName)
            {
                // A name search offers choices, so it gets rows with join
                // buttons rather than a paragraph of text.
                searchingByName = false;
                ShowRoomResults(rooms);
                return;
            }

            if (rooms.Count == 0)
            {
                findStatusText.text = "검색 결과 없음";
                return;
            }

            StringBuilder builder = new StringBuilder();
            builder.AppendFormat("{0}개 방 발견\n", totalCount);
            int count = Mathf.Min(rooms.Count, 4);
            for (int i = 0; i < count; i++)
            {
                builder.Append(ServerProtocolText.ToRoomLine(rooms[i]));
                if (i < count - 1)
                {
                    builder.Append('\n');
                }
            }

            findStatusText.text = builder.ToString();
        }

        private void OnMatchGameStarting(ServerGameStartingInfo info)
        {
            SetStatus("인스턴스 접속 중: " + info.Host + ":" + info.Port);

            // Connect here, before the scene change, rather than storing the
            // ticket for the game scene to use. The ticket is single-use and
            // expires in 30 seconds, and the session object carries the open
            // connection across the load.
            NetworkGameSession.Begin(info, playerName, matchClient != null ? matchClient.SessionId : 0UL);

            SceneManager.LoadScene(inGameSceneName);
        }

        private void SendReadyToServer()
        {
            RunWhenConnected(delegate
            {
                matchClient.SetReady(true);
            });
        }

        private void StartServerRoom()
        {
            RunWhenConnected(delegate
            {
                matchClient.StartRoom();
            });
        }

        private void LeaveServerRoom()
        {
            pendingJoinCode = null;
            pendingServerAction = null;
            queuedForMatchmaking = false;

            if (matchmakingButtonText != null)
            {
                matchmakingButtonText.text = "매칭 시작";
            }

            if (matchClient != null && matchClient.IsAuthenticated)
            {
                matchClient.LeaveRoom();
            }
            else
            {
                SetStatus("나갈 서버 방이 없다.");
            }
        }

        private void UpdateServerRoomText()
        {
            if (serverRoomText == null)
            {
                return;
            }

            if (matchClient == null || !matchClient.IsAuthenticated)
            {
                serverRoomText.text = "서버 연결 전\n" +
                    "주소: " + matchServerHost + ":" + matchServerPort + "\n" +
                    "방 만들기, 방 찾기, 매치메이킹 버튼을 누르면 자동 연결을 시도합니다.";
                return;
            }

            ServerRoomDetail room = matchClient.CurrentRoom;
            if (!room.IsValid)
            {
                serverRoomText.text = "서버 연결됨\n" +
                    "현재 방 없음\n" +
                    "방 코드 생성, 코드 검색, 빠른 매칭 중 하나를 선택하세요.";
                return;
            }

            int memberCount = room.Members != null ? room.Members.Count : 0;
            StringBuilder builder = new StringBuilder();
            builder.AppendFormat("서버 방 #{0}  코드 {1}  {2}/{3}\n", room.RoomId, room.Name, memberCount, room.Capacity);
            if (room.Members != null)
            {
                for (int i = 0; i < room.Members.Count; i++)
                {
                    ServerRoomMember member = room.Members[i];
                    builder.AppendFormat("{0}P  {1}  {2}{3}{4}",
                        member.Slot + 1,
                        member.Nickname,
                        member.IsBot ? " [BOT " + member.BotDifficulty + "]" : string.Empty,
                        member.IsHost ? " [HOST]" : string.Empty,
                        member.IsReady ? " [READY]" : string.Empty);
                    if (i < room.Members.Count - 1)
                    {
                        builder.Append('\n');
                    }
                }
            }

            serverRoomText.text = builder.ToString();
            RefreshRoomPanel();
        }

        private void UpdateHeader()
        {
            if (nameBadgeText != null)
            {
                nameBadgeText.text = playerName + "  |  " + GetModeName(selectedMode) + " " + createPlayerCount + "P";
            }
        }

        private void SetStatus(string value)
        {
            if (statusText != null)
            {
                statusText.text = value;
            }
        }

        private void UpdateBackdrop(MenuView view)
        {
            if (backgroundImage == null)
            {
                return;
            }

            ApplyUiSprite(backgroundImage, GetBackdropForView(view), backgroundColor);
        }

        private Sprite GetBackdropForView(MenuView view)
        {
            if (view == MenuView.Title || view == MenuView.Name)
            {
                return titleBackdropSprite != null ? titleBackdropSprite : menuBackdropSprite;
            }

            return lobbyBackdropSprite != null ? lobbyBackdropSprite : menuBackdropSprite;
        }

        private string GetModeName(ServerGameMode mode)
        {
            return ModeRules.For(mode).DisplayName;
        }

        private bool WasSubmitPressed()
        {
#if ENABLE_INPUT_SYSTEM
            if (Keyboard.current != null &&
                (Keyboard.current.enterKey.wasPressedThisFrame || Keyboard.current.numpadEnterKey.wasPressedThisFrame))
            {
                return true;
            }
#endif

#if ENABLE_LEGACY_INPUT_MANAGER
            return Input.GetKeyDown(KeyCode.Return) || Input.GetKeyDown(KeyCode.KeypadEnter);
#else
            return false;
#endif
        }

        private void EnsureEventSystem()
        {
            if (EventSystem.current != null)
            {
                return;
            }

            GameObject eventSystemObject = new GameObject("EventSystem");
            eventSystemObject.AddComponent<EventSystem>();

#if ENABLE_INPUT_SYSTEM
            InputSystemUIInputModule inputModule = eventSystemObject.AddComponent<InputSystemUIInputModule>();
            inputModule.AssignDefaultActions();
#else
            eventSystemObject.AddComponent<StandaloneInputModule>();
#endif
        }

        private GameObject CreateScreen(RectTransform parent, string name)
        {
            RectTransform screen = CreateRect(name, parent);
            Stretch(screen, 0f, 0f, 0f, 0f);
            return screen.gameObject;
        }

        private RectTransform CreateContentPanel(RectTransform parent, string name)
        {
            RectTransform panel = CreatePanel(parent, name, panelColor, brassDarkColor, panelSprite);
            Stretch(panel, 0f, 0f, 0f, 0f);
            AddVertical(panel, 34, 34, 28, 28, 12f, TextAnchor.UpperLeft);
            return panel;
        }

        private RectTransform CreateFixedPanel(RectTransform parent, string name, Vector2 anchor, Vector2 pivot, Vector2 position, Vector2 size)
        {
            RectTransform panel = CreatePanel(parent, name, panelColor, brassDarkColor, panelSprite);
            Anchor(panel, anchor, pivot, position, size);
            Shadow shadow = panel.gameObject.AddComponent<Shadow>();
            shadow.effectColor = new Color(0f, 0f, 0f, 0.45f);
            shadow.effectDistance = new Vector2(8f, -8f);
            return panel;
        }

        private RectTransform CreateSection(RectTransform parent, string name, float height)
        {
            RectTransform section = CreatePanel(parent, name, panelDeepColor, brassDarkColor, darkPanelSprite);
            SetLayout(section, height, 1f, 0f);
            AddVertical(section, 22, 22, 14, 14, 8f, TextAnchor.UpperLeft);
            return section;
        }

        private RectTransform CreatePanel(RectTransform parent, string name, Color color, Color outline, Sprite sprite = null)
        {
            RectTransform rect = CreateRect(name, parent);
            Image image = rect.gameObject.AddComponent<Image>();
            ApplyUiSprite(image, sprite, color);
            image.raycastTarget = false;

            Outline uiOutline = rect.gameObject.AddComponent<Outline>();
            uiOutline.effectColor = outline;
            uiOutline.effectDistance = new Vector2(2f, -2f);
            return rect;
        }

        private RectTransform CreateRow(RectTransform parent, string name, float height, float spacing)
        {
            RectTransform row = CreateRect(name, parent);
            SetLayout(row, height, 1f, 0f);
            AddHorizontal(row, 0, 0, 0, 0, spacing, TextAnchor.MiddleCenter);
            return row;
        }

        private Button CreateLargeAction(RectTransform parent, string name, string title, string subtitle, Color background, Color textColor, UnityEngine.Events.UnityAction onClick)
        {
            Button button = CreateButton(parent, name, title + "\n" + subtitle, 0f, 170f, background, textColor, onClick);
            Text label = button.GetComponentInChildren<Text>();
            label.fontSize = 24;
            label.lineSpacing = 1.1f;
            return button;
        }

        private Button CreateButton(RectTransform parent, string name, string label, float preferredWidth, float preferredHeight, Color background, Color textColor, UnityEngine.Events.UnityAction onClick)
        {
            RectTransform rect = CreateRect(name, parent);
            SetLayout(rect, preferredHeight, preferredWidth <= 0f ? 1f : 0f, 0f);
            if (preferredWidth > 0f)
            {
                LayoutElement layout = rect.GetComponent<LayoutElement>();
                layout.preferredWidth = preferredWidth;
            }

            Image image = rect.gameObject.AddComponent<Image>();
            image.raycastTarget = true;

            Button button = rect.gameObject.AddComponent<Button>();
            button.targetGraphic = image;
            ApplyButtonSkin(button, image, background, false);
            button.onClick.AddListener(onClick);

            Outline outline = rect.gameObject.AddComponent<Outline>();
            outline.effectColor = inkColor;
            outline.effectDistance = new Vector2(2f, -2f);

            Text text = CreateText(rect, "Label", label, 22, textColor, TextAnchor.MiddleCenter, FontStyle.Bold);
            Stretch(text.rectTransform, 12f, 12f, 6f, 6f);
            return button;
        }

        private SegmentOption<T> CreateSegment<T>(RectTransform parent, string label, T value, UnityEngine.Events.UnityAction onClick)
        {
            Button button = CreateButton(parent, "Segment " + label, label, 0f, 54f, panelDeepColor, parchmentColor, onClick);
            return new SegmentOption<T>
            {
                Button = button,
                Background = button.GetComponent<Image>(),
                Label = button.GetComponentInChildren<Text>(),
                Value = value
            };
        }

        private Toggle CreateToggle(RectTransform parent, string label, bool initialValue, UnityEngine.Events.UnityAction<bool> onChanged)
        {
            RectTransform row = CreateRect("Toggle " + label, parent);
            SetLayout(row, 38f, 1f, 0f);
            AddHorizontal(row, 0, 0, 0, 0, 10f, TextAnchor.MiddleLeft);

            Image hitArea = row.gameObject.AddComponent<Image>();
            hitArea.color = new Color(0f, 0f, 0f, 0f);
            hitArea.raycastTarget = true;

            Toggle toggle = row.gameObject.AddComponent<Toggle>();

            RectTransform box = CreateRect("Box", row);
            box.sizeDelta = new Vector2(28f, 28f);
            SetLayout(box, 28f, 0f, 0f).preferredWidth = 28f;
            Image boxImage = box.gameObject.AddComponent<Image>();
            ApplyUiSprite(boxImage, checkboxSprite, panelColor);
            boxImage.raycastTarget = true;
            Outline boxOutline = box.gameObject.AddComponent<Outline>();
            boxOutline.effectColor = brassDarkColor;
            boxOutline.effectDistance = new Vector2(2f, -2f);

            RectTransform check = CreateRect("Check", box);
            Stretch(check, 5f, 5f, 5f, 5f);
            Image checkImage = check.gameObject.AddComponent<Image>();
            ApplyUiSprite(checkImage, checkboxCheckSprite, brassColor);
            checkImage.raycastTarget = false;

            Text text = CreateText(row, "Label", label, 20, parchmentColor, TextAnchor.MiddleLeft, FontStyle.Bold);
            SetLayout(text.rectTransform, 36f, 1f, 0f);

            toggle.targetGraphic = boxImage;
            toggle.graphic = checkImage;
            toggle.isOn = initialValue;
            toggle.onValueChanged.AddListener(onChanged);
            return toggle;
        }

        private InputField CreateInputField(RectTransform parent, string name, string placeholder, string value, int characterLimit)
        {
            RectTransform rect = CreatePanel(parent, name, new Color32(26, 19, 16, 245), brassDarkColor, inputSprite);
            SetLayout(rect, 62f, 1f, 0f);
            rect.gameObject.GetComponent<Image>().raycastTarget = true;

            InputField input = rect.gameObject.AddComponent<InputField>();
            input.characterLimit = characterLimit;
            input.caretColor = brassColor;
            input.selectionColor = new Color(brassColor.r, brassColor.g, brassColor.b, 0.35f);

            Text text = CreateText(rect, "Text", value, 25, parchmentColor, TextAnchor.MiddleLeft, FontStyle.Bold);
            Stretch(text.rectTransform, 20f, 20f, 6f, 6f);
            text.supportRichText = false;

            Text placeholderText = CreateText(rect, "Placeholder", placeholder, 25, new Color(parchmentMutedColor.r, parchmentMutedColor.g, parchmentMutedColor.b, 0.55f), TextAnchor.MiddleLeft, FontStyle.Normal);
            Stretch(placeholderText.rectTransform, 20f, 20f, 6f, 6f);

            input.textComponent = text;
            input.placeholder = placeholderText;
            input.text = value;
            return input;
        }

        private Slider CreateSliderControl(RectTransform parent, string label, float minValue, float maxValue, float value, UnityEngine.Events.UnityAction<float> onChanged)
        {
            RectTransform row = CreateRow(parent, "Slider " + label, 42f, 14f);

            Text labelText = CreateText(row, "Label", label, 19, parchmentColor, TextAnchor.MiddleLeft, FontStyle.Bold);
            SetLayout(labelText.rectTransform, 38f, 0f, 0f).preferredWidth = 160f;

            RectTransform sliderRoot = CreateRect("Slider Root", row);
            SetLayout(sliderRoot, 38f, 1f, 0f);

            Slider slider = sliderRoot.gameObject.AddComponent<Slider>();
            slider.minValue = minValue;
            slider.maxValue = maxValue;

            RectTransform background = CreateRect("Background", sliderRoot);
            Stretch(background, 0f, 0f, 13f, 13f);
            Image backgroundImage = background.gameObject.AddComponent<Image>();
            ApplyUiSprite(backgroundImage, sliderTrackSprite, panelColor);
            backgroundImage.raycastTarget = true;

            RectTransform fillArea = CreateRect("Fill Area", sliderRoot);
            Stretch(fillArea, 0f, 0f, 13f, 13f);

            RectTransform fill = CreateRect("Fill", fillArea);
            Stretch(fill, 0f, 0f, 0f, 0f);
            Image fillImage = fill.gameObject.AddComponent<Image>();
            ApplyUiSprite(fillImage, sliderFillSprite, brassColor);
            fillImage.raycastTarget = false;

            RectTransform handleArea = CreateRect("Handle Slide Area", sliderRoot);
            Stretch(handleArea, 0f, 0f, 4f, 4f);

            RectTransform handle = CreateRect("Handle", handleArea);
            Anchor(handle, new Vector2(0.5f, 0.5f), new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(24f, 30f));
            Image handleImage = handle.gameObject.AddComponent<Image>();
            ApplyUiSprite(handleImage, sliderHandleSprite, parchmentColor);
            handleImage.raycastTarget = true;

            Text valueText = CreateText(row, "Value", string.Empty, 18, parchmentMutedColor, TextAnchor.MiddleRight, FontStyle.Bold);
            SetLayout(valueText.rectTransform, 38f, 0f, 0f).preferredWidth = 62f;

            slider.fillRect = fill;
            slider.handleRect = handle;
            slider.targetGraphic = handleImage;
            slider.onValueChanged.AddListener(delegate(float sliderValue)
            {
                valueText.text = sliderValue.ToString("0.0");
                onChanged(sliderValue);
            });
            slider.value = value;
            valueText.text = value.ToString("0.0");
            return slider;
        }

        private Text CreateLabel(RectTransform parent, string name, string value, int fontSize, Color color, TextAnchor alignment, float height, FontStyle style)
        {
            Text text = CreateText(parent, name, value, fontSize, color, alignment, style);
            SetLayout(text.rectTransform, height, 1f, 0f);
            return text;
        }

        private Text CreateText(RectTransform parent, string name, string value, int fontSize, Color color, TextAnchor alignment, FontStyle style)
        {
            RectTransform rect = CreateRect(name, parent);
            Text text = rect.gameObject.AddComponent<Text>();
            text.font = menuFont;
            text.text = value;
            text.fontSize = fontSize;
            text.color = color;
            text.alignment = alignment;
            text.fontStyle = style;
            text.horizontalOverflow = HorizontalWrapMode.Wrap;
            text.verticalOverflow = VerticalWrapMode.Truncate;
            text.raycastTarget = false;
            return text;
        }

        private RectTransform CreateRect(string name, Transform parent)
        {
            GameObject gameObject = new GameObject(name, typeof(RectTransform));
            RectTransform rect = gameObject.GetComponent<RectTransform>();
            rect.SetParent(parent, false);
            rect.anchorMin = new Vector2(0.5f, 0.5f);
            rect.anchorMax = new Vector2(0.5f, 0.5f);
            rect.pivot = new Vector2(0.5f, 0.5f);
            rect.anchoredPosition = Vector2.zero;
            rect.sizeDelta = Vector2.zero;
            return rect;
        }

        private LayoutElement SetLayout(RectTransform rect, float preferredHeight, float flexibleWidth, float flexibleHeight)
        {
            LayoutElement layout = rect.gameObject.GetComponent<LayoutElement>();
            if (layout == null)
            {
                layout = rect.gameObject.AddComponent<LayoutElement>();
            }

            if (preferredHeight >= 0f)
            {
                layout.minHeight = preferredHeight;
                layout.preferredHeight = preferredHeight;
            }

            layout.flexibleWidth = flexibleWidth;
            layout.flexibleHeight = flexibleHeight;
            return layout;
        }

        private void CreateSpacer(RectTransform parent, float height)
        {
            RectTransform spacer = CreateRect("Spacer", parent);
            SetLayout(spacer, height, 1f, 0f);
        }

        private void CreateDivider(RectTransform parent, string name)
        {
            RectTransform divider = CreateRect(name, parent);
            SetLayout(divider, 24f, 1f, 0f);

            Image image = divider.gameObject.AddComponent<Image>();
            ApplyUiSprite(image, dividerSprite, brassDarkColor);
            image.raycastTarget = false;
            image.preserveAspect = true;
        }

        private void CreateFlexibleSpacer(RectTransform parent)
        {
            RectTransform spacer = CreateRect("Flexible Spacer", parent);
            SetLayout(spacer, 0f, 1f, 1f);
        }

        private void CreateBand(RectTransform parent, string name, bool top, float height, Color color)
        {
            RectTransform band = CreateRect(name, parent);
            if (top)
            {
                SetTop(band, 0f, 0f, 0f, height);
            }
            else
            {
                SetBottom(band, 0f, 0f, 0f, height);
            }

            Image image = band.gameObject.AddComponent<Image>();
            image.color = color;
            image.raycastTarget = false;
        }

        private void CreateDecorSprite(RectTransform parent, string name, Sprite sprite, Vector2 anchor, Vector2 pivot, Vector2 position, Vector2 size, Color color)
        {
            if (sprite == null)
            {
                return;
            }

            RectTransform rect = CreateRect(name, parent);
            Anchor(rect, anchor, pivot, position, size);
            Image image = rect.gameObject.AddComponent<Image>();
            image.sprite = sprite;
            image.color = color;
            image.preserveAspect = true;
            image.raycastTarget = false;
        }

        private void AddVertical(RectTransform rect, int left, int right, int top, int bottom, float spacing, TextAnchor alignment)
        {
            VerticalLayoutGroup group = rect.gameObject.AddComponent<VerticalLayoutGroup>();
            group.padding = new RectOffset(left, right, top, bottom);
            group.spacing = spacing;
            group.childAlignment = alignment;
            group.childControlWidth = true;
            group.childControlHeight = true;
            group.childForceExpandWidth = true;
            group.childForceExpandHeight = false;
        }

        private void AddHorizontal(RectTransform rect, int left, int right, int top, int bottom, float spacing, TextAnchor alignment)
        {
            HorizontalLayoutGroup group = rect.gameObject.AddComponent<HorizontalLayoutGroup>();
            group.padding = new RectOffset(left, right, top, bottom);
            group.spacing = spacing;
            group.childAlignment = alignment;
            group.childControlWidth = true;
            group.childControlHeight = true;
            group.childForceExpandWidth = false;
            group.childForceExpandHeight = true;
        }

        private void AddTextShadow(Text text, Color color, Vector2 distance)
        {
            if (text == null)
            {
                return;
            }

            Shadow shadow = text.gameObject.AddComponent<Shadow>();
            shadow.effectColor = color;
            shadow.effectDistance = distance;
        }

        private void AddTextOutline(Text text, Color color, Vector2 distance)
        {
            if (text == null)
            {
                return;
            }

            Outline outline = text.gameObject.AddComponent<Outline>();
            outline.effectColor = color;
            outline.effectDistance = distance;
        }

        private void ApplyUiSprite(Image image, Sprite sprite, Color fallbackColor)
        {
            if (sprite == null)
            {
                image.sprite = null;
                image.type = Image.Type.Simple;
                image.color = fallbackColor;
                return;
            }

            image.sprite = sprite;
            image.type = sprite.border == Vector4.zero ? Image.Type.Simple : Image.Type.Sliced;
            image.color = Color.white;
        }

        private Sprite SelectButtonSprite(Color background)
        {
            if (IsSimilarColor(background, brassColor))
            {
                return buttonGoldSprite;
            }

            if (IsSimilarColor(background, redAccentColor))
            {
                return buttonRedSprite;
            }

            if (IsSimilarColor(background, greenAccentColor))
            {
                return buttonGreenSprite;
            }

            return buttonDarkSprite;
        }

        private void ApplyButtonSkin(Button button, Image image, Color background, bool disabledVisual)
        {
            Sprite buttonSprite = disabledVisual && buttonDisabledSprite != null ? buttonDisabledSprite : SelectButtonSprite(background);
            ApplyUiSprite(image, buttonSprite, disabledVisual ? disabledColor : background);

            if (buttonSprite == null)
            {
                button.transition = Selectable.Transition.ColorTint;
                SetButtonColors(button, disabledVisual ? disabledColor : background);
                return;
            }

            button.transition = Selectable.Transition.SpriteSwap;
            if (disabledVisual)
            {
                SpriteState disabledState = button.spriteState;
                disabledState.highlightedSprite = buttonSprite;
                disabledState.pressedSprite = buttonSprite;
                disabledState.selectedSprite = buttonSprite;
                disabledState.disabledSprite = buttonSprite;
                button.spriteState = disabledState;
            }
            else
            {
                SetButtonSpriteState(button, SelectButtonHoverSprite(background), SelectButtonPressedSprite(background));
            }

            SetButtonColors(button, Color.white);
        }

        private Sprite SelectButtonHoverSprite(Color background)
        {
            if (IsSimilarColor(background, brassColor))
            {
                return buttonGoldHoverSprite;
            }

            if (IsSimilarColor(background, redAccentColor))
            {
                return buttonRedHoverSprite;
            }

            if (IsSimilarColor(background, greenAccentColor))
            {
                return buttonGreenHoverSprite;
            }

            return buttonDarkHoverSprite;
        }

        private Sprite SelectButtonPressedSprite(Color background)
        {
            if (IsSimilarColor(background, brassColor))
            {
                return buttonGoldPressedSprite;
            }

            if (IsSimilarColor(background, redAccentColor))
            {
                return buttonRedPressedSprite;
            }

            if (IsSimilarColor(background, greenAccentColor))
            {
                return buttonGreenPressedSprite;
            }

            return buttonDarkPressedSprite;
        }

        private bool IsSimilarColor(Color left, Color right)
        {
            const float tolerance = 0.03f;
            return Mathf.Abs(left.r - right.r) < tolerance &&
                Mathf.Abs(left.g - right.g) < tolerance &&
                Mathf.Abs(left.b - right.b) < tolerance;
        }

        private void SetButtonColors(Button button, Color normal)
        {
            ColorBlock colors = button.colors;
            colors.normalColor = normal;
            colors.highlightedColor = Color.Lerp(normal, Color.white, 0.18f);
            colors.pressedColor = Color.Lerp(normal, Color.black, 0.2f);
            colors.selectedColor = Color.Lerp(normal, Color.white, 0.12f);
            colors.disabledColor = disabledColor;
            colors.colorMultiplier = 1f;
            colors.fadeDuration = 0.08f;
            button.colors = colors;
        }

        private void SetButtonSpriteState(Button button, Sprite highlighted, Sprite pressed)
        {
            SpriteState state = button.spriteState;
            state.highlightedSprite = highlighted != null ? highlighted : button.targetGraphic.GetComponent<Image>().sprite;
            state.pressedSprite = pressed != null ? pressed : state.highlightedSprite;
            state.selectedSprite = state.highlightedSprite;
            state.disabledSprite = buttonDisabledSprite != null ? buttonDisabledSprite : state.pressedSprite;
            button.spriteState = state;
        }

        private void Stretch(RectTransform rect, float left, float right, float top, float bottom)
        {
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.pivot = new Vector2(0.5f, 0.5f);
            rect.offsetMin = new Vector2(left, bottom);
            rect.offsetMax = new Vector2(-right, -top);
        }

        private void SetTop(RectTransform rect, float left, float right, float top, float height)
        {
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(1f, 1f);
            rect.pivot = new Vector2(0.5f, 1f);
            rect.offsetMin = new Vector2(left, -top - height);
            rect.offsetMax = new Vector2(-right, -top);
        }

        private void SetBottom(RectTransform rect, float left, float right, float bottom, float height)
        {
            rect.anchorMin = new Vector2(0f, 0f);
            rect.anchorMax = new Vector2(1f, 0f);
            rect.pivot = new Vector2(0.5f, 0f);
            rect.offsetMin = new Vector2(left, bottom);
            rect.offsetMax = new Vector2(-right, bottom + height);
        }

        private void SetLeft(RectTransform rect, float left, float top, float bottom, float width)
        {
            rect.anchorMin = new Vector2(0f, 0f);
            rect.anchorMax = new Vector2(0f, 1f);
            rect.pivot = new Vector2(0f, 0.5f);
            rect.offsetMin = new Vector2(left, bottom);
            rect.offsetMax = new Vector2(left + width, -top);
        }

        private void Anchor(RectTransform rect, Vector2 anchor, Vector2 pivot, Vector2 position, Vector2 size)
        {
            rect.anchorMin = anchor;
            rect.anchorMax = anchor;
            rect.pivot = pivot;
            rect.anchoredPosition = position;
            rect.sizeDelta = size;
        }

        private Font ResolveFont()
        {
            string[] candidates = { "Malgun Gothic", "맑은 고딕", "Arial" };
            for (int i = 0; i < candidates.Length; i++)
            {
                Font font = Font.CreateDynamicFontFromOSFont(candidates[i], 32);
                if (font != null)
                {
                    return font;
                }
            }

            return Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        }
    }
}

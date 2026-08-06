using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using NHN.InGame;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

public static class InGamePrototypeSceneBuilder
{
    private const string ScenePath = "Assets/Scenes/InGamePrototype.unity";
    private const string PaperPath = "Assets/Sprite/GomokuPaper.png";
    private const string PaperIdlePath = "Assets/Sprite/Generated/PaperBoardBlankIdleGenerated.png";
    private const string PaperFlyingPath = "Assets/Sprite/Generated/PaperBoardBlankFlyingGenerated.png";
    private const string PaperAttachedPath = "Assets/Sprite/Generated/PaperBoardBlankAttachedGenerated.png";
    private const string PaperFlightFramePathPrefix = "Assets/Sprite/Generated/PaperFlightFrames/PaperBoardFlyingFrame";
    private const string PaperPromptIdleFramePathPrefix = "Assets/Sprite/Generated/PaperPromptAnimations/Idle/PaperIdleFrame";
    private const string PaperPromptFlyingFramePathPrefix = "Assets/Sprite/Generated/PaperPromptAnimations/Flying/PaperFlyingFrame";
    private const string PaperPromptUnfoldFramePathPrefix = "Assets/Sprite/Generated/PaperPromptAnimations/Unfold/PaperUnfoldFrame";
    private const string BulletHolePath = "Assets/Sprite/Generated/BulletHoleGenerated.png";
    private const string CrosshairPath = "Assets/Sprite/CrossHairRed.png";
    private const string CowboyPath = "Assets/Sprite/CowBoy.png";
    private const string CylinderPath = "Assets/Sprite/Silinder_front_south.png";
    private const string ScarecrowPath = "Assets/Sprite/Generated/ScarecrowCarrierGenerated.png";
    private const string ScarecrowDownPath = "Assets/Sprite/Generated/ScarecrowDownGenerated.png";
    private const string ScarecrowIdleFramePathPrefix = "Assets/Sprite/Generated/ScarecrowPromptAnimations/Idle/ScarecrowIdleFrame";
    private const string ScarecrowAttackedFramePathPrefix = "Assets/Sprite/Generated/ScarecrowPromptAnimations/Attacked/ScarecrowAttackedFrame";
    private const string ScarecrowDeathFramePathPrefix = "Assets/Sprite/Generated/ScarecrowPromptAnimations/Death/ScarecrowDeathFrame";
    private const string FryingPanPath = "Assets/Sprite/Generated/FryingPanGenerated.png";
    private const string OutlawBanditPath = "Assets/Sprite/Generated/OutlawBanditGenerated.png";
    private const string TripleShotPath = "Assets/Sprite/Generated/TripleShotPowerupGenerated.png";
    private const string CamelCarrierPath = "Assets/Sprite/Generated/CamelCarrierGenerated.png";
    private const string HudPanelPath = "Assets/Sprite/Generated/PlayerHudPanelGenerated.png";
    private const string GameplayBackgroundPath = "Assets/Resources/UI/WesternGameplayBackgroundGenerated.png";

    [MenuItem("NHN/Prototype/Create InGame Prototype Scene")]
    public static void CreateScene()
    {
        Scene scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

        Camera camera = CreateCamera();
        CreateGlobalLight();

        Sprite[] paperSprites = LoadPaperStateSprites(
            out int paperIdleFrameCount,
            out int paperFlyingFrameStartIndex,
            out int paperFlyingFrameCount,
            out int paperUnfoldFrameStartIndex,
            out int paperUnfoldFrameCount,
            out int paperAttachedFrameIndex);
        Sprite gameplayBackgroundSprite = LoadFirstSprite(GameplayBackgroundPath);
        Sprite bulletHoleSprite = LoadFirstSprite(BulletHolePath);
        Sprite crosshairSprite = LoadFirstSprite(CrosshairPath);
        Sprite[] cowboySprites = LoadSprites(CowboyPath);
        Sprite cylinderSprite = LoadFirstSprite(CylinderPath);
        Sprite[] scarecrowIdleFrames = LoadSequentialSprites(ScarecrowIdleFramePathPrefix, 16);
        Sprite[] scarecrowAttackedFrames = LoadSequentialSprites(ScarecrowAttackedFramePathPrefix, 16);
        Sprite[] scarecrowDeathFrames = LoadSequentialSprites(ScarecrowDeathFramePathPrefix, 16);
        Sprite scarecrowSprite = scarecrowIdleFrames.FirstOrDefault() != null ? scarecrowIdleFrames.FirstOrDefault() : LoadFirstSprite(ScarecrowPath);
        Sprite scarecrowDownSprite = scarecrowDeathFrames.LastOrDefault() != null ? scarecrowDeathFrames.LastOrDefault() : LoadFirstSprite(ScarecrowDownPath);
        Sprite fryingPanSprite = LoadFirstSprite(FryingPanPath);
        Sprite outlawBanditSprite = LoadFirstSprite(OutlawBanditPath);
        Sprite tripleShotSprite = LoadFirstSprite(TripleShotPath);
        Sprite camelCarrierSprite = LoadFirstSprite(CamelCarrierPath);
        Texture2D hudPanelTexture = LoadTexture(HudPanelPath);

        CreateGameplayBackground(gameplayBackgroundSprite, camera);
        GameObject boardObject = CreateBoard(paperSprites);
        PaperBoardTarget boardTarget = boardObject.GetComponent<PaperBoardTarget>();
        PaperWindMotion windMotion = boardObject.GetComponent<PaperWindMotion>();
        PaperBoardWarp boardWarp = boardObject.GetComponent<PaperBoardWarp>();
        PaperBoardGridRenderer gridRenderer = boardObject.GetComponent<PaperBoardGridRenderer>();
        ScarecrowPaperCarrier scarecrowCarrier = CreateScarecrowCarrier(camera, boardObject.transform, windMotion, boardWarp, gridRenderer, scarecrowSprite, scarecrowDownSprite, scarecrowIdleFrames, scarecrowAttackedFrames, scarecrowDeathFrames, paperIdleFrameCount, paperFlyingFrameStartIndex, paperFlyingFrameCount, paperUnfoldFrameStartIndex, paperUnfoldFrameCount, paperAttachedFrameIndex);
        RoundResultBoardOverlay resultOverlay = CreateResultOverlay(paperSprites.FirstOrDefault(), bulletHoleSprite);

        Transform markRoot = new GameObject("Shot Marks").transform;
        markRoot.SetParent(boardObject.transform, false);

        PrototypeSpriteAnimator cowboyAnimator = CreateCowboy(cowboySprites);
        CreateAmmoIcon(cylinderSprite);
        CreateCrosshair(crosshairSprite, camera);

        GameObject controllerObject = new GameObject("Local InGame Controller");
        LocalInGameController controller = controllerObject.AddComponent<LocalInGameController>();
        controller.targetCamera = camera;
        controller.boardTarget = boardTarget;
        controller.windMotion = windMotion;
        controller.boardWarp = boardWarp;
        controller.boardGridRenderer = gridRenderer;
        controller.scarecrowCarrier = scarecrowCarrier;
        controller.markRoot = markRoot;
        controller.bulletHoleSprite = bulletHoleSprite;
        controller.cowboyAnimator = cowboyAnimator;
        controller.resultOverlay = resultOverlay;
        controller.resultBoardSprite = paperSprites.FirstOrDefault();
        controller.fryingPanItemSprite = fryingPanSprite;
        controller.outlawBanditSprite = outlawBanditSprite;
        controller.tripleShotPowerupSprite = tripleShotSprite;
        controller.camelCarrierSprite = camelCarrierSprite;

        PrototypeHud hud = controllerObject.AddComponent<PrototypeHud>();
        hud.controller = controller;
        hud.playerHudPanelTexture = hudPanelTexture;

        EditorSceneManager.SaveScene(scene, ScenePath);
        EditorSceneManager.OpenScene(ScenePath);
        Selection.activeGameObject = controllerObject;
        Debug.Log($"Created prototype scene at {ScenePath}");
    }

    private static Camera CreateCamera()
    {
        GameObject cameraObject = new GameObject("Main Camera");
        Camera camera = cameraObject.AddComponent<Camera>();
        camera.tag = "MainCamera";
        camera.orthographic = true;
        camera.orthographicSize = 5.4f;
        camera.backgroundColor = new Color(0.17f, 0.12f, 0.08f, 1f);
        camera.clearFlags = CameraClearFlags.SolidColor;
        cameraObject.transform.position = new Vector3(0f, 0f, -10f);
        cameraObject.AddComponent<AudioListener>();
        return camera;
    }

    private static void CreateGameplayBackground(Sprite backgroundSprite, Camera camera)
    {
        if (backgroundSprite == null || camera == null)
        {
            return;
        }

        GameObject backgroundObject = new GameObject("Generated Gameplay Background");
        backgroundObject.transform.position = new Vector3(0f, 0f, 4f);

        SpriteRenderer renderer = backgroundObject.AddComponent<SpriteRenderer>();
        renderer.sprite = backgroundSprite;
        renderer.sortingOrder = -100;

        float worldHeight = camera.orthographicSize * 2f;
        float worldWidth = worldHeight * (16f / 9f);
        float scale = Mathf.Max(
            worldWidth / Mathf.Max(backgroundSprite.bounds.size.x, 0.01f),
            worldHeight / Mathf.Max(backgroundSprite.bounds.size.y, 0.01f));
        backgroundObject.transform.localScale = Vector3.one * scale;
    }

    private static void CreateGlobalLight()
    {
        Type light2DType = FindType("UnityEngine.Rendering.Universal.Light2D");
        if (light2DType == null)
        {
            return;
        }

        GameObject lightObject = new GameObject("Global Light 2D");
        Component light = lightObject.AddComponent(light2DType);
        SerializedObject serializedLight = new SerializedObject(light);
        SetInt(serializedLight.FindProperty("m_LightType"), 4);
        SetFloat(serializedLight.FindProperty("m_Intensity"), 1f);
        serializedLight.ApplyModifiedPropertiesWithoutUndo();
    }

    private static Type FindType(string fullName)
    {
        foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            Type type = assembly.GetType(fullName);
            if (type != null)
            {
                return type;
            }
        }

        return null;
    }

    private static GameObject CreateBoard(Sprite[] paperSprites)
    {
        GameObject boardObject = new GameObject("Flying Gomoku Paper Board");
        boardObject.transform.position = new Vector3(0f, 1.15f, 0f);

        GameObject visualObject = new GameObject("Paper Visual");
        visualObject.transform.SetParent(boardObject.transform, false);

        SpriteRenderer renderer = visualObject.AddComponent<SpriteRenderer>();
        renderer.sprite = paperSprites.Length > 0 ? paperSprites[0] : null;
        renderer.sortingOrder = 10;

        if (renderer.sprite != null)
        {
            float spriteSize = Mathf.Max(renderer.sprite.bounds.size.x, renderer.sprite.bounds.size.y, 0.01f);
            visualObject.transform.localScale = Vector3.one * (3.6f / spriteSize);
        }

        SpriteBoundsCenterer centerer = visualObject.AddComponent<SpriteBoundsCenterer>();
        centerer.spriteRenderer = renderer;

        PaperBoardTarget boardTarget = boardObject.AddComponent<PaperBoardTarget>();
        boardTarget.boardSize = 15;
        boardTarget.boardWorldSize = new Vector2(3.6f, 3.6f);
        boardTarget.gridInsetNormalized = 0.1f;
        boardTarget.gridNormalizedRect = new Rect(0.1f, 0.1f, 0.8f, 0.8f);

        PaperBoardGridRenderer gridRenderer = boardObject.AddComponent<PaperBoardGridRenderer>();
        gridRenderer.boardTarget = boardTarget;
        gridRenderer.syncFromTarget = true;
        gridRenderer.lineWidth = 0.018f;
        gridRenderer.sortingOrder = 16;
        gridRenderer.lineColor = new Color32(84, 48, 22, 210);
        gridRenderer.Rebuild();
        gridRenderer.SetVisible(false);

        PaperWindMotion windMotion = boardObject.AddComponent<PaperWindMotion>();
        windMotion.spriteRenderer = renderer;
        windMotion.frames = paperSprites;
        windMotion.frameRate = 12f;
        windMotion.controlTransform = false;
        windMotion.useScreenTravel = true;
        windMotion.visibleExtents = new Vector2(4.3f, 2.7f);
        windMotion.offscreenPadding = new Vector2(3.1f, 2.1f);
        windMotion.driftAmplitude = new Vector2(0.28f, 0.22f);
        windMotion.pathDurationRange = new Vector2(2f, 3.7f);
        windMotion.rotationAmplitude = 8f;

        PaperBoardWarp boardWarp = boardObject.AddComponent<PaperBoardWarp>();
        boardWarp.boardTarget = boardTarget;
        boardWarp.windMotion = windMotion;
        boardWarp.horizontalBend = 0.16f;
        boardWarp.verticalBend = 0.1f;
        boardWarp.diagonalTwist = 0.08f;
        boardWarp.enableWarp = false;

        return boardObject;
    }

    private static ScarecrowPaperCarrier CreateScarecrowCarrier(Camera camera, Transform boardTransform, PaperWindMotion windMotion, PaperBoardWarp boardWarp, PaperBoardGridRenderer gridRenderer, Sprite scarecrowSprite, Sprite scarecrowDownSprite, Sprite[] scarecrowIdleFrames, Sprite[] scarecrowAttackedFrames, Sprite[] scarecrowDeathFrames, int paperIdleFrameCount, int paperFlyingFrameStartIndex, int paperFlyingFrameCount, int paperUnfoldFrameStartIndex, int paperUnfoldFrameCount, int paperAttachedFrameIndex)
    {
        GameObject carrierObject = new GameObject("Scarecrow Paper Carrier");
        carrierObject.transform.position = new Vector3(0f, 0.35f, 0f);

        ScarecrowPaperCarrier carrier = carrierObject.AddComponent<ScarecrowPaperCarrier>();
        carrier.boardTransform = boardTransform;
        carrier.boardWindMotion = windMotion;
        carrier.boardWarp = boardWarp;
        carrier.boardGridRenderer = gridRenderer;
        carrier.targetCamera = camera;
        carrier.autoLayoutScarecrows = true;
        carrier.maxHealth = 8;
        carrier.fallDuration = 1.9f;
        carrier.attachApproachDuration = 0.5f;
        carrier.respawnDelay = 0.8f;
        carrier.moveScarecrow = false;
        carrier.moveSpeed = 4.4f;
        carrier.moveBounds = new Vector2(3.65f, 1.75f);
        carrier.paperFlightBounds = new Vector2(4.2f, 2.35f);
        carrier.paperFlightSpeed = 9.8f;
        carrier.skyY = 6.45f;
        carrier.scarecrowLocalPositions = new[]
        {
            new Vector3(-3f, -0.2f, 0f),
            new Vector3(-1f, 0.45f, 0f),
            new Vector3(1.1f, 0.25f, 0f),
            new Vector3(3f, -0.12f, 0f)
        };
        carrier.paperSocketLocalPosition = new Vector3(0f, 0.78f, -0.05f);
        carrier.paperAttachedScale = boardTransform.localScale;
        carrier.scarecrowSprite = scarecrowSprite;
        carrier.knockedDownSprite = scarecrowDownSprite;
        carrier.scarecrowIdleFrames = scarecrowIdleFrames;
        carrier.scarecrowAttackedFrames = scarecrowAttackedFrames;
        carrier.scarecrowDeathFrames = scarecrowDeathFrames;
        carrier.scarecrowIdleFrameRate = 5f;
        carrier.scarecrowAttackedFrameRate = 14f;
        carrier.scarecrowDeathFrameRate = 12f;
        carrier.idlePaperFrameIndex = 0;
        carrier.idlePaperFrameCount = paperIdleFrameCount;
        carrier.flyingPaperFrameIndex = paperFlyingFrameStartIndex;
        carrier.flyingPaperFrameStartIndex = paperFlyingFrameStartIndex;
        carrier.flyingPaperFrameCount = paperFlyingFrameCount;
        carrier.unfoldPaperFrameStartIndex = paperUnfoldFrameStartIndex;
        carrier.unfoldPaperFrameCount = paperUnfoldFrameCount;
        carrier.attachedPaperFrameIndex = paperAttachedFrameIndex;
        carrier.spriteWorldHeight = 3.5f;
        return carrier;
    }

    private static RoundResultBoardOverlay CreateResultOverlay(Sprite boardSprite, Sprite bulletHoleSprite)
    {
        GameObject overlayObject = new GameObject("Round Result Board UI");
        overlayObject.transform.position = new Vector3(0f, 0.15f, -0.2f);

        GameObject visualObject = new GameObject("Result Paper Visual");
        visualObject.transform.SetParent(overlayObject.transform, false);

        SpriteRenderer boardRenderer = visualObject.AddComponent<SpriteRenderer>();
        boardRenderer.sprite = boardSprite;
        boardRenderer.sortingOrder = 220;

        SpriteBoundsCenterer centerer = visualObject.AddComponent<SpriteBoundsCenterer>();
        centerer.spriteRenderer = boardRenderer;

        Transform markerRoot = new GameObject("Result Shot Marks").transform;
        markerRoot.SetParent(overlayObject.transform, false);

        RoundResultBoardOverlay overlay = overlayObject.AddComponent<RoundResultBoardOverlay>();
        overlay.boardRenderer = boardRenderer;
        overlay.markerRoot = markerRoot;
        overlay.bulletHoleSprite = bulletHoleSprite;
        overlay.boardWorldSize = new Vector2(6.6f, 6.6f);
        overlay.boardImageScaleMultiplier = 1.18f;
        overlay.gridInsetNormalized = 0.1f;
        overlay.gridNormalizedRect = new Rect(0.1f, 0.1f, 0.8f, 0.8f);
        overlay.gomokuMarkerWorldSize = 0.34f;
        overlay.ticTacToeMarkerWorldSize = 1.25f;

        PaperBoardGridRenderer gridRenderer = overlayObject.AddComponent<PaperBoardGridRenderer>();
        gridRenderer.syncFromTarget = false;
        gridRenderer.boardWorldSize = overlay.boardWorldSize;
        gridRenderer.gridNormalizedRect = overlay.gridNormalizedRect;
        gridRenderer.lineWidth = 0.026f;
        gridRenderer.sortingOrder = overlay.markerSortingBase;
        gridRenderer.lineColor = new Color32(84, 48, 22, 225);
        overlay.gridRenderer = gridRenderer;

        overlayObject.SetActive(false);
        return overlay;
    }

    private static PrototypeSpriteAnimator CreateCowboy(Sprite[] cowboySprites)
    {
        if (cowboySprites.Length == 0)
        {
            return null;
        }

        GameObject cowboyObject = new GameObject("Cowboy Player");
        cowboyObject.transform.position = new Vector3(-2.9f, -2.9f, 0f);
        cowboyObject.transform.localScale = Vector3.one * 0.28f;

        SpriteRenderer renderer = cowboyObject.AddComponent<SpriteRenderer>();
        renderer.sprite = cowboySprites[0];
        renderer.sortingOrder = 30;

        PrototypeSpriteAnimator animator = cowboyObject.AddComponent<PrototypeSpriteAnimator>();
        animator.spriteRenderer = renderer;
        animator.frames = cowboySprites;
        animator.framesPerSecond = 18f;
        animator.idleFrame = 0;
        return animator;
    }

    private static void CreateAmmoIcon(Sprite cylinderSprite)
    {
        if (cylinderSprite == null)
        {
            return;
        }

        GameObject iconObject = new GameObject("Ammo Cylinder Icon");
        iconObject.transform.position = new Vector3(3.65f, 4.35f, 0f);
        iconObject.transform.localScale = Vector3.one * 0.32f;

        SpriteRenderer renderer = iconObject.AddComponent<SpriteRenderer>();
        renderer.sprite = cylinderSprite;
        renderer.sortingOrder = 40;
    }

    private static void CreateCrosshair(Sprite crosshairSprite, Camera camera)
    {
        if (crosshairSprite == null)
        {
            return;
        }

        GameObject cursorObject = new GameObject("Crosshair Cursor");
        cursorObject.transform.localScale = Vector3.one * 0.24f;

        SpriteRenderer renderer = cursorObject.AddComponent<SpriteRenderer>();
        renderer.sprite = crosshairSprite;
        renderer.sortingOrder = 100;

        PrototypeCursor cursor = cursorObject.AddComponent<PrototypeCursor>();
        cursor.targetCamera = camera;
        cursor.worldZ = -1f;
    }

    private static Sprite LoadFirstSprite(string assetPath)
    {
        return LoadSprites(assetPath).FirstOrDefault();
    }

    private static Sprite[] LoadPaperStateSprites(out int idleFrameCount, out int flyingFrameStartIndex, out int flyingFrameCount, out int unfoldFrameStartIndex, out int unfoldFrameCount, out int attachedFrameIndex)
    {
        Sprite[] idleFrames = LoadSequentialSprites(PaperPromptIdleFramePathPrefix, 16);
        Sprite[] flyingFrames = LoadSequentialSprites(PaperPromptFlyingFramePathPrefix, 32);
        Sprite[] unfoldFrames = LoadSequentialSprites(PaperPromptUnfoldFramePathPrefix, 16);
        Sprite idleSprite = LoadFirstSprite(PaperIdlePath);
        Sprite attachedSprite = LoadFirstSprite(PaperAttachedPath);
        Sprite[] legacyFlightFrames = flyingFrames.Length == 0 ? LoadPaperFlightFrames() : new Sprite[0];
        Sprite fallbackFlyingSprite = flyingFrames.Length == 0 && legacyFlightFrames.Length == 0 ? LoadFirstSprite(PaperFlyingPath) : null;

        List<Sprite> stateSprites = new List<Sprite>();
        if (idleFrames.Length > 0)
        {
            stateSprites.AddRange(idleFrames);
            idleFrameCount = idleFrames.Length;
        }
        else if (idleSprite != null)
        {
            stateSprites.Add(idleSprite);
            idleFrameCount = 1;
        }
        else
        {
            idleFrameCount = 1;
        }

        flyingFrameStartIndex = stateSprites.Count;
        if (flyingFrames.Length > 0)
        {
            stateSprites.AddRange(flyingFrames);
            flyingFrameCount = flyingFrames.Length;
        }
        else if (legacyFlightFrames.Length > 0)
        {
            stateSprites.AddRange(legacyFlightFrames);
            flyingFrameCount = legacyFlightFrames.Length;
        }
        else if (fallbackFlyingSprite != null)
        {
            stateSprites.Add(fallbackFlyingSprite);
            flyingFrameCount = 1;
        }
        else
        {
            flyingFrameCount = 1;
        }

        unfoldFrameStartIndex = stateSprites.Count;
        if (unfoldFrames.Length > 0)
        {
            stateSprites.AddRange(unfoldFrames);
            unfoldFrameCount = unfoldFrames.Length;
            attachedFrameIndex = stateSprites.Count - 1;
        }
        else if (attachedSprite != null)
        {
            stateSprites.Add(attachedSprite);
            unfoldFrameCount = 1;
            attachedFrameIndex = stateSprites.Count - 1;
        }
        else
        {
            unfoldFrameCount = 1;
            unfoldFrameStartIndex = Mathf.Max(0, stateSprites.Count - 1);
            attachedFrameIndex = unfoldFrameStartIndex;
        }

        stateSprites.RemoveAll(sprite => sprite == null);
        return stateSprites.Count > 0 ? stateSprites.ToArray() : LoadSprites(PaperPath);
    }

    private static Sprite[] LoadPaperFlightFrames()
    {
        return LoadSequentialSprites(PaperFlightFramePathPrefix, 32);
    }

    private static Sprite[] LoadSequentialSprites(string assetPathPrefix, int maxFrameCount)
    {
        List<Sprite> frames = new List<Sprite>();
        for (int index = 0; index < maxFrameCount; index++)
        {
            Sprite sprite = LoadFirstSprite($"{assetPathPrefix}{index:00}.png");
            if (sprite != null)
            {
                frames.Add(sprite);
            }
        }

        return frames.ToArray();
    }

    private static Texture2D LoadTexture(string assetPath)
    {
        return AssetDatabase.LoadAssetAtPath<Texture2D>(assetPath);
    }

    private static Sprite[] LoadSprites(string assetPath)
    {
        UnityEngine.Object[] assets = AssetDatabase.LoadAllAssetsAtPath(assetPath);
        List<Sprite> sprites = assets.OfType<Sprite>().ToList();

        if (sprites.Count == 0 && AssetImporter.GetAtPath(assetPath) is TextureImporter textureImporter)
        {
            textureImporter.textureType = TextureImporterType.Sprite;
            textureImporter.spriteImportMode = SpriteImportMode.Single;
            textureImporter.alphaIsTransparency = true;
            textureImporter.mipmapEnabled = false;
            textureImporter.SaveAndReimport();

            assets = AssetDatabase.LoadAllAssetsAtPath(assetPath);
            sprites = assets.OfType<Sprite>().ToList();
        }

        sprites.Sort((left, right) => GetTrailingNumber(left.name).CompareTo(GetTrailingNumber(right.name)));
        return sprites.ToArray();
    }

    private static int GetTrailingNumber(string value)
    {
        int index = value.Length - 1;
        while (index >= 0 && char.IsDigit(value[index]))
        {
            index--;
        }

        if (index == value.Length - 1)
        {
            return 0;
        }

        string number = value.Substring(index + 1);
        return int.TryParse(number, out int result) ? result : 0;
    }

    private static void SetInt(SerializedProperty property, int value)
    {
        if (property != null)
        {
            property.intValue = value;
        }
    }

    private static void SetFloat(SerializedProperty property, float value)
    {
        if (property != null)
        {
            property.floatValue = value;
        }
    }
}

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
    private const string BulletHolePath = "Assets/Sprite/Generated/BulletHoleGenerated.png";
    private const string CrosshairPath = "Assets/Sprite/CrossHairRed.png";
    private const string CowboyPath = "Assets/Sprite/CowBoy.png";
    private const string CylinderPath = "Assets/Sprite/Silinder_front_south.png";
    private const string ScarecrowPath = "Assets/Sprite/Generated/ScarecrowCarrierGenerated.png";
    private const string ScarecrowDownPath = "Assets/Sprite/Generated/ScarecrowDownGenerated.png";
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

        Sprite[] paperSprites = LoadSprites(PaperPath);
        Sprite gameplayBackgroundSprite = LoadFirstSprite(GameplayBackgroundPath);
        Sprite bulletHoleSprite = LoadFirstSprite(BulletHolePath);
        Sprite crosshairSprite = LoadFirstSprite(CrosshairPath);
        Sprite[] cowboySprites = LoadSprites(CowboyPath);
        Sprite cylinderSprite = LoadFirstSprite(CylinderPath);
        Sprite scarecrowSprite = LoadFirstSprite(ScarecrowPath);
        Sprite scarecrowDownSprite = LoadFirstSprite(ScarecrowDownPath);
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
        ScarecrowPaperCarrier scarecrowCarrier = CreateScarecrowCarrier(boardObject.transform, windMotion, boardWarp, scarecrowSprite, scarecrowDownSprite);
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

    private static ScarecrowPaperCarrier CreateScarecrowCarrier(Transform boardTransform, PaperWindMotion windMotion, PaperBoardWarp boardWarp, Sprite scarecrowSprite, Sprite scarecrowDownSprite)
    {
        GameObject carrierObject = new GameObject("Scarecrow Paper Carrier");
        carrierObject.transform.position = new Vector3(0f, 0.35f, 0f);

        ScarecrowPaperCarrier carrier = carrierObject.AddComponent<ScarecrowPaperCarrier>();
        carrier.boardTransform = boardTransform;
        carrier.boardWindMotion = windMotion;
        carrier.boardWarp = boardWarp;
        carrier.maxHealth = 8;
        carrier.fallDuration = 0.6f;
        carrier.respawnDelay = 0.8f;
        carrier.moveSpeed = 4.4f;
        carrier.moveBounds = new Vector2(3.65f, 1.75f);
        carrier.skyY = 6.45f;
        carrier.paperSocketLocalPosition = new Vector3(0f, 0.78f, -0.05f);
        carrier.paperAttachedScale = boardTransform.localScale;
        carrier.scarecrowSprite = scarecrowSprite;
        carrier.knockedDownSprite = scarecrowDownSprite;
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
        overlay.gridInsetNormalized = 0.1f;
        overlay.gridNormalizedRect = new Rect(0.1f, 0.1f, 0.8f, 0.8f);
        overlay.gomokuMarkerWorldSize = 0.34f;
        overlay.ticTacToeMarkerWorldSize = 1.25f;
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

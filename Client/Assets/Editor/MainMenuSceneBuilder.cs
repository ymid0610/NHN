using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using NHN.Menu;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.SceneManagement;

#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem.UI;
#endif

public static class MainMenuSceneBuilder
{
    private const string ScenePath = "Assets/Scenes/MainMenu.unity";
    private const string InGameScenePath = "Assets/Scenes/InGamePrototype.unity";
    private const string TutorialScenePath = "Assets/Scenes/Tutorial.unity";
    private const string PaperPath = "Assets/Sprite/GomokuPaper.png";
    private const string CowboyPath = "Assets/Sprite/CowBoy.png";
    private const string CylinderPath = "Assets/Sprite/Silinder_front_south.png";
    private const string ScarecrowPath = "Assets/Sprite/Generated/ScarecrowCarrierGenerated.png";
    private const string UiPath = "Assets/Resources/UI/";
    private const string GeneratedButtonPath = "Assets/Resources/UI/GeneratedButtons/";

    [MenuItem("NHN/Prototype/Create Main Menu Scene")]
    public static void CreateScene()
    {
        Scene scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

        CreateCamera();
        CreateEventSystem();

        GameObject controllerObject = new GameObject("Western Main Menu Controller");
        WesternMainMenuController controller = controllerObject.AddComponent<WesternMainMenuController>();
        controller.inGameSceneName = "InGamePrototype";
        controller.tutorialSceneName = "Tutorial";
        controller.paperSprite = LoadFirstSprite(PaperPath);
        controller.cowboySprite = LoadFirstSprite(CowboyPath);
        controller.cylinderSprite = LoadFirstSprite(CylinderPath);
        controller.scarecrowSprite = LoadFirstSprite(ScarecrowPath);
        controller.menuBackdropSprite = LoadFirstSprite(UiPath + "WesternMenuBackdrop.png");
        controller.titleBackdropSprite = LoadFirstSprite(UiPath + "WesternMainMenuBackdropNoButtonsGenerated.png");
        controller.lobbyBackdropSprite = LoadFirstSprite(UiPath + "WesternLobbyBackgroundGenerated.png");
        controller.panelSprite = LoadFirstSprite(UiPath + "WesternPanel.png");
        controller.darkPanelSprite = LoadFirstSprite(UiPath + "WesternPanelDark.png");
        controller.headerSprite = LoadFirstSprite(UiPath + "WesternHeader.png");
        controller.titlePlaqueSprite = LoadFirstSprite(UiPath + "WesternTitlePlaque.png");
        controller.dividerSprite = LoadFirstSprite(UiPath + "WesternDivider.png");
        controller.buttonGoldSprite = LoadPreferredSprite(GeneratedButtonPath + "WesternButtonGoldGenerated.png", UiPath + "WesternButtonGold.png");
        controller.buttonGoldHoverSprite = LoadFirstSprite(GeneratedButtonPath + "WesternButtonGoldHoverGenerated.png");
        controller.buttonGoldPressedSprite = LoadFirstSprite(GeneratedButtonPath + "WesternButtonGoldPressedGenerated.png");
        controller.buttonDarkSprite = LoadPreferredSprite(GeneratedButtonPath + "WesternButtonDarkGenerated.png", UiPath + "WesternButtonDark.png");
        controller.buttonDarkHoverSprite = LoadFirstSprite(GeneratedButtonPath + "WesternButtonDarkHoverGenerated.png");
        controller.buttonDarkPressedSprite = LoadFirstSprite(GeneratedButtonPath + "WesternButtonDarkPressedGenerated.png");
        controller.buttonGreenSprite = LoadPreferredSprite(GeneratedButtonPath + "WesternButtonGreenGenerated.png", UiPath + "WesternButtonGreen.png");
        controller.buttonGreenHoverSprite = LoadFirstSprite(GeneratedButtonPath + "WesternButtonGreenHoverGenerated.png");
        controller.buttonGreenPressedSprite = LoadFirstSprite(GeneratedButtonPath + "WesternButtonGreenPressedGenerated.png");
        controller.buttonRedSprite = LoadPreferredSprite(GeneratedButtonPath + "WesternButtonRedGenerated.png", UiPath + "WesternButtonRed.png");
        controller.buttonRedHoverSprite = LoadFirstSprite(GeneratedButtonPath + "WesternButtonRedHoverGenerated.png");
        controller.buttonRedPressedSprite = LoadFirstSprite(GeneratedButtonPath + "WesternButtonRedPressedGenerated.png");
        controller.buttonDisabledSprite = LoadFirstSprite(GeneratedButtonPath + "WesternButtonDisabledGenerated.png");
        controller.inputSprite = LoadPreferredSprite(GeneratedButtonPath + "WesternInputGenerated.png", UiPath + "WesternInput.png");
        controller.checkboxSprite = LoadPreferredSprite(GeneratedButtonPath + "WesternCheckboxGenerated.png", UiPath + "WesternCheckbox.png");
        controller.checkboxCheckSprite = LoadFirstSprite(GeneratedButtonPath + "WesternCheckboxCheckGenerated.png");
        controller.sliderTrackSprite = LoadFirstSprite(UiPath + "WesternSliderTrack.png");
        controller.sliderFillSprite = LoadFirstSprite(UiPath + "WesternSliderFill.png");
        controller.sliderHandleSprite = LoadFirstSprite(UiPath + "WesternSliderHandle.png");

        EditorSceneManager.SaveScene(scene, ScenePath);
        UpdateBuildSettings();
        AssetDatabase.SaveAssets();

        EditorSceneManager.OpenScene(ScenePath);
        Selection.activeGameObject = controllerObject;
        Debug.Log($"Created main menu scene at {ScenePath}");
    }

    private static void CreateCamera()
    {
        GameObject cameraObject = new GameObject("Main Camera");
        Camera camera = cameraObject.AddComponent<Camera>();
        camera.tag = "MainCamera";
        camera.orthographic = true;
        camera.orthographicSize = 5.4f;
        camera.clearFlags = CameraClearFlags.SolidColor;
        camera.backgroundColor = new Color32(31, 19, 13, 255);
        cameraObject.transform.position = new Vector3(0f, 0f, -10f);
        cameraObject.AddComponent<AudioListener>();
    }

    private static void CreateEventSystem()
    {
        if (UnityEngine.Object.FindFirstObjectByType<EventSystem>() != null)
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

    private static void UpdateBuildSettings()
    {
        string[] priorityScenes =
        {
            ScenePath,
            InGameScenePath,
            TutorialScenePath
        };

        List<EditorBuildSettingsScene> scenes = new List<EditorBuildSettingsScene>();
        HashSet<string> added = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (string path in priorityScenes)
        {
            if (!File.Exists(path))
            {
                continue;
            }

            scenes.Add(new EditorBuildSettingsScene(path, true));
            added.Add(path);
        }

        foreach (EditorBuildSettingsScene existingScene in EditorBuildSettings.scenes)
        {
            if (string.IsNullOrEmpty(existingScene.path) || added.Contains(existingScene.path))
            {
                continue;
            }

            scenes.Add(existingScene);
        }

        EditorBuildSettings.scenes = scenes.ToArray();
    }

    private static Sprite LoadFirstSprite(string assetPath)
    {
        return LoadSprites(assetPath).FirstOrDefault();
    }

    private static Sprite LoadPreferredSprite(string preferredAssetPath, string fallbackAssetPath)
    {
        return LoadFirstSprite(preferredAssetPath) ?? LoadFirstSprite(fallbackAssetPath);
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
}

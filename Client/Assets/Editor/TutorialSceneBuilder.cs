using System;
using System.Collections.Generic;
using System.IO;
using NHN.Menu;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.SceneManagement;

#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem.UI;
#endif

public static class TutorialSceneBuilder
{
    private const string ScenePath = "Assets/Scenes/Tutorial.unity";
    private const string MainMenuScenePath = "Assets/Scenes/MainMenu.unity";
    private const string InGameScenePath = "Assets/Scenes/InGamePrototype.unity";
    private const string TutorialBackgroundPath = "Assets/Resources/UI/WesternLobbyBackgroundGenerated.png";

    [MenuItem("NHN/Prototype/Create Tutorial Scene")]
    public static void CreateScene()
    {
        Scene scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        CreateCamera();
        CreateEventSystem();

        GameObject controllerObject = new GameObject("Tutorial Scene Controller");
        TutorialSceneController controller = controllerObject.AddComponent<TutorialSceneController>();
        controller.mainMenuSceneName = "MainMenu";
        controller.inGameSceneName = "InGamePrototype";
        controller.backgroundTexture = LoadTexture(TutorialBackgroundPath);

        EditorSceneManager.SaveScene(scene, ScenePath);
        UpdateBuildSettings();
        AssetDatabase.SaveAssets();

        EditorSceneManager.OpenScene(ScenePath);
        Selection.activeGameObject = controllerObject;
        Debug.Log($"Created tutorial scene at {ScenePath}");
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

    private static Texture2D LoadTexture(string assetPath)
    {
        return AssetDatabase.LoadAssetAtPath<Texture2D>(assetPath);
    }

    private static void UpdateBuildSettings()
    {
        string[] priorityScenes =
        {
            MainMenuScenePath,
            InGameScenePath,
            ScenePath
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
}

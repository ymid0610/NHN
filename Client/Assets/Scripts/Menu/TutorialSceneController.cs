using UnityEngine;
using UnityEngine.SceneManagement;

#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

namespace NHN.Menu
{
    public sealed class TutorialSceneController : MonoBehaviour
    {
        [System.Serializable]
        private struct TutorialStep
        {
            public string Title;
            public string Body;
        }

        public string mainMenuSceneName = "MainMenu";
        public string inGameSceneName = "InGamePrototype";
        public Texture2D backgroundTexture;

        private readonly TutorialStep[] steps =
        {
            new TutorialStep
            {
                Title = "1. 종이가 붙을 때까지 기다리기",
                Body = "종이 보드판은 하늘에서 날아오다가 허수아비에 펼쳐집니다. 격자가 보이는 순간부터 사격할 수 있습니다."
            },
            new TutorialStep
            {
                Title = "2. 격자 교차점에 조준",
                Body = "마우스로 종이의 격자점을 쏘면 그 칸에 탄착 구멍이 남습니다. 이미 뚫린 칸은 설정에 따라 다시 쏠 수 없습니다."
            },
            new TutorialStep
            {
                Title = "3. 탄환과 턴",
                Body = "오목은 한 턴에 6발, 틱택토는 한 턴에 1발입니다. 모든 플레이어가 탄환을 쓰면 결과판에서 탄착 순서를 다시 보여줍니다."
            },
            new TutorialStep
            {
                Title = "4. 승리 조건",
                Body = "오목은 같은 플레이어 탄착 5개를 한 줄로 만들면 승리합니다. 틱택토는 3x3에서 한 줄을 만들면 승리합니다."
            },
            new TutorialStep
            {
                Title = "5. 프라이팬 아이템",
                Body = "프라이팬이 화면을 가로지를 때 맞추면 총알이 튕기며 다른 플레이어가 한 턴 기절합니다."
            }
        };

        private GUIStyle titleStyle;
        private GUIStyle bodyStyle;
        private GUIStyle buttonStyle;
        private GUIStyle panelStyle;
        private int currentStep;

        private void Awake()
        {
            if (backgroundTexture == null)
            {
                backgroundTexture = Resources.Load<Texture2D>("UI/WesternLobbyBackgroundGenerated");
            }
        }

        private void Update()
        {
            if (WasNextPressed())
            {
                NextStep();
            }

            if (WasBackPressed())
            {
                PreviousStep();
            }

            if (WasCancelPressed())
            {
                LoadMainMenu();
            }
        }

        private void OnGUI()
        {
            EnsureStyles();
            DrawBackground();

            float panelWidth = Mathf.Min(880f, Screen.width - 48f);
            float panelHeight = Mathf.Min(520f, Screen.height - 96f);
            Rect panelRect = new Rect((Screen.width - panelWidth) * 0.5f, (Screen.height - panelHeight) * 0.5f, panelWidth, panelHeight);

            Color previousColor = GUI.color;
            GUI.color = new Color(0.18f, 0.11f, 0.06f, 0.9f);
            GUI.Box(panelRect, GUIContent.none, panelStyle);
            GUI.color = previousColor;

            TutorialStep step = steps[Mathf.Clamp(currentStep, 0, steps.Length - 1)];
            GUI.Label(new Rect(panelRect.x + 44f, panelRect.y + 34f, panelRect.width - 88f, 88f), "WESTERN SHOOTOUT GUIDE", titleStyle);
            GUI.Label(new Rect(panelRect.x + 54f, panelRect.y + 136f, panelRect.width - 108f, 74f), step.Title, titleStyle);
            GUI.Label(new Rect(panelRect.x + 64f, panelRect.y + 226f, panelRect.width - 128f, 130f), step.Body, bodyStyle);
            GUI.Label(new Rect(panelRect.x + 64f, panelRect.y + panelRect.height - 112f, 220f, 34f), $"{currentStep + 1}/{steps.Length}", bodyStyle);

            Rect backRect = new Rect(panelRect.x + panelRect.width - 390f, panelRect.y + panelRect.height - 118f, 150f, 52f);
            Rect nextRect = new Rect(panelRect.x + panelRect.width - 222f, panelRect.y + panelRect.height - 118f, 168f, 52f);
            if (GUI.Button(backRect, currentStep == 0 ? "메인 메뉴" : "이전", buttonStyle))
            {
                if (currentStep == 0)
                {
                    LoadMainMenu();
                }
                else
                {
                    PreviousStep();
                }
            }

            if (GUI.Button(nextRect, currentStep == steps.Length - 1 ? "연습 시작" : "다음", buttonStyle))
            {
                if (currentStep == steps.Length - 1)
                {
                    SceneManager.LoadScene(inGameSceneName);
                }
                else
                {
                    NextStep();
                }
            }
        }

        private void DrawBackground()
        {
            if (backgroundTexture != null)
            {
                GUI.DrawTexture(new Rect(0f, 0f, Screen.width, Screen.height), backgroundTexture, ScaleMode.ScaleAndCrop);
                return;
            }

            Color previousColor = GUI.color;
            GUI.color = new Color(0.18f, 0.1f, 0.05f, 1f);
            GUI.DrawTexture(new Rect(0f, 0f, Screen.width, Screen.height), Texture2D.whiteTexture);
            GUI.color = previousColor;
        }

        private void NextStep()
        {
            currentStep = Mathf.Min(currentStep + 1, steps.Length - 1);
        }

        private void PreviousStep()
        {
            currentStep = Mathf.Max(currentStep - 1, 0);
        }

        private void LoadMainMenu()
        {
            SceneManager.LoadScene(mainMenuSceneName);
        }

        private void EnsureStyles()
        {
            if (titleStyle != null)
            {
                return;
            }

            panelStyle = new GUIStyle(GUI.skin.box)
            {
                normal = { background = Texture2D.whiteTexture }
            };

            titleStyle = new GUIStyle(GUI.skin.label)
            {
                alignment = TextAnchor.MiddleCenter,
                fontStyle = FontStyle.Bold,
                fontSize = 38,
                normal = { textColor = new Color(0.94f, 0.72f, 0.36f, 1f) },
                wordWrap = true
            };

            bodyStyle = new GUIStyle(GUI.skin.label)
            {
                alignment = TextAnchor.UpperLeft,
                fontStyle = FontStyle.Bold,
                fontSize = 24,
                normal = { textColor = new Color(0.95f, 0.83f, 0.62f, 1f) },
                wordWrap = true
            };

            buttonStyle = new GUIStyle(GUI.skin.button)
            {
                fontStyle = FontStyle.Bold,
                fontSize = 22
            };
        }

        private static bool WasNextPressed()
        {
#if ENABLE_INPUT_SYSTEM
            if (Keyboard.current != null &&
                (Keyboard.current.enterKey.wasPressedThisFrame || Keyboard.current.spaceKey.wasPressedThisFrame || Keyboard.current.rightArrowKey.wasPressedThisFrame))
            {
                return true;
            }
#endif

#if ENABLE_LEGACY_INPUT_MANAGER
            return Input.GetKeyDown(KeyCode.Return) || Input.GetKeyDown(KeyCode.Space) || Input.GetKeyDown(KeyCode.RightArrow);
#else
            return false;
#endif
        }

        private static bool WasBackPressed()
        {
#if ENABLE_INPUT_SYSTEM
            if (Keyboard.current != null && Keyboard.current.leftArrowKey.wasPressedThisFrame)
            {
                return true;
            }
#endif

#if ENABLE_LEGACY_INPUT_MANAGER
            return Input.GetKeyDown(KeyCode.LeftArrow);
#else
            return false;
#endif
        }

        private static bool WasCancelPressed()
        {
#if ENABLE_INPUT_SYSTEM
            if (Keyboard.current != null && Keyboard.current.escapeKey.wasPressedThisFrame)
            {
                return true;
            }
#endif

#if ENABLE_LEGACY_INPUT_MANAGER
            return Input.GetKeyDown(KeyCode.Escape);
#else
            return false;
#endif
        }
    }
}

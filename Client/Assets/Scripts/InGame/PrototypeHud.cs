using UnityEngine;

namespace NHN.InGame
{
    public sealed class PrototypeHud : MonoBehaviour
    {
        public LocalInGameController controller;
        public Texture2D playerHudPanelTexture;

        private GUIStyle panelStyle;
        private GUIStyle titleStyle;
        private GUIStyle buttonStyle;
        private GUIStyle hudNameStyle;
        private GUIStyle hudAmmoStyle;

        private void OnGUI()
        {
            if (controller == null)
            {
                return;
            }

            EnsureStyles();
            DrawPlayerStatusHud();

            GUILayout.BeginArea(new Rect(16f, 16f, 340f, 325f), GUIContent.none, panelStyle);
            GUILayout.Label("InGame Prototype", titleStyle);
            GUILayout.Label($"Mode: {controller.gameMode}");
            GUILayout.Label($"Round: {controller.RoundIndex}");
            GUILayout.Label($"Player: P{controller.CurrentPlayer}");
            GUILayout.Label($"Ammo: {controller.RemainingShots}/{controller.ShotsPerTurn}");
            GUILayout.Label($"Target: {(controller.TargetCanShoot ? "Attached" : "Falling")}  HP {controller.TargetHealth}/{controller.TargetMaxHealth}");
            GUILayout.Label($"Status: {controller.StatusMessage}");
            GUILayout.Label(controller.ShowingResult ? "Result: large board view" : "Shoot: left click");

            if (controller.Winner != 0)
            {
                GUILayout.Space(8f);
                GUILayout.Label($"Winner: P{controller.Winner}", titleStyle);
            }

            GUILayout.Space(8f);
            controller.blockOccupiedCells = GUILayout.Toggle(controller.blockOccupiedCells, "Block already-shot cells");
            controller.allowOverline = GUILayout.Toggle(controller.allowOverline, "Allow six-in-a-row");
            controller.itemEnabled = GUILayout.Toggle(controller.itemEnabled, "Items enabled");
            controller.requireScarecrowAttached = GUILayout.Toggle(controller.requireScarecrowAttached, "Shoot only when paper attached");

            GUILayout.Space(8f);
            GUILayout.BeginHorizontal();
            if (GUILayout.Button("Gomoku", buttonStyle))
            {
                controller.SetMode(GameMode.Gomoku);
            }

            if (GUILayout.Button("TicTacToe", buttonStyle))
            {
                controller.SetMode(GameMode.TicTacToe);
            }
            GUILayout.EndHorizontal();

            GUILayout.BeginHorizontal();
            string advanceLabel = controller.ShowingResult ? "Next Board" : "Next Player";
            if (GUILayout.Button(advanceLabel, buttonStyle))
            {
                controller.AdvanceTurn();
            }

            if (GUILayout.Button("Reset", buttonStyle))
            {
                controller.ResetMatch();
            }
            GUILayout.EndHorizontal();
            GUILayout.EndArea();
        }

        private void EnsureStyles()
        {
            if (panelStyle != null)
            {
                return;
            }

            panelStyle = new GUIStyle(GUI.skin.box)
            {
                padding = new RectOffset(14, 14, 12, 12)
            };

            titleStyle = new GUIStyle(GUI.skin.label)
            {
                fontStyle = FontStyle.Bold,
                fontSize = 18
            };

            buttonStyle = new GUIStyle(GUI.skin.button)
            {
                fixedHeight = 28f
            };

            hudNameStyle = new GUIStyle(GUI.skin.label)
            {
                alignment = TextAnchor.MiddleLeft,
                fontStyle = FontStyle.Bold,
                fontSize = 22,
                normal = { textColor = new Color(0.96f, 0.82f, 0.52f, 1f) }
            };

            hudAmmoStyle = new GUIStyle(hudNameStyle)
            {
                alignment = TextAnchor.MiddleRight,
                fontSize = 24
            };
        }

        private void DrawPlayerStatusHud()
        {
            float width = Mathf.Min(560f, Screen.width - 32f);
            Rect rect = new Rect((Screen.width - width) * 0.5f, 14f, width, 122f);

            if (playerHudPanelTexture != null)
            {
                GUI.DrawTexture(rect, playerHudPanelTexture, ScaleMode.StretchToFill, true);
            }
            else
            {
                GUI.Box(rect, GUIContent.none);
            }

            string playerName = PlayerPrefs.GetString("NHN.PlayerName", "Player");
            Rect nameRect = new Rect(rect.x + 48f, rect.y + 35f, rect.width * 0.48f, 42f);
            Rect ammoRect = new Rect(rect.x + rect.width * 0.55f, rect.y + 35f, rect.width * 0.36f, 42f);
            GUI.Label(nameRect, $"{playerName}  P{controller.CurrentPlayer}", hudNameStyle);
            GUI.Label(ammoRect, $"Ammo {controller.RemainingShots}/{controller.ShotsPerTurn}", hudAmmoStyle);
        }
    }
}

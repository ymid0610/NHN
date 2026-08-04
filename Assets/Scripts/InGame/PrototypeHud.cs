using UnityEngine;

namespace NHN.InGame
{
    public sealed class PrototypeHud : MonoBehaviour
    {
        public LocalInGameController controller;

        private GUIStyle panelStyle;
        private GUIStyle titleStyle;
        private GUIStyle buttonStyle;

        private void OnGUI()
        {
            if (controller == null)
            {
                return;
            }

            EnsureStyles();

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
        }
    }
}

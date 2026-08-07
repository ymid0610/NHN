using UnityEngine;

namespace NHN.InGame
{
    public sealed class PrototypeHud : MonoBehaviour
    {
        public LocalInGameController controller;
        public Texture2D playerHudPanelTexture;

        [Header("HUD Layout")]
        public float maxPanelWidth = 1180f;
        public float horizontalPadding = 28f;
        public float bottomOffset = 12f;
        public float heightFromWidth = 0.16f;
        public Vector2 panelHeightClamp = new Vector2(120f, 168f);
        [Range(0f, 0.16f)] public float slotSidePaddingNormalized = 0.045f;
        [Range(0f, 0.04f)] public float slotGapNormalized = 0.012f;
        [Range(0f, 0.4f)] public float slotTopNormalized = 0.16f;
        [Range(0.2f, 1f)] public float slotHeightNormalized = 0.72f;
        public Vector2 slotTextPadding = new Vector2(18f, 8f);

        private const string BottomHudResourcePath = "UI/PlayerHudBottomGenerated";
        private const int HudChamberCount = 6;
        private GUIStyle panelStyle;
        private GUIStyle titleStyle;
        private GUIStyle buttonStyle;
        private GUIStyle hudNameStyle;
        private GUIStyle hudAmmoStyle;
        private GUIStyle hudStateStyle;
        private Texture2D resolvedHudPanelTexture;

        private void OnGUI()
        {
            if (controller == null)
            {
                return;
            }

            EnsureStyles();
            DrawPlayerStatusHud();

            GUILayout.BeginArea(new Rect(16f, 16f, 340f, 290f), GUIContent.none, panelStyle);
            GUILayout.Label("InGame Prototype", titleStyle);
            GUILayout.Label($"Mode: {controller.gameMode}");
            GUILayout.Label($"Round: {controller.RoundIndex}");
            GUILayout.Label($"Player: {controller.GetPlayerDisplayName(controller.CurrentPlayer)}");
            GUILayout.Label($"Ammo: {controller.RemainingShots}/{controller.ShotsPerTurn}");
            GUILayout.Label($"Target: {(controller.TargetCanShoot ? "Attached" : "Falling")}  HP {controller.TargetHealth}/{controller.TargetMaxHealth}");
            GUILayout.Label($"Status: {controller.StatusMessage}");
            GUILayout.Label(controller.ShowingResult ? "Result: large board view" : "Shoot: left click");

            if (controller.Winner != 0)
            {
                GUILayout.Space(8f);
                GUILayout.Label($"Winner: {controller.GetPlayerDisplayName(controller.Winner)}", titleStyle);
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

            hudStateStyle = new GUIStyle(GUI.skin.label)
            {
                alignment = TextAnchor.MiddleCenter,
                fontStyle = FontStyle.Bold,
                fontSize = 15,
                normal = { textColor = new Color(0.12f, 0.07f, 0.03f, 1f) }
            };

        }

        private void DrawPlayerStatusHud()
        {
            Texture2D texture = GetHudPanelTexture();
            float width = Mathf.Min(Mathf.Max(240f, maxPanelWidth), Mathf.Max(240f, Screen.width - horizontalPadding));
            float minHeight = Mathf.Min(panelHeightClamp.x, panelHeightClamp.y);
            float maxHeight = Mathf.Max(panelHeightClamp.x, panelHeightClamp.y);
            float height = Mathf.Clamp(width * Mathf.Max(0.05f, heightFromWidth), minHeight, maxHeight);
            Rect rect = new Rect((Screen.width - width) * 0.5f, Screen.height - height - bottomOffset, width, height);

            if (texture != null)
            {
                GUI.DrawTexture(rect, texture, ScaleMode.StretchToFill, true);
            }
            else
            {
                GUI.Box(rect, GUIContent.none);
            }

            int playerCount = Mathf.Clamp(controller.PlayerCount, 1, 4);
            float slotGap = rect.width * slotGapNormalized;
            float sidePadding = rect.width * slotSidePaddingNormalized;
            float slotWidth = (rect.width - sidePadding * 2f - slotGap * 3f) / 4f;
            float slotHeight = rect.height * slotHeightNormalized;
            float slotY = rect.y + rect.height * slotTopNormalized;

            for (int player = 1; player <= 4; player++)
            {
                Rect slotRect = new Rect(rect.x + sidePadding + (player - 1) * (slotWidth + slotGap), slotY, slotWidth, slotHeight);
                bool enabled = player <= playerCount;
                bool active = player == controller.CurrentPlayer && !controller.ShowingResult && controller.Winner == 0;
                DrawPlayerSlot(slotRect, player, enabled, active);
            }
        }

        private void DrawPlayerSlot(Rect rect, int player, bool enabled, bool active)
        {
            Color previousColor = GUI.color;
            Color playerColor = GetPlayerColor(player);

            if (active)
            {
                GUI.color = new Color(playerColor.r, playerColor.g, playerColor.b, 0.52f);
                GUI.DrawTexture(rect, Texture2D.whiteTexture);
            }
            else if (!enabled)
            {
                GUI.color = new Color(0f, 0f, 0f, 0.26f);
                GUI.DrawTexture(rect, Texture2D.whiteTexture);
            }

            GUI.color = previousColor;

            string playerName = enabled ? controller.GetPlayerDisplayName(player) : "-";
            int ammo = enabled ? controller.GetDisplayedAmmoForPlayer(player) : 0;
            bool stunned = enabled && controller.IsPlayerStunned(player);

            float insetX = Mathf.Max(0f, slotTextPadding.x);
            float insetY = Mathf.Max(0f, slotTextPadding.y);
            Rect nameRect = new Rect(rect.x + insetX, rect.y + insetY, rect.width - insetX * 2f, rect.height * 0.34f);
            Rect ammoRect = new Rect(rect.x + insetX, rect.y + rect.height * 0.42f, rect.width - insetX * 2f, rect.height * 0.32f);
            Rect stateRect = new Rect(rect.x + insetX, rect.y + rect.height * 0.72f, rect.width - insetX * 2f, rect.height * 0.22f);

            GUI.Label(nameRect, enabled ? $"{playerName}  P{player}" : "EMPTY", hudNameStyle);
            GUI.Label(ammoRect, enabled ? $"{Mathf.Clamp(ammo, 0, HudChamberCount)}/{HudChamberCount}" : string.Empty, hudAmmoStyle);

            if (enabled)
            {
                string state = stunned ? "STUNNED" : active ? "TURN" : "READY";
                GUI.color = stunned ? new Color(1f, 0.35f, 0.25f, 1f) : playerColor;
                GUI.Label(stateRect, state, hudStateStyle);
                GUI.color = previousColor;
            }
        }

        private Texture2D GetHudPanelTexture()
        {
            if (resolvedHudPanelTexture != null)
            {
                return resolvedHudPanelTexture;
            }

            resolvedHudPanelTexture = Resources.Load<Texture2D>(BottomHudResourcePath);
            if (resolvedHudPanelTexture == null)
            {
                resolvedHudPanelTexture = playerHudPanelTexture;
            }

            return resolvedHudPanelTexture;
        }

        private Color GetPlayerColor(int player)
        {
            switch (player)
            {
                case 1:
                    return new Color(0.3f, 1f, 0.35f, 1f);
                case 2:
                    return new Color(1f, 0.25f, 0.2f, 1f);
                case 3:
                    return new Color(0.25f, 0.55f, 1f, 1f);
                default:
                    return new Color(1f, 0.9f, 0.25f, 1f);
            }
        }
    }
}

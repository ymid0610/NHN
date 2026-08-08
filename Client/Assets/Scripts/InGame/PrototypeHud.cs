using System.Collections.Generic;
using NHN.Network;
using UnityEngine;

namespace NHN.InGame
{
    public sealed class PrototypeHud : MonoBehaviour
    {
        public LocalInGameController controller;
        /// Takes over when a real match is running. Optional; found at startup
        /// if the scene does not wire it.
        public NetworkInGameController networkController;
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
            // Resolved lazily, not in Awake: the networked controller is added
            // during LocalInGameController.Start when a match is running, which
            // is after every Awake has been called.
            if (networkController == null)
            {
                networkController = FindFirstObjectByType<NetworkInGameController>();
            }

            if (networkController != null && networkController.HasMatch)
            {
                EnsureStyles();
                DrawNetworkHud();
                return;
            }

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
            // No re-shoot or overline toggles: the server owns both rules —
            // a cell goes to whoever claims it first, and a run longer than the
            // win length still wins — so practice offers no switch live play
            // does not have.
            controller.itemEnabled = GUILayout.Toggle(controller.itemEnabled, "Items enabled");
            controller.requireScarecrowAttached = GUILayout.Toggle(controller.requireScarecrowAttached, "Shoot only when paper attached");

            GUILayout.Space(8f);
            GUILayout.BeginHorizontal();
            for (int i = 0; i < ModeRules.Playable.Length; i++)
            {
                ModeRules rules = ModeRules.For(ModeRules.Playable[i]);
                if (GUILayout.Button(rules.DisplayName, buttonStyle))
                {
                    controller.SetMode(rules.Mode);
                }
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

        /// <summary>
        /// The simultaneous-play HUD.
        ///
        /// Deliberately has no "current player" and no TURN marker: every player
        /// shoots throughout the wave, so all four ammo counts are live at once
        /// and the only per-player state worth showing is whether an item has
        /// taken someone out of the pass.
        /// </summary>
        private void DrawNetworkHud()
        {
            InstanceClient client = networkController.Client;
            if (client == null)
            {
                return;
            }

            List<ServerRoomMember> players = client.Setup.Players;
            int playerCount = players != null ? players.Count : 0;

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

            float slotGap = rect.width * slotGapNormalized;
            float sidePadding = rect.width * slotSidePaddingNormalized;
            float slotWidth = (rect.width - sidePadding * 2f - slotGap * 3f) / 4f;
            float slotHeight = rect.height * slotHeightNormalized;
            float slotY = rect.y + rect.height * slotTopNormalized;

            for (int index = 0; index < 4; index++)
            {
                Rect slotRect = new Rect(rect.x + sidePadding + index * (slotWidth + slotGap), slotY, slotWidth, slotHeight);
                if (index >= playerCount)
                {
                    DrawEmptySlot(slotRect);
                    continue;
                }

                DrawNetworkSlot(slotRect, client, players[index]);
            }

            GUILayout.BeginArea(new Rect(16f, 16f, 340f, 190f), GUIContent.none, panelStyle);
            GUILayout.Label("네트워크 대전", titleStyle);
            GUILayout.Label(string.Format("모드: {0}", ServerProtocolText.ToDisplay(client.Setup.Mode)));
            GUILayout.Label(string.Format("라운드: {0}/{1}", networkController.RoundNumber, networkController.RoundCount));
            GUILayout.Label(client.WaveActive
                ? string.Format("웨이브 {0} 진행 중", client.Wave.WaveIndex + 1)
                : "웨이브 대기");
            GUILayout.Label(string.Format("내 탄약: {0}", DescribeAmmo(client, client.Slot)));
            GUILayout.Label("상태: " + networkController.StatusMessage);
            GUILayout.EndArea();
        }

        private void DrawNetworkSlot(Rect rect, InstanceClient client, ServerRoomMember member)
        {
            Color previousColor = GUI.color;
            Color slotColor = networkController.GetSlotColor(member.Slot);
            ServerStatusFlags flags = client.GetStatus(member.Slot);
            bool stunned = (flags & ServerStatusFlags.Stunned) != 0;
            bool blinded = (flags & ServerStatusFlags.Blinded) != 0;
            bool isMe = member.Slot == client.Slot;

            if (isMe)
            {
                GUI.color = new Color(slotColor.r, slotColor.g, slotColor.b, 0.32f);
                GUI.DrawTexture(rect, Texture2D.whiteTexture);
                GUI.color = previousColor;
            }

            float insetX = Mathf.Max(0f, slotTextPadding.x);
            float insetY = Mathf.Max(0f, slotTextPadding.y);
            Rect nameRect = new Rect(rect.x + insetX, rect.y + insetY, rect.width - insetX * 2f, rect.height * 0.34f);
            Rect ammoRect = new Rect(rect.x + insetX, rect.y + rect.height * 0.42f, rect.width - insetX * 2f, rect.height * 0.32f);
            Rect stateRect = new Rect(rect.x + insetX, rect.y + rect.height * 0.72f, rect.width - insetX * 2f, rect.height * 0.22f);

            GUI.Label(nameRect, string.Format("{0}{1}", member.Nickname, isMe ? "  (나)" : string.Empty), hudNameStyle);
            GUI.Label(ammoRect, DescribeAmmo(client, member.Slot), hudAmmoStyle);

            string state = stunned ? "STUNNED" : blinded ? "BLIND" : "READY";
            GUI.color = stunned ? new Color(1f, 0.35f, 0.25f, 1f)
                : blinded ? new Color(0.55f, 0.6f, 1f, 1f)
                : slotColor;
            GUI.Label(stateRect, state, hudStateStyle);
            GUI.color = previousColor;
        }

        private void DrawEmptySlot(Rect rect)
        {
            Color previousColor = GUI.color;
            GUI.color = new Color(0f, 0f, 0f, 0.26f);
            GUI.DrawTexture(rect, Texture2D.whiteTexture);
            GUI.color = previousColor;

            float insetX = Mathf.Max(0f, slotTextPadding.x);
            Rect nameRect = new Rect(rect.x + insetX, rect.y + Mathf.Max(0f, slotTextPadding.y), rect.width - insetX * 2f, rect.height * 0.34f);
            GUI.Label(nameRect, "EMPTY", hudNameStyle);
        }

        private static string DescribeAmmo(InstanceClient client, byte slot)
        {
            byte perWave = client.Setup.Config.AmmoPerWave;
            if (perWave == GameField.Unlimited)
            {
                return "∞";
            }

            return string.Format("{0}/{1}", client.GetAmmo(slot), perWave);
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

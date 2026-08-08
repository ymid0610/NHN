using UnityEngine;

namespace NHN.Network
{
    /// <summary>
    /// Carries the instance connection from the menu scene into the game scene.
    ///
    /// The handoff has to survive SceneManager.LoadScene, and the socket cannot
    /// simply be reopened on the other side: the ticket is single-use and
    /// expires in 30 seconds, so the connection must be made once, in the menu,
    /// and kept. This object marks itself DontDestroyOnLoad and owns the client
    /// for the lifetime of the match.
    /// </summary>
    public sealed class NetworkGameSession : MonoBehaviour
    {
        public static NetworkGameSession Current { get; private set; }

        public InstanceClient Client { get; private set; }
        public ServerGameStartingInfo StartInfo { get; private set; }
        public string LocalNickname { get; private set; }
        public ulong LocalSessionId { get; private set; }

        /// True when a networked match is in progress, so the in-game scene
        /// knows to follow the server instead of running the local prototype.
        public static bool IsNetworked
        {
            get { return Current != null && Current.Client != null; }
        }

        /// <summary>
        /// Opens the connection described by S_GameStarting. Call this before
        /// loading the game scene, not after.
        /// </summary>
        public static NetworkGameSession Begin(ServerGameStartingInfo info, string nickname, ulong sessionId)
        {
            End();

            GameObject host = new GameObject("NHN Network Session");
            DontDestroyOnLoad(host);

            NetworkGameSession session = host.AddComponent<NetworkGameSession>();
            session.StartInfo = info;
            session.LocalNickname = nickname;
            session.LocalSessionId = sessionId;
            session.Client = host.AddComponent<InstanceClient>();
            session.Client.Connect(info.Host, info.Port, info.Ticket);

            Current = session;
            return session;
        }

        /// <summary>
        /// Closes the match connection and drops the carrier. Safe to call when
        /// there is no session.
        /// </summary>
        public static void End()
        {
            if (Current == null)
            {
                return;
            }

            NetworkGameSession session = Current;
            Current = null;

            if (session.Client != null)
            {
                session.Client.Disconnect();
            }

            Destroy(session.gameObject);
        }

        private void OnDestroy()
        {
            if (Current == this)
            {
                Current = null;
            }
        }
    }
}

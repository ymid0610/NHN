using UnityEngine;

namespace NHN.InGame
{
    public sealed class ScarecrowPaperCarrier : MonoBehaviour
    {
        private enum CarrierState
        {
            FallingIn,
            Attached,
            KnockedDown,
            RespawnDelay
        }

        public Transform boardTransform;
        public PaperWindMotion boardWindMotion;
        public PaperBoardWarp boardWarp;
        public int maxHealth = 8;
        public float fallDuration = 0.65f;
        public float respawnDelay = 0.85f;
        public float moveSpeed = 4.2f;
        public Vector2 moveBounds = new Vector2(3.7f, 1.75f);
        public float skyY = 6.5f;
        public Vector3 paperSocketLocalPosition = new Vector3(0f, 0.8f, -0.05f);
        public Vector3 paperAttachedScale = Vector3.one;
        public Sprite scarecrowSprite;
        public float spriteWorldHeight = 3.5f;
        public bool buildPlaceholderVisual = true;

        private static Sprite solidSprite;
        private Transform visualRoot;
        private Vector3 moveTarget;
        private Vector3 fallStart;
        private float stateTimer;
        private int health;
        private CarrierState state;

        public int CurrentHealth => health;
        public int MaxHealth => maxHealth;
        public bool CanShoot => state == CarrierState.Attached;

        private void Awake()
        {
            if (buildPlaceholderVisual)
            {
                EnsurePlaceholderVisual();
            }
        }

        private void Start()
        {
            ResetCarrier();
        }

        private void Update()
        {
            MoveScarecrow();
            UpdateState();
        }

        public void ResetCarrier()
        {
            health = maxHealth;
            state = CarrierState.FallingIn;
            stateTimer = 0f;
            moveTarget = RandomMoveTarget();
            transform.position = RandomMoveTarget();
            fallStart = RandomSkyPosition();

            if (visualRoot != null)
            {
                visualRoot.localRotation = Quaternion.identity;
            }

            if (boardTransform != null)
            {
                boardTransform.gameObject.SetActive(true);
                boardTransform.position = fallStart;
                boardTransform.rotation = Quaternion.identity;
                boardTransform.localScale = paperAttachedScale;
            }

            ConfigureBoardMotion(false);
        }

        public bool RegisterHit()
        {
            if (!CanShoot)
            {
                return false;
            }

            health = Mathf.Max(health - 1, 0);
            if (health <= 0)
            {
                KnockDown();
            }

            return true;
        }

        private void UpdateState()
        {
            switch (state)
            {
                case CarrierState.FallingIn:
                    UpdateFallingIn();
                    break;
                case CarrierState.Attached:
                    AttachPaperToSocket();
                    break;
                case CarrierState.KnockedDown:
                    UpdateKnockdown();
                    break;
                case CarrierState.RespawnDelay:
                    UpdateRespawnDelay();
                    break;
            }
        }

        private void UpdateFallingIn()
        {
            stateTimer += Time.deltaTime;
            float t = Mathf.Clamp01(stateTimer / Mathf.Max(0.05f, fallDuration));
            float eased = Mathf.SmoothStep(0f, 1f, t);

            if (boardTransform != null)
            {
                Vector3 socket = PaperSocketWorldPosition();
                Vector3 wind = new Vector3(Mathf.Sin(Time.time * 14f) * 0.2f, Mathf.Cos(Time.time * 11f) * 0.08f, 0f);
                boardTransform.position = Vector3.Lerp(fallStart, socket, eased) + wind * (1f - eased);
                boardTransform.rotation = Quaternion.Euler(0f, 0f, Mathf.Lerp(18f, 0f, eased) + Mathf.Sin(Time.time * 16f) * 5f * (1f - eased));
                boardTransform.localScale = paperAttachedScale;
            }

            if (t >= 1f)
            {
                state = CarrierState.Attached;
                stateTimer = 0f;
                AttachPaperToSocket();
            }
        }

        private void UpdateKnockdown()
        {
            stateTimer += Time.deltaTime;
            float t = Mathf.Clamp01(stateTimer / 0.35f);

            if (visualRoot != null)
            {
                visualRoot.localRotation = Quaternion.Lerp(Quaternion.identity, Quaternion.Euler(0f, 0f, -78f), Mathf.SmoothStep(0f, 1f, t));
            }

            if (boardTransform != null)
            {
                boardTransform.position += new Vector3(0f, -2.7f, 0f) * Time.deltaTime;
                boardTransform.Rotate(0f, 0f, -180f * Time.deltaTime);
            }

            if (stateTimer >= 0.45f)
            {
                state = CarrierState.RespawnDelay;
                stateTimer = 0f;
                if (boardTransform != null)
                {
                    boardTransform.gameObject.SetActive(false);
                }
            }
        }

        private void UpdateRespawnDelay()
        {
            stateTimer += Time.deltaTime;
            if (stateTimer >= respawnDelay)
            {
                ResetCarrier();
            }
        }

        private void KnockDown()
        {
            state = CarrierState.KnockedDown;
            stateTimer = 0f;
            ConfigureBoardMotion(false);
        }

        private void AttachPaperToSocket()
        {
            if (boardTransform == null)
            {
                return;
            }

            boardTransform.position = PaperSocketWorldPosition();
            boardTransform.rotation = transform.rotation * Quaternion.Euler(0f, 0f, Mathf.Sin(Time.time * 7f) * 2.5f);
            boardTransform.localScale = paperAttachedScale;
            ConfigureBoardMotion(false);
        }

        private void MoveScarecrow()
        {
            if (state == CarrierState.KnockedDown || state == CarrierState.RespawnDelay)
            {
                return;
            }

            transform.position = Vector3.MoveTowards(transform.position, moveTarget, moveSpeed * Time.deltaTime);
            if (Vector3.Distance(transform.position, moveTarget) < 0.05f)
            {
                moveTarget = RandomMoveTarget();
            }
        }

        private Vector3 PaperSocketWorldPosition()
        {
            return transform.TransformPoint(paperSocketLocalPosition);
        }

        private Vector3 RandomMoveTarget()
        {
            return new Vector3(
                Random.Range(-moveBounds.x, moveBounds.x),
                Random.Range(-moveBounds.y, moveBounds.y) + 0.45f,
                0f);
        }

        private Vector3 RandomSkyPosition()
        {
            return new Vector3(Random.Range(-moveBounds.x, moveBounds.x), skyY, 0f);
        }

        private void ConfigureBoardMotion(bool enabled)
        {
            if (boardWindMotion != null)
            {
                boardWindMotion.controlTransform = enabled;
                boardWindMotion.SetResultView(true);
            }

            if (boardWarp != null)
            {
                boardWarp.enableWarp = false;
            }
        }

        private void EnsurePlaceholderVisual()
        {
            if (visualRoot != null)
            {
                return;
            }

            visualRoot = new GameObject("Scarecrow Visual").transform;
            visualRoot.SetParent(transform, false);

            if (scarecrowSprite != null)
            {
                GameObject spriteObject = new GameObject("Scarecrow Sprite");
                Transform spriteTransform = spriteObject.transform;
                spriteTransform.SetParent(visualRoot, false);

                SpriteRenderer renderer = spriteObject.AddComponent<SpriteRenderer>();
                renderer.sprite = scarecrowSprite;
                renderer.sortingOrder = 8;

                float sourceHeight = Mathf.Max(scarecrowSprite.bounds.size.y, 0.01f);
                spriteTransform.localScale = Vector3.one * (spriteWorldHeight / sourceHeight);
                return;
            }

            CreatePart("Shadow", new Vector3(0f, -1.18f, 0.08f), new Vector3(1.25f, 0.18f, 1f), new Color(0f, 0f, 0f, 0.28f), 4);
            CreatePart("Pole", new Vector3(0f, -0.28f, 0f), new Vector3(0.12f, 1.85f, 1f), new Color(0.43f, 0.22f, 0.09f, 1f), 5);
            CreatePart("Crossbar", new Vector3(0f, 0.32f, -0.01f), new Vector3(1.35f, 0.12f, 1f), new Color(0.47f, 0.25f, 0.1f, 1f), 6);
            CreatePart("Coat", new Vector3(0f, -0.08f, -0.02f), new Vector3(0.78f, 1f, 1f), new Color(0.62f, 0.37f, 0.16f, 1f), 7);
            CreatePart("Head", new Vector3(0f, 0.68f, -0.03f), new Vector3(0.48f, 0.48f, 1f), new Color(0.86f, 0.62f, 0.27f, 1f), 8);
            CreatePart("HatBrim", new Vector3(0f, 0.98f, -0.04f), new Vector3(0.75f, 0.13f, 1f), new Color(0.31f, 0.16f, 0.07f, 1f), 9);
            CreatePart("HatTop", new Vector3(0f, 1.11f, -0.05f), new Vector3(0.42f, 0.28f, 1f), new Color(0.36f, 0.18f, 0.08f, 1f), 10);
        }

        private void CreatePart(string partName, Vector3 localPosition, Vector3 localScale, Color color, int sortingOrder)
        {
            GameObject part = new GameObject(partName);
            Transform partTransform = part.transform;
            partTransform.SetParent(visualRoot, false);
            partTransform.localPosition = localPosition;
            partTransform.localScale = localScale;

            SpriteRenderer renderer = part.AddComponent<SpriteRenderer>();
            renderer.sprite = GetSolidSprite();
            renderer.color = color;
            renderer.sortingOrder = sortingOrder;
        }

        private static Sprite GetSolidSprite()
        {
            if (solidSprite != null)
            {
                return solidSprite;
            }

            Texture2D texture = new Texture2D(1, 1, TextureFormat.RGBA32, false);
            texture.SetPixel(0, 0, Color.white);
            texture.Apply();
            solidSprite = Sprite.Create(texture, new Rect(0f, 0f, 1f, 1f), new Vector2(0.5f, 0.5f), 1f);
            return solidSprite;
        }
    }
}

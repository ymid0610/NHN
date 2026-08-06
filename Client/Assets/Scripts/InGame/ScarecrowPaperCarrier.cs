using System.Collections.Generic;
using UnityEngine;

namespace NHN.InGame
{
    public sealed class ScarecrowPaperCarrier : MonoBehaviour
    {
        private sealed class ScarecrowVisual
        {
            public Transform Root;
            public Transform VisualRoot;
            public Transform SpriteTransform;
            public SpriteRenderer SpriteRenderer;
            public Sprite[] AnimationFrames;
            public float AnimationFrameRate;
            public float AnimationTime;
            public bool LoopAnimation;
            public bool ReturnToIdleWhenDone;
        }

        private enum CarrierState
        {
            FallingIn,
            Attached,
            KnockedDown,
            RespawnDelay
        }

        private enum PaperVisualState
        {
            None,
            Flying,
            Attached,
            Static
        }

        public Transform boardTransform;
        public PaperWindMotion boardWindMotion;
        public PaperBoardWarp boardWarp;
        public PaperBoardGridRenderer boardGridRenderer;
        public Camera targetCamera;
        public bool autoLayoutScarecrows = true;
        public Vector2 autoLayoutViewportPadding = new Vector2(0.1f, 0.16f);
        public int maxHealth = 8;
        public float fallDuration = 1.85f;
        public float attachApproachDuration = 0.48f;
        public float respawnDelay = 0.85f;
        public float moveSpeed = 4.2f;
        public Vector2 moveBounds = new Vector2(3.7f, 1.75f);
        public Vector2 paperFlightBounds = new Vector2(4.25f, 2.35f);
        public float paperFlightSpeed = 9.5f;
        public float skyY = 6.5f;
        public Vector3 paperSocketLocalPosition = new Vector3(0f, 0.8f, -0.05f);
        public Vector3 paperAttachedScale = Vector3.one;
        public Vector3[] scarecrowLocalPositions =
        {
            new Vector3(-3f, -0.2f, 0f),
            new Vector3(-1f, 0.45f, 0f),
            new Vector3(1.1f, 0.25f, 0f),
            new Vector3(3f, -0.12f, 0f)
        };
        public bool moveScarecrow;
        public Sprite scarecrowSprite;
        public Sprite knockedDownSprite;
        public Sprite[] scarecrowIdleFrames;
        public Sprite[] scarecrowAttackedFrames;
        public Sprite[] scarecrowDeathFrames;
        [Min(1f)] public float scarecrowIdleFrameRate = 5f;
        [Min(1f)] public float scarecrowAttackedFrameRate = 14f;
        [Min(1f)] public float scarecrowDeathFrameRate = 12f;
        public int idlePaperFrameIndex;
        [Min(1)] public int idlePaperFrameCount = 1;
        public int flyingPaperFrameIndex = 1;
        public int flyingPaperFrameStartIndex = 1;
        [Min(1)] public int flyingPaperFrameCount = 1;
        public int unfoldPaperFrameStartIndex = 2;
        [Min(1)] public int unfoldPaperFrameCount = 1;
        public int attachedPaperFrameIndex = 2;
        [Min(1f)] public float paperIdleFrameRate = 4f;
        [Min(1f)] public float paperFlyingFrameRate = 12f;
        [Min(1f)] public float paperUnfoldFrameRate = 12f;
        public float spriteWorldHeight = 3.5f;
        public bool buildPlaceholderVisual = true;

        private static Sprite solidSprite;
        private readonly List<ScarecrowVisual> scarecrowVisuals = new List<ScarecrowVisual>();
        private ScarecrowVisual activeVisual;
        private Vector3 moveTarget;
        private Vector3 fallStart;
        private Vector3 approachStart;
        private Vector3 paperFlightTarget;
        private float stateTimer;
        private int health;
        private CarrierState state;
        private PaperVisualState paperVisualState;
        private bool approachStarted;
        private float lastLayoutAspect = -1f;
        private float lastLayoutOrthographicSize = -1f;

        public int CurrentHealth => health;
        public int MaxHealth => maxHealth;
        public bool CanShoot => state == CarrierState.Attached;

        private void Awake()
        {
            if (buildPlaceholderVisual)
            {
                EnsureScarecrowVisuals();
            }
        }

        private void Start()
        {
            ResetCarrier();
        }

        private void Update()
        {
            RefreshResponsiveLayoutIfNeeded();
            AnimateScarecrowVisuals();
            MoveScarecrow();
            UpdateState();
        }

        public void ResetCarrier()
        {
            RefreshResponsiveLayout(true);
            EnsureScarecrowVisuals();
            health = maxHealth;
            state = CarrierState.FallingIn;
            paperVisualState = PaperVisualState.None;
            stateTimer = 0f;
            approachStarted = false;
            activeVisual = PickScarecrowVisual();
            moveTarget = RandomMoveTarget();
            fallStart = RandomSkyPosition();
            paperFlightTarget = RandomPaperFlightTarget();

            ResetScarecrowVisuals();
            if (activeVisual != null && activeVisual.VisualRoot != null)
            {
                activeVisual.VisualRoot.localRotation = Quaternion.identity;
            }

            PlayScarecrowIdle(activeVisual, true);

            if (boardTransform != null)
            {
                boardTransform.gameObject.SetActive(true);
                boardTransform.position = fallStart;
                boardTransform.rotation = Quaternion.identity;
                boardTransform.localScale = paperAttachedScale;
            }

            ConfigureBoardFlight(false);
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
            else
            {
                PlayScarecrowAnimation(activeVisual, scarecrowAttackedFrames, scarecrowAttackedFrameRate, false, true, GetDefaultScarecrowSprite());
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
            float totalDuration = Mathf.Max(0.2f, fallDuration);
            float approachDuration = Mathf.Min(Mathf.Max(0.12f, attachApproachDuration), totalDuration * 0.65f);
            float freeFlightDuration = Mathf.Max(0f, totalDuration - approachDuration);

            if (boardTransform != null)
            {
                if (stateTimer < freeFlightDuration)
                {
                    UpdatePaperFreeFlight();
                }
                else
                {
                    UpdatePaperApproach(freeFlightDuration, approachDuration);
                }

                boardTransform.localScale = paperAttachedScale;
            }

            if (stateTimer >= totalDuration)
            {
                state = CarrierState.Attached;
                stateTimer = 0f;
                AttachPaperToSocket();
            }
        }

        private void UpdatePaperFreeFlight()
        {
            boardTransform.position = Vector3.MoveTowards(boardTransform.position, paperFlightTarget, paperFlightSpeed * Time.deltaTime);
            if (Vector3.Distance(boardTransform.position, paperFlightTarget) < 0.12f)
            {
                paperFlightTarget = RandomPaperFlightTarget();
            }

            float flutter = Mathf.Sin(Time.time * 18f) * 13f + Mathf.Cos(Time.time * 11f) * 5f;
            boardTransform.rotation = Quaternion.Euler(0f, 0f, flutter);
        }

        private void UpdatePaperApproach(float freeFlightDuration, float approachDuration)
        {
            if (!approachStarted)
            {
                approachStarted = true;
                approachStart = boardTransform.position;
            }

            float t = Mathf.Clamp01((stateTimer - freeFlightDuration) / Mathf.Max(0.05f, approachDuration));
            float eased = Mathf.SmoothStep(0f, 1f, t);
            Vector3 socket = PaperSocketWorldPosition();
            Vector3 wind = new Vector3(Mathf.Sin(Time.time * 14f) * 0.18f, Mathf.Cos(Time.time * 11f) * 0.07f, 0f);
            boardTransform.position = Vector3.Lerp(approachStart, socket, eased) + wind * (1f - eased);
            boardTransform.rotation = Quaternion.Euler(0f, 0f, Mathf.Lerp(20f, 0f, eased) + Mathf.Sin(Time.time * 16f) * 5f * (1f - eased));
        }

        private void UpdateKnockdown()
        {
            stateTimer += Time.deltaTime;
            float t = Mathf.Clamp01(stateTimer / 0.35f);

            if (activeVisual != null && activeVisual.VisualRoot != null)
            {
                if (!HasFrames(scarecrowDeathFrames) && knockedDownSprite == null)
                {
                    activeVisual.VisualRoot.localRotation = Quaternion.Lerp(Quaternion.identity, Quaternion.Euler(0f, 0f, -78f), Mathf.SmoothStep(0f, 1f, t));
                }
                else
                {
                    activeVisual.VisualRoot.localRotation = Quaternion.identity;
                }
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
            PlayScarecrowAnimation(activeVisual, scarecrowDeathFrames, scarecrowDeathFrameRate, false, false, knockedDownSprite != null ? knockedDownSprite : GetDefaultScarecrowSprite());
            ConfigureBoardMotion(false, attachedPaperFrameIndex, false);
        }

        private void AttachPaperToSocket()
        {
            if (boardTransform == null)
            {
                return;
            }

            boardTransform.position = PaperSocketWorldPosition();
            Transform socketRoot = activeVisual != null ? activeVisual.Root : transform;
            boardTransform.rotation = socketRoot.rotation * Quaternion.Euler(0f, 0f, Mathf.Sin(Time.time * 7f) * 2.5f);
            boardTransform.localScale = paperAttachedScale;
            if (paperVisualState != PaperVisualState.Attached)
            {
                ConfigureBoardUnfold(false);
            }

            if (boardGridRenderer != null)
            {
                boardGridRenderer.SetVisible(boardWindMotion == null || boardWindMotion.IsAnimationComplete);
            }
        }

        private void MoveScarecrow()
        {
            if (!moveScarecrow || activeVisual == null || activeVisual.Root == null || state == CarrierState.KnockedDown || state == CarrierState.RespawnDelay)
            {
                return;
            }

            activeVisual.Root.localPosition = Vector3.MoveTowards(activeVisual.Root.localPosition, moveTarget, moveSpeed * Time.deltaTime);
            if (Vector3.Distance(activeVisual.Root.localPosition, moveTarget) < 0.05f)
            {
                moveTarget = RandomMoveTarget();
            }
        }

        private Vector3 PaperSocketWorldPosition()
        {
            Transform socketRoot = activeVisual != null ? activeVisual.Root : transform;
            return socketRoot.TransformPoint(paperSocketLocalPosition);
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
            return transform.TransformPoint(new Vector3(Random.Range(-paperFlightBounds.x, paperFlightBounds.x), skyY, 0f));
        }

        private Vector3 RandomPaperFlightTarget()
        {
            return transform.TransformPoint(new Vector3(
                Random.Range(-paperFlightBounds.x, paperFlightBounds.x),
                Random.Range(-paperFlightBounds.y, paperFlightBounds.y) + 0.45f,
                0f));
        }

        private void ConfigureBoardMotion(bool enabled, int staticFrameIndex, bool gridVisible)
        {
            if (boardWindMotion != null)
            {
                boardWindMotion.controlTransform = enabled;
                boardWindMotion.SetStaticFrame(staticFrameIndex);
            }

            paperVisualState = PaperVisualState.Static;

            if (boardWarp != null)
            {
                boardWarp.enableWarp = false;
            }

            if (boardGridRenderer != null)
            {
                boardGridRenderer.SetVisible(gridVisible);
            }
        }

        private void ConfigureBoardFlight(bool enabled)
        {
            if (boardWindMotion != null)
            {
                boardWindMotion.controlTransform = enabled;
                boardWindMotion.frameRate = paperFlyingFrameRate;
                int startIndex = flyingPaperFrameStartIndex > 0 ? flyingPaperFrameStartIndex : flyingPaperFrameIndex;
                boardWindMotion.PlayFrameRange(startIndex, flyingPaperFrameCount);
            }

            paperVisualState = PaperVisualState.Flying;

            if (boardWarp != null)
            {
                boardWarp.enableWarp = false;
            }

            if (boardGridRenderer != null)
            {
                boardGridRenderer.SetVisible(false);
            }
        }

        private void ConfigureBoardUnfold(bool enabled)
        {
            if (boardWindMotion != null)
            {
                boardWindMotion.controlTransform = enabled;
                boardWindMotion.frameRate = paperUnfoldFrameRate;
                int startIndex = unfoldPaperFrameStartIndex > 0 ? unfoldPaperFrameStartIndex : attachedPaperFrameIndex;
                if (unfoldPaperFrameCount > 1)
                {
                    boardWindMotion.PlayFrameRange(startIndex, unfoldPaperFrameCount, false);
                }
                else
                {
                    boardWindMotion.SetStaticFrame(attachedPaperFrameIndex);
                }
            }

            paperVisualState = PaperVisualState.Attached;

            if (boardWarp != null)
            {
                boardWarp.enableWarp = false;
            }

            if (boardGridRenderer != null)
            {
                boardGridRenderer.SetVisible(false);
            }
        }

        private void EnsureScarecrowVisuals()
        {
            int desiredCount = GetScarecrowCount();
            if (scarecrowVisuals.Count == desiredCount && !ShouldRebuildScarecrowVisuals())
            {
                ApplyScarecrowVisualPositions();
                return;
            }

            for (int index = scarecrowVisuals.Count - 1; index >= 0; index--)
            {
                if (scarecrowVisuals[index].Root != null)
                {
                    Destroy(scarecrowVisuals[index].Root.gameObject);
                }
            }

            scarecrowVisuals.Clear();

            for (int index = 0; index < desiredCount; index++)
            {
                ScarecrowVisual visual = CreateScarecrowVisual(index, GetScarecrowLocalPosition(index));
                scarecrowVisuals.Add(visual);
            }
        }

        private void ApplyScarecrowVisualPositions()
        {
            for (int index = 0; index < scarecrowVisuals.Count; index++)
            {
                ScarecrowVisual visual = scarecrowVisuals[index];
                if (visual.Root != null)
                {
                    visual.Root.localPosition = GetScarecrowLocalPosition(index);
                }
            }
        }

        private ScarecrowVisual CreateScarecrowVisual(int index, Vector3 localPosition)
        {
            ScarecrowVisual visual = new ScarecrowVisual();
            GameObject rootObject = new GameObject($"Scarecrow Target {index + 1}");
            visual.Root = rootObject.transform;
            visual.Root.SetParent(transform, false);
            visual.Root.localPosition = localPosition;

            visual.VisualRoot = new GameObject("Scarecrow Visual").transform;
            visual.VisualRoot.SetParent(visual.Root, false);

            Sprite defaultSprite = GetDefaultScarecrowSprite();
            if (defaultSprite != null)
            {
                GameObject spriteObject = new GameObject("Scarecrow Sprite");
                visual.SpriteTransform = spriteObject.transform;
                visual.SpriteTransform.SetParent(visual.VisualRoot, false);

                visual.SpriteRenderer = spriteObject.AddComponent<SpriteRenderer>();
                visual.SpriteRenderer.sortingOrder = 8;

                ApplyGeneratedSprite(visual, defaultSprite);
                PlayScarecrowIdle(visual, true);
                return visual;
            }

            CreatePart(visual.VisualRoot, "Shadow", new Vector3(0f, -1.18f, 0.08f), new Vector3(1.25f, 0.18f, 1f), new Color(0f, 0f, 0f, 0.28f), 4);
            CreatePart(visual.VisualRoot, "Pole", new Vector3(0f, -0.28f, 0f), new Vector3(0.12f, 1.85f, 1f), new Color(0.43f, 0.22f, 0.09f, 1f), 5);
            CreatePart(visual.VisualRoot, "Crossbar", new Vector3(0f, 0.32f, -0.01f), new Vector3(1.35f, 0.12f, 1f), new Color(0.47f, 0.25f, 0.1f, 1f), 6);
            CreatePart(visual.VisualRoot, "Coat", new Vector3(0f, -0.08f, -0.02f), new Vector3(0.78f, 1f, 1f), new Color(0.62f, 0.37f, 0.16f, 1f), 7);
            CreatePart(visual.VisualRoot, "Head", new Vector3(0f, 0.68f, -0.03f), new Vector3(0.48f, 0.48f, 1f), new Color(0.86f, 0.62f, 0.27f, 1f), 8);
            CreatePart(visual.VisualRoot, "HatBrim", new Vector3(0f, 0.98f, -0.04f), new Vector3(0.75f, 0.13f, 1f), new Color(0.31f, 0.16f, 0.07f, 1f), 9);
            CreatePart(visual.VisualRoot, "HatTop", new Vector3(0f, 1.11f, -0.05f), new Vector3(0.42f, 0.28f, 1f), new Color(0.36f, 0.18f, 0.08f, 1f), 10);
            return visual;
        }

        private void ResetScarecrowVisuals()
        {
            foreach (ScarecrowVisual visual in scarecrowVisuals)
            {
                if (visual.VisualRoot != null)
                {
                    visual.VisualRoot.localRotation = Quaternion.identity;
                }

                PlayScarecrowIdle(visual, true);
            }
        }

        private ScarecrowVisual PickScarecrowVisual()
        {
            if (scarecrowVisuals.Count == 0)
            {
                return null;
            }

            return scarecrowVisuals[Random.Range(0, scarecrowVisuals.Count)];
        }

        private int GetScarecrowCount()
        {
            return scarecrowLocalPositions == null || scarecrowLocalPositions.Length == 0 ? 1 : scarecrowLocalPositions.Length;
        }

        private void RefreshResponsiveLayoutIfNeeded()
        {
            if (!autoLayoutScarecrows || moveScarecrow)
            {
                return;
            }

            Camera layoutCamera = GetLayoutCamera();
            if (layoutCamera == null)
            {
                return;
            }

            if (Mathf.Abs(layoutCamera.aspect - lastLayoutAspect) < 0.001f &&
                Mathf.Abs(layoutCamera.orthographicSize - lastLayoutOrthographicSize) < 0.001f)
            {
                return;
            }

            RefreshResponsiveLayout(true);
            ApplyScarecrowVisualPositions();
        }

        private void RefreshResponsiveLayout(bool force)
        {
            if (!autoLayoutScarecrows)
            {
                return;
            }

            Camera layoutCamera = GetLayoutCamera();
            if (layoutCamera == null)
            {
                return;
            }

            if (!force &&
                Mathf.Abs(layoutCamera.aspect - lastLayoutAspect) < 0.001f &&
                Mathf.Abs(layoutCamera.orthographicSize - lastLayoutOrthographicSize) < 0.001f)
            {
                return;
            }

            if (!TryGetCameraWorldBounds(layoutCamera, out float minX, out float maxX, out float minY, out float maxY))
            {
                return;
            }

            int count = Mathf.Max(1, GetScarecrowCount());
            if (scarecrowLocalPositions == null || scarecrowLocalPositions.Length != count)
            {
                scarecrowLocalPositions = new Vector3[count];
            }

            float worldWidth = Mathf.Max(0.1f, maxX - minX);
            float worldHeight = Mathf.Max(0.1f, maxY - minY);
            float centerX = (minX + maxX) * 0.5f;
            float xReach = Mathf.Min(worldWidth * 0.27f, worldHeight * 0.72f);
            float yBase = Mathf.Lerp(minY, maxY, 0.52f);
            float yStep = worldHeight * 0.045f;

            for (int index = 0; index < count; index++)
            {
                float normalized = count <= 1 ? 0.5f : (float)index / (count - 1);
                float x = centerX + Mathf.Lerp(-xReach, xReach, normalized);
                float yPattern = index == 0 || index == count - 1 ? -1f : 0.55f;
                float y = yBase + yPattern * yStep;
                Vector3 worldPosition = new Vector3(x, y, transform.position.z);
                scarecrowLocalPositions[index] = transform.InverseTransformPoint(worldPosition);
            }

            Vector3 localLeft = transform.InverseTransformPoint(new Vector3(centerX - xReach, yBase, transform.position.z));
            Vector3 localRight = transform.InverseTransformPoint(new Vector3(centerX + xReach, yBase, transform.position.z));
            Vector3 localBottom = transform.InverseTransformPoint(new Vector3(centerX, minY, transform.position.z));
            Vector3 localTop = transform.InverseTransformPoint(new Vector3(centerX, maxY, transform.position.z));

            float localXReach = Mathf.Abs(localRight.x - localLeft.x) * 0.5f;
            float localHeight = Mathf.Abs(localTop.y - localBottom.y);
            paperFlightBounds = new Vector2(Mathf.Max(3.2f, localXReach + 0.85f), Mathf.Max(1.7f, localHeight * 0.22f));
            moveBounds = new Vector2(Mathf.Max(2.2f, localXReach * 0.72f), Mathf.Max(1.1f, localHeight * 0.16f));
            skyY = Mathf.Max(localTop.y + localHeight * 0.12f, 5.8f);
            lastLayoutAspect = layoutCamera.aspect;
            lastLayoutOrthographicSize = layoutCamera.orthographicSize;
        }

        private Camera GetLayoutCamera()
        {
            if (targetCamera != null)
            {
                return targetCamera;
            }

            targetCamera = Camera.main;
            return targetCamera;
        }

        private bool TryGetCameraWorldBounds(Camera layoutCamera, out float minX, out float maxX, out float minY, out float maxY)
        {
            float paddingX = Mathf.Clamp(autoLayoutViewportPadding.x, 0.04f, 0.28f);
            float paddingY = Mathf.Clamp(autoLayoutViewportPadding.y, 0.08f, 0.3f);
            float zDistance = Mathf.Abs(transform.position.z - layoutCamera.transform.position.z);
            if (zDistance <= 0.01f)
            {
                zDistance = layoutCamera.nearClipPlane + 0.01f;
            }

            Vector3 bottomLeft = layoutCamera.ViewportToWorldPoint(new Vector3(paddingX, paddingY, zDistance));
            Vector3 topRight = layoutCamera.ViewportToWorldPoint(new Vector3(1f - paddingX, 1f - paddingY, zDistance));
            minX = Mathf.Min(bottomLeft.x, topRight.x);
            maxX = Mathf.Max(bottomLeft.x, topRight.x);
            minY = Mathf.Min(bottomLeft.y, topRight.y);
            maxY = Mathf.Max(bottomLeft.y, topRight.y);
            return maxX - minX > 0.1f && maxY - minY > 0.1f;
        }

        private Vector3 GetScarecrowLocalPosition(int index)
        {
            if (scarecrowLocalPositions == null || scarecrowLocalPositions.Length == 0)
            {
                return Vector3.zero;
            }

            return scarecrowLocalPositions[Mathf.Clamp(index, 0, scarecrowLocalPositions.Length - 1)];
        }

        private bool ShouldRebuildScarecrowVisuals()
        {
            if (GetDefaultScarecrowSprite() == null)
            {
                return false;
            }

            foreach (ScarecrowVisual visual in scarecrowVisuals)
            {
                if (visual.SpriteRenderer == null || visual.SpriteTransform == null)
                {
                    return true;
                }
            }

            return false;
        }

        private void AnimateScarecrowVisuals()
        {
            foreach (ScarecrowVisual visual in scarecrowVisuals)
            {
                if (visual.SpriteRenderer == null || visual.AnimationFrames == null || visual.AnimationFrames.Length == 0)
                {
                    continue;
                }

                visual.AnimationTime += Time.deltaTime;
                float frameRate = Mathf.Max(1f, visual.AnimationFrameRate);
                int frameIndex = Mathf.FloorToInt(visual.AnimationTime * frameRate);

                if (visual.LoopAnimation)
                {
                    frameIndex %= visual.AnimationFrames.Length;
                }
                else if (frameIndex >= visual.AnimationFrames.Length)
                {
                    if (visual.ReturnToIdleWhenDone)
                    {
                        PlayScarecrowIdle(visual, false);
                        continue;
                    }

                    frameIndex = visual.AnimationFrames.Length - 1;
                }

                ApplyGeneratedSprite(visual, visual.AnimationFrames[Mathf.Clamp(frameIndex, 0, visual.AnimationFrames.Length - 1)]);
            }
        }

        private void PlayScarecrowIdle(ScarecrowVisual visual, bool restart)
        {
            PlayScarecrowAnimation(visual, scarecrowIdleFrames, scarecrowIdleFrameRate, true, false, GetDefaultScarecrowSprite(), restart);
        }

        private void PlayScarecrowAnimation(ScarecrowVisual visual, Sprite[] frames, float frameRate, bool loop, bool returnToIdleWhenDone, Sprite fallbackSprite, bool restart = true)
        {
            if (visual == null || visual.SpriteRenderer == null)
            {
                return;
            }

            if (!HasFrames(frames))
            {
                visual.AnimationFrames = null;
                visual.ReturnToIdleWhenDone = false;
                visual.LoopAnimation = false;
                if (fallbackSprite != null)
                {
                    ApplyGeneratedSprite(visual, fallbackSprite);
                }

                return;
            }

            visual.AnimationFrames = frames;
            visual.AnimationFrameRate = frameRate;
            visual.LoopAnimation = loop;
            visual.ReturnToIdleWhenDone = returnToIdleWhenDone;
            if (restart)
            {
                visual.AnimationTime = 0f;
            }

            ApplyGeneratedSprite(visual, frames[0]);
        }

        private Sprite GetDefaultScarecrowSprite()
        {
            if (HasFrames(scarecrowIdleFrames))
            {
                return scarecrowIdleFrames[0];
            }

            return scarecrowSprite;
        }

        private static bool HasFrames(Sprite[] frames)
        {
            return frames != null && frames.Length > 0;
        }

        private void ApplyGeneratedSprite(ScarecrowVisual visual, Sprite sprite)
        {
            if (visual == null || visual.SpriteRenderer == null || visual.SpriteTransform == null || sprite == null)
            {
                return;
            }

            visual.SpriteRenderer.sprite = sprite;
            visual.SpriteTransform.localRotation = Quaternion.identity;
            visual.SpriteTransform.localPosition = Vector3.zero;

            Vector2 size = sprite.bounds.size;
            float sourceSize = Mathf.Max(size.x, size.y, 0.01f);
            visual.SpriteTransform.localScale = Vector3.one * (spriteWorldHeight / sourceSize);
        }

        private void CreatePart(Transform parent, string partName, Vector3 localPosition, Vector3 localScale, Color color, int sortingOrder)
        {
            GameObject part = new GameObject(partName);
            Transform partTransform = part.transform;
            partTransform.SetParent(parent, false);
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

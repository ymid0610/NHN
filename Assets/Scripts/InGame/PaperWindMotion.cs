using UnityEngine;

namespace NHN.InGame
{
    public sealed class PaperWindMotion : MonoBehaviour
    {
        public SpriteRenderer spriteRenderer;
        public Sprite[] frames;
        [Min(1f)] public float frameRate = 12f;
        public bool controlTransform = true;
        public bool useScreenTravel = true;
        public Vector2 visibleExtents = new Vector2(4.4f, 2.6f);
        public Vector2 offscreenPadding = new Vector2(2.1f, 1.45f);
        public Vector2 driftAmplitude = new Vector2(0.45f, 0.25f);
        public Vector2 pathDurationRange = new Vector2(2.2f, 4.4f);
        public float rotationAmplitude = 7f;
        public float scalePulse = 0.04f;

        private Vector3 baseLocalPosition;
        private Quaternion baseLocalRotation;
        private Vector3 baseLocalScale;
        private Vector3 pathStart;
        private Vector3 pathEnd;
        private Vector3 curveOffset;
        private float pathTimer;
        private float pathDuration;
        private int pathStep;
        private bool initialized;
        private bool resultView;

        public bool IsResultView => resultView;
        public int CurrentFrameIndex { get; private set; }
        public float MotionPhase { get; private set; }

        private void Awake()
        {
            CacheBasePose();
        }

        private void OnEnable()
        {
            CacheBasePose();
            RestartTravel();
        }

        private void Update()
        {
            CacheBasePose();

            if (resultView)
            {
                if (controlTransform)
                {
                    transform.localPosition = Vector3.Lerp(transform.localPosition, baseLocalPosition, Time.deltaTime * 9f);
                    transform.localRotation = Quaternion.Slerp(transform.localRotation, baseLocalRotation, Time.deltaTime * 9f);
                    transform.localScale = Vector3.Lerp(transform.localScale, baseLocalScale, Time.deltaTime * 9f);
                }

                CurrentFrameIndex = 0;
                SetFrame(0);
                return;
            }

            float time = Time.time;
            MotionPhase = time;
            CurrentFrameIndex = frames == null || frames.Length == 0 ? 0 : Mathf.FloorToInt(time * frameRate) % frames.Length;
            SetFrame(CurrentFrameIndex);

            if (!controlTransform)
            {
                return;
            }

            Vector3 pathPosition = useScreenTravel ? EvaluateTravelPath() : baseLocalPosition;
            Vector3 flutterOffset = new Vector3(
                Mathf.Sin(time * 4.9f) * driftAmplitude.x + Mathf.Sin(time * 8.3f) * driftAmplitude.x * 0.25f,
                Mathf.Cos(time * 5.7f) * driftAmplitude.y + Mathf.Sin(time * 9.1f) * driftAmplitude.y * 0.2f,
                0f);

            float rotation = Mathf.Sin(time * 3.4f) * rotationAmplitude + Mathf.Sin(time * 9.1f) * rotationAmplitude * 0.35f;
            float scale = 1f + Mathf.Sin(time * 3.6f) * scalePulse;

            transform.localPosition = pathPosition + flutterOffset;
            transform.localRotation = baseLocalRotation * Quaternion.Euler(0f, 0f, rotation);
            transform.localScale = baseLocalScale * scale;

        }

        public void SetResultView(bool value)
        {
            CacheBasePose();
            resultView = value;

            if (!value)
            {
                RestartTravel();
            }
        }

        public void RestartTravel()
        {
            CacheBasePose();
            pathStep = 0;
            pathStart = RandomOffscreenPoint();
            pathEnd = RandomVisiblePoint();
            curveOffset = RandomCurveOffset();
            pathDuration = Random.Range(pathDurationRange.x, pathDurationRange.y);
            pathTimer = 0f;
            transform.localPosition = pathStart;
        }

        private void CacheBasePose()
        {
            if (initialized)
            {
                return;
            }

            baseLocalPosition = transform.localPosition;
            baseLocalRotation = transform.localRotation;
            baseLocalScale = transform.localScale;
            initialized = true;
        }

        private void SetFrame(int index)
        {
            if (spriteRenderer == null || frames == null || frames.Length == 0)
            {
                return;
            }

            spriteRenderer.sprite = frames[Mathf.Clamp(index, 0, frames.Length - 1)];
        }

        private Vector3 EvaluateTravelPath()
        {
            pathTimer += Time.deltaTime;
            float duration = Mathf.Max(0.1f, pathDuration);
            float t = Mathf.Clamp01(pathTimer / duration);
            float eased = Mathf.SmoothStep(0f, 1f, t);
            Vector3 arc = curveOffset * Mathf.Sin(t * Mathf.PI);
            Vector3 position = Vector3.Lerp(pathStart, pathEnd, eased) + arc;

            if (t >= 1f)
            {
                pathStart = pathEnd;
                pathEnd = GetNextTarget();
                curveOffset = RandomCurveOffset();
                pathDuration = Random.Range(pathDurationRange.x, pathDurationRange.y);
                pathTimer = 0f;
                pathStep++;
            }

            return position;
        }

        private Vector3 GetNextTarget()
        {
            // Every few hops the paper leaves the screen, then comes back from another edge.
            if (pathStep % 4 == 2)
            {
                return RandomOffscreenPoint();
            }

            return RandomVisiblePoint();
        }

        private Vector3 RandomVisiblePoint()
        {
            return baseLocalPosition + new Vector3(
                Random.Range(-visibleExtents.x, visibleExtents.x),
                Random.Range(-visibleExtents.y, visibleExtents.y),
                0f);
        }

        private Vector3 RandomOffscreenPoint()
        {
            int edge = 3;
            float xLimit = visibleExtents.x + offscreenPadding.x;
            float yLimit = visibleExtents.y + offscreenPadding.y;

            switch (edge)
            {
                case 0:
                    return baseLocalPosition + new Vector3(-xLimit, Random.Range(-yLimit, yLimit), 0f);
                case 1:
                    return baseLocalPosition + new Vector3(xLimit, Random.Range(-yLimit, yLimit), 0f);
                case 2:
                    return baseLocalPosition + new Vector3(Random.Range(-xLimit, xLimit), -yLimit, 0f);
                default:
                    return baseLocalPosition + new Vector3(Random.Range(-xLimit, xLimit), yLimit, 0f);
            }
        }

        private Vector3 RandomCurveOffset()
        {
            return new Vector3(
                Random.Range(-1.2f, 1.2f),
                Random.Range(-0.8f, 0.8f),
                0f);
        }
    }
}

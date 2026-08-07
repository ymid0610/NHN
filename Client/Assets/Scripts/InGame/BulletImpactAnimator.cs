using UnityEngine;

namespace NHN.InGame
{
    public sealed class BulletImpactAnimator : MonoBehaviour
    {
        public SpriteRenderer spriteRenderer;
        public Sprite[] frames;
        public Sprite finalSprite;
        public Transform targetTransform;
        public SpriteBoundsCenterer centerer;
        public Vector3 finalLocalScale = Vector3.one;
        public bool applyFinalLocalScale;
        [Min(1f)] public float framesPerSecond = 18f;
        public bool playOnEnable = true;
        public bool destroyWhenDone;

        private float elapsed;
        private bool playing;

        private void OnEnable()
        {
            if (playOnEnable)
            {
                Play();
            }
        }

        private void Update()
        {
            if (!playing || spriteRenderer == null || frames == null || frames.Length == 0)
            {
                return;
            }

            elapsed += Time.deltaTime;
            int frameIndex = Mathf.FloorToInt(elapsed * Mathf.Max(1f, framesPerSecond));
            if (frameIndex >= frames.Length)
            {
                Finish();
                return;
            }

            spriteRenderer.sprite = frames[frameIndex];
            centerer?.CenterNow();
        }

        public void Play()
        {
            elapsed = 0f;
            playing = frames != null && frames.Length > 0;
            if (spriteRenderer == null)
            {
                spriteRenderer = GetComponent<SpriteRenderer>();
            }

            if (targetTransform == null)
            {
                targetTransform = transform;
            }

            if (centerer == null)
            {
                centerer = GetComponent<SpriteBoundsCenterer>();
            }

            if (playing && spriteRenderer != null)
            {
                spriteRenderer.sprite = frames[0];
                centerer?.CenterNow();
            }
        }

        private void Finish()
        {
            playing = false;
            if (spriteRenderer != null)
            {
                if (finalSprite != null)
                {
                    spriteRenderer.sprite = finalSprite;
                }
                else if (frames != null && frames.Length > 0)
                {
                    spriteRenderer.sprite = frames[frames.Length - 1];
                }

                if (applyFinalLocalScale && targetTransform != null)
                {
                    targetTransform.localScale = finalLocalScale;
                    WarpedBoardMarker warpedMarker = GetComponentInParent<WarpedBoardMarker>();
                    warpedMarker?.SetBaseVisualScale(finalLocalScale);
                }

                centerer?.CenterNow();
            }

            if (destroyWhenDone)
            {
                Destroy(gameObject);
            }
        }
    }
}

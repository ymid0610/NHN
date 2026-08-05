using UnityEngine;

namespace NHN.InGame
{
    public sealed class PrototypeSpriteAnimator : MonoBehaviour
    {
        public SpriteRenderer spriteRenderer;
        public Sprite[] frames;
        [Min(1f)] public float framesPerSecond = 18f;
        public int idleFrame;

        private bool playing;
        private float timer;

        private void Awake()
        {
            SetFrame(idleFrame);
        }

        private void Update()
        {
            if (!playing || frames == null || frames.Length == 0)
            {
                return;
            }

            timer += Time.deltaTime;
            int index = Mathf.FloorToInt(timer * framesPerSecond);

            if (index >= frames.Length)
            {
                playing = false;
                SetFrame(idleFrame);
                return;
            }

            SetFrame(index);
        }

        public void PlayOnce()
        {
            if (frames == null || frames.Length == 0)
            {
                return;
            }

            timer = 0f;
            playing = true;
            SetFrame(0);
        }

        private void SetFrame(int index)
        {
            if (spriteRenderer == null || frames == null || frames.Length == 0)
            {
                return;
            }

            spriteRenderer.sprite = frames[Mathf.Clamp(index, 0, frames.Length - 1)];
        }
    }
}

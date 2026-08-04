using UnityEngine;

namespace NHN.InGame
{
    [ExecuteAlways]
    public sealed class SpriteBoundsCenterer : MonoBehaviour
    {
        public SpriteRenderer spriteRenderer;

        private void LateUpdate()
        {
            CenterNow();
        }

        public void CenterNow()
        {
            if (spriteRenderer == null || spriteRenderer.sprite == null)
            {
                return;
            }

            Vector3 center = spriteRenderer.sprite.bounds.center;
            Vector3 scaledCenter = Vector3.Scale(center, transform.localScale);
            transform.localPosition = new Vector3(-scaledCenter.x, -scaledCenter.y, transform.localPosition.z);
        }
    }
}

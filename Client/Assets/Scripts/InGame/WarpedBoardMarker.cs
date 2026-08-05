using UnityEngine;

namespace NHN.InGame
{
    public sealed class WarpedBoardMarker : MonoBehaviour
    {
        public PaperBoardTarget boardTarget;
        public PaperBoardWarp boardWarp;
        public Transform visualRoot;
        public SpriteBoundsCenterer visualCenterer;
        public Vector2Int cell;

        private Vector3 baseVisualScale = Vector3.one;

        public void Configure(PaperBoardTarget target, PaperBoardWarp warp, Vector2Int gridCell, Transform visual)
        {
            boardTarget = target;
            boardWarp = warp;
            cell = gridCell;
            visualRoot = visual;
            visualCenterer = visualRoot != null ? visualRoot.GetComponent<SpriteBoundsCenterer>() : null;
            baseVisualScale = visualRoot != null ? visualRoot.localScale : Vector3.one;
            ApplyWarp();
        }

        private void LateUpdate()
        {
            ApplyWarp();
        }

        private void ApplyWarp()
        {
            if (boardTarget == null)
            {
                return;
            }

            PaperBoardWarp activeWarp = boardWarp;
            Vector3 localPosition = activeWarp != null ? activeWarp.CellToLocal(cell) : boardTarget.CellToLocal(cell);
            Vector3 worldPosition = boardTarget.transform.TransformPoint(localPosition);
            if (transform.parent != null)
            {
                transform.localPosition = transform.parent.InverseTransformPoint(worldPosition);
            }
            else
            {
                transform.position = worldPosition;
            }

            float rotation = activeWarp != null ? activeWarp.CellRotationDegrees(cell) : 0f;
            Quaternion worldRotation = boardTarget.transform.rotation * Quaternion.Euler(0f, 0f, rotation);
            if (transform.parent != null)
            {
                transform.localRotation = Quaternion.Inverse(transform.parent.rotation) * worldRotation;
            }
            else
            {
                transform.rotation = worldRotation;
            }

            if (visualRoot != null)
            {
                visualRoot.localScale = activeWarp != null ? activeWarp.CellVisualScale(cell, baseVisualScale) : baseVisualScale;
                visualCenterer?.CenterNow();
            }
        }
    }
}

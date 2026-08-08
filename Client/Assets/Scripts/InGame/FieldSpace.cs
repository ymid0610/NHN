using NHN.Network;
using UnityEngine;

namespace NHN.InGame
{
    /// <summary>
    /// Maps the server's play field onto the camera's view.
    ///
    /// Two things differ between the two spaces and both matter:
    ///
    ///  - The field is 1920x1080 with Y INCREASING DOWNWARD (the target sheet
    ///    enters at a negative y above the field and leaves below it). Unity
    ///    world space has Y up, so every conversion flips.
    ///  - The field has a fixed 16:9 aspect and the camera does not. The scale
    ///    is therefore uniform and the field is fitted inside the view, rather
    ///    than stretched to fill it: a non-uniform scale would make the square
    ///    target sheet render as a rectangle, and a shot near a cell edge would
    ///    land in a different cell than it looked like.
    ///
    /// Aiming uses the exact inverse of the transform used for drawing, so what
    /// the player clicks on is what the server rewinds to.
    /// </summary>
    public struct FieldSpace
    {
        public Vector2 Origin;   // world position of field (0, 0), i.e. top-left
        public float UnitsPerPixel;
        public bool IsValid;

        public static FieldSpace FromCamera(Camera camera, bool stretchToFill)
        {
            FieldSpace space = default(FieldSpace);
            if (camera == null)
            {
                return space;
            }

            float worldHeight = camera.orthographic
                ? camera.orthographicSize * 2f
                : Mathf.Abs(camera.transform.position.z) * 2f *
                  Mathf.Tan(camera.fieldOfView * 0.5f * Mathf.Deg2Rad);
            float worldWidth = worldHeight * camera.aspect;

            float scaleX = worldWidth / GameField.Width;
            float scaleY = worldHeight / GameField.Height;

            // Fit, not fill. See the class comment: a non-uniform scale moves
            // where cell boundaries land on screen.
            float scale = stretchToFill ? Mathf.Max(scaleX, scaleY) : Mathf.Min(scaleX, scaleY);

            Vector3 centre = camera.transform.position;
            space.UnitsPerPixel = scale;
            space.Origin = new Vector2(
                centre.x - GameField.Width * scale * 0.5f,
                centre.y + GameField.Height * scale * 0.5f);
            space.IsValid = scale > 0f;
            return space;
        }

        public Vector2 FieldToWorld(float fieldX, float fieldY)
        {
            return new Vector2(
                Origin.x + fieldX * UnitsPerPixel,
                Origin.y - fieldY * UnitsPerPixel);
        }

        public Vector3 FieldToWorld(float fieldX, float fieldY, float z)
        {
            Vector2 flat = FieldToWorld(fieldX, fieldY);
            return new Vector3(flat.x, flat.y, z);
        }

        public Vector2 WorldToField(Vector3 world)
        {
            if (UnitsPerPixel <= 0f)
            {
                return Vector2.zero;
            }

            return new Vector2(
                (world.x - Origin.x) / UnitsPerPixel,
                (Origin.y - world.y) / UnitsPerPixel);
        }

        /// Field units to Unity world units, for sizing sprites from an entity's
        /// half-extents.
        public float ToWorldSize(float fieldSize)
        {
            return fieldSize * UnitsPerPixel;
        }

        public bool TryScreenToField(Camera camera, Vector2 screenPosition, out Vector2 field)
        {
            field = Vector2.zero;
            if (camera == null || !IsValid)
            {
                return false;
            }

            Vector3 world = camera.ScreenToWorldPoint(new Vector3(
                screenPosition.x,
                screenPosition.y,
                Mathf.Abs(camera.transform.position.z)));

            field = WorldToField(world);
            return field.x >= 0f && field.y >= 0f &&
                   field.x < GameField.Width && field.y < GameField.Height;
        }
    }
}

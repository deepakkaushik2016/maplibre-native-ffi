// Generated from the C headers by tools/bindgen. Do not edit.
#nullable enable
namespace Maplibre.NativeFfi;

/// <summary>
/// A change to the camera from where it is now, for gestures.
/// </summary>
/// <remarks>
/// See <c>mln_camera_delta</c> in the <see
/// href="https://maplibre.org/maplibre-native-ffi/reference/c/map_8h.html">C API reference</see>.
/// </remarks>
public sealed record CameraDelta
{
    /// <summary>
    /// Moves the map's content this far on the screen, as a drag does.
    /// </summary>
    public ScreenPoint? Offset { get; init; }

    /// <summary>
    /// Multiplies the map's scale. 2 zooms in by one level.
    /// </summary>
    public double? Scale { get; init; }

    /// <summary>
    /// Degrees to add to the bearing.
    /// </summary>
    public double? Bearing { get; init; }

    /// <summary>
    /// Degrees to add to the pitch.
    /// </summary>
    public double? Pitch { get; init; }

    /// <summary>
    /// Where the delta leaves the finger, or the fingers' focal point: the
    /// screen point that <c>scale</c>, <c>bearing</c>, and <c>pitch</c> keep in
    /// place. It needs <c>offset</c> or one of them, and it combines with
    /// <c>offset</c> only in a delta without a duration.
    /// </summary>
    /// <remarks>
    /// With <c>offset</c>, the ground under <c>anchor</c> minus <c>offset</c>,
    /// where the finger was, moves under <c>anchor</c>. Over terrain that is
    /// the ground drawn there, at its own height, so a drag keeps the ground it
    /// grabbed under the finger. With a scale, bearing, or pitch too, as a
    /// pinch sends them, the change turns about where the focal point was
    /// before the pan moves it, so a pinch keeps its ground under the focal
    /// point as well. During a gesture, a delta that starts where the last one
    /// left the finger keeps the ground that the gesture first grabbed.
    /// </remarks>
    public ScreenPoint? Anchor { get; init; }

    /// <summary>
    /// How the gesture keeps moving after this delta. Only a delta in the
    /// <c>GesturePhase.End</c> phase carries it.
    /// </summary>
    public GestureMomentum? Momentum { get; init; }
    public AnimationOptions Animation { get; init; } = new();
    public GesturePhase GesturePhase { get; init; }
}

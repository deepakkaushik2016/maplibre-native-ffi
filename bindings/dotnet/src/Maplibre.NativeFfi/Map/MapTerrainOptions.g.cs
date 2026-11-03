// Generated from the C headers by tools/bindgen. Do not edit.
#nullable enable
namespace Maplibre.NativeFfi;

/// <summary>
/// How the map loads its style's terrain, and how its camera keeps to it.
/// </summary>
/// <remarks>
/// <para>The options apply only while the style has terrain.</para>
/// <para>See <c>mln_map_terrain_options</c> in the <see
/// href="https://maplibre.org/maplibre-native-ffi/reference/c/map_8h.html">C API reference</see>.</para>
/// </remarks>
public sealed record MapTerrainOptions
{
    /// <summary>
    /// <c>TerrainLoadMode.Quality</c> by default.
    /// </summary>
    public TerrainLoadMode? LoadMode { get; init; }

    /// <summary>
    /// Whether the camera's center follows the height of the terrain under it,
    /// rather than staying at its <c>center_altitude</c>. Off by default.
    /// </summary>
    /// <remarks>
    /// With it on, the camera also keeps its eye 10 m above the ground under
    /// the eye. During a gesture, the center keeps the height it had when the
    /// gesture started, and the gesture's end moves the center onto the ground
    /// without moving the camera, as
    /// <c>MapHandle.AnchorCenterOnTerrainAsync()</c> does.
    /// </remarks>
    public bool? CenterClampedToGround { get; init; }
}

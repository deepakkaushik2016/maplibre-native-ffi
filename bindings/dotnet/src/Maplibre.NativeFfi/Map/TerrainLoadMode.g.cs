// Generated from the C headers by tools/bindgen. Do not edit.
namespace Maplibre.NativeFfi;

/// <summary>
/// How a map loads 3D terrain: how much it builds in one frame, and how many
/// terrain mesh tiles a frame may have.
/// </summary>
/// <remarks>
/// See <c>mln_terrain_load_mode</c> in the <see
/// href="https://maplibre.org/maplibre-native-ffi/reference/c/map_8h.html">C API reference</see>.
/// </remarks>
public enum TerrainLoadMode : uint
{
    /// <summary>
    /// Builds every revealed tile in the frame that reveals it, and keeps up to
    /// 64 mesh tiles. The sharpest, but a frame can stall on a burst of tiles.
    /// </summary>
    Quality = 0,

    /// <summary>
    /// Builds up to 32 new tiles and redraws up to 16 terrain textures a frame,
    /// and keeps up to 48 mesh tiles.
    /// </summary>
    Balanced = 1,

    /// <summary>
    /// Builds up to 8 new tiles and redraws up to 4 terrain textures a frame,
    /// and keeps up to 24 mesh tiles. The smoothest on a weak GPU, and the
    /// shortest view distance.
    /// </summary>
    Performance = 2,
}

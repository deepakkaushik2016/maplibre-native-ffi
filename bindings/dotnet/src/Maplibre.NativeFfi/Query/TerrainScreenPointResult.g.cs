// Generated from the C headers by tools/bindgen. Do not edit.
#nullable enable
namespace Maplibre.NativeFfi;

/// <summary>
/// Where the terrain draws one coordinate of
/// <c>RenderSessionHandle.QueryPixelsForLatLngsAsync()</c>, if it draws it.
/// </summary>
/// <remarks>
/// See <c>mln_terrain_screen_point_result</c> in the <see
/// href="https://maplibre.org/maplibre-native-ffi/reference/c/query_8h.html">C API reference</see>.
/// </remarks>
/// <param name="Found">
/// Whether the terrain draws the coordinate, as
/// <c>RenderSessionHandle.QueryPixelForLatLngAsync()</c> delivers a value for
/// it. When false, <c>pixel</c> is zeroed.
/// </param>
/// <param name="Pixel">
/// Where the terrain draws the coordinate, when <c>found</c> is true.
/// </param>
public readonly partial record struct TerrainScreenPointResult(
    bool Found,
    TerrainScreenPoint Pixel
);

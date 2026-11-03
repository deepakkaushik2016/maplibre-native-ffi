// Generated from the C headers by tools/bindgen. Do not edit.
#nullable enable
namespace Maplibre.NativeFfi;

/// <summary>
/// The height of the terrain at a point.
/// </summary>
/// <remarks>
/// See <c>mln_terrain_elevation</c> in the <see
/// href="https://maplibre.org/maplibre-native-ffi/reference/c/query_8h.html">C API reference</see>.
/// </remarks>
/// <param name="Meters">
/// Meters above sea level, with the style's terrain exaggeration applied.
/// </param>
public readonly partial record struct TerrainElevation(double Meters);

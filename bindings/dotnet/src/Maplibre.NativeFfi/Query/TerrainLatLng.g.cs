// Generated from the C headers by tools/bindgen. Do not edit.
#nullable enable
namespace Maplibre.NativeFfi;

/// <summary>
/// The ground that the terrain draws under a screen point.
/// </summary>
/// <remarks>
/// See <c>mln_terrain_lat_lng</c> in the <see
/// href="https://maplibre.org/maplibre-native-ffi/reference/c/query_8h.html">C API reference</see>.
/// </remarks>
/// <param name="Coordinate">
/// The ground's coordinate, with its longitude within [-180, 180].
/// </param>
public readonly partial record struct TerrainLatLng(LatLng Coordinate);

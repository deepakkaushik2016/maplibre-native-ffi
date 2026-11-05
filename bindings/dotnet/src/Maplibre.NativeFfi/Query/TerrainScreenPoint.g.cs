// Generated from the C headers by tools/bindgen. Do not edit.
#nullable enable
namespace Maplibre.NativeFfi;

/// <summary>
/// Where the terrain draws a point on the ground.
/// </summary>
/// <remarks>
/// See <c>mln_terrain_screen_point</c> in the <see
/// href="https://maplibre.org/maplibre-native-ffi/reference/c/query_8h.html">C API reference</see>.
/// </remarks>
/// <param name="Point">
/// The point, from the top-left corner in logical pixels. It may lie outside
/// the viewport.
/// </param>
/// <param name="Occluded">
/// <para>Whether ground from a loaded DEM tile is drawn in front of the point
/// and hides it, as a ridge hides the valley behind it.</para>
/// <para>False is no proof that nothing hides the point. Ground that no loaded
/// DEM tile covers never hides it, because the terrain draws a stand-in height
/// there until the tile arrives. And a line of sight that the search for the
/// ground doesn't see meet it, such as one that grazes a thin crest, reports
/// the point as visible.</para>
/// </param>
public readonly partial record struct TerrainScreenPoint(ScreenPoint Point, bool Occluded);

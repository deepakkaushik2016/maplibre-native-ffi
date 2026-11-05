// Generated from the C headers by tools/bindgen. Do not edit.
#nullable enable
namespace Maplibre.NativeFfi;

/// <summary>
/// The details of <c>RuntimeEventType.MapTerrainGrabbedGround</c>.
/// </summary>
/// <remarks>
/// See <c>mln_runtime_event_terrain_grabbed_ground</c> in the <see
/// href="https://maplibre.org/maplibre-native-ffi/reference/c/runtime_8h.html">C API reference</see>.
/// </remarks>
/// <param name="ElevationMeters">
/// The grabbed ground's height in meters above sea level, as the terrain draws
/// it, with the style's terrain exaggeration applied.
/// </param>
public readonly partial record struct RuntimeEventTerrainGrabbedGround(double ElevationMeters);

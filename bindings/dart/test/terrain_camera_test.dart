// The terrain options, set and read back through the map's snapshot.
import 'package:maplibre_native_ffi/maplibre_native_ffi.dart';
import 'package:test/test.dart';

import 'support/fixture.dart';

void main() {
  test('terrain options round-trip, and unset ones keep their value', () async {
    final fixture = await openRuntime();
    final map = await fixture.openStyledMap();
    final initial = map.getSnapshot().terrain;
    expect(initial.loadMode, TerrainLoadMode.quality);
    expect(initial.centerClampedToGround, isFalse);

    await expectCommitted(
      map.setTerrainOptions(
        const MapTerrainOptions(
          loadMode: TerrainLoadMode.performance,
          centerClampedToGround: true,
        ),
      ),
    );
    await expectCommitted(
      map.setTerrainOptions(
        const MapTerrainOptions(loadMode: TerrainLoadMode.balanced),
      ),
    );
    final terrain = map.getSnapshot().terrain;
    expect(terrain.loadMode, TerrainLoadMode.balanced);
    expect(terrain.centerClampedToGround, isTrue);

    // The style has no terrain, so anchoring leaves the camera alone.
    await expectCommitted(map.anchorCenterOnTerrain());
  });
}

// The conversions between screen points and the ground that a render
// session's last frame drew.
import 'dart:convert';

import 'package:maplibre_native_ffi/maplibre_native_ffi.dart';
import 'package:test/test.dart';

import 'support/fixture.dart';
import 'support/render.dart';

/// A 32 by 32 Mapbox Terrain-RGB PNG that decodes to 1000 m everywhere, the
/// plateau of the native terrain suites.
final _plateauPng = base64Decode(
  'iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAIAAAD8GO2jAAAAKklEQVR42u3NQQkAAAgEsLN/FsGIpvA'
  'hDPZfqudUBAKBQCAQCAQCwZdgAcFaeEyURzhvAAAAAElFTkSuQmCC',
);

const _plateau = LatLng(latitude: 47.27, longitude: 11.39);

Map<String, Object?> _plateauStyle({double pitch = 0}) => {
  'version': 8,
  'center': [_plateau.longitude, _plateau.latitude],
  'zoom': 10,
  'pitch': pitch,
  'sources': {
    'dem': {
      'type': 'raster-dem',
      'tiles': ['custom://dem/{z}/{x}/{y}.png'],
      'encoding': 'mapbox',
      'tileSize': 256,
      'maxzoom': 12,
    },
  },
  'terrain': {'source': 'dem', 'exaggeration': 1},
  'layers': <Object?>[],
};

/// Attaches a session to a static map of the plateau, seen at [pitch], which
/// renders no frame until it is asked for a still image.
Future<(MapHandle, WorkerSession)> _openPlateau({double pitch = 0}) async {
  final (map, worker, _) = await _openPlateauFixture(pitch: pitch);
  return (map, worker);
}

/// [_openPlateau], with the runtime fixture whose events a test awaits.
Future<(MapHandle, WorkerSession, RuntimeFixture)> _openPlateauFixture({
  double pitch = 0,
}) async {
  final fixture = await openRuntime(
    provider: ResourceProvider(
      callback: (request, handle) => handle.complete(
        request.requestedUrl.contains('/dem/')
            ? ResourceResponse(bytes: _plateauPng)
            : ResourceResponse(status: ResourceResponseStatus.noContent),
      ),
    ),
  );
  final map = await fixture.openMap(
    MapOptions(
      mapMode: MapMode.static,
      initialExtent: LogicalExtent(
        width: renderSize,
        height: renderSize,
        scaleFactor: 1,
      ),
    ),
  );
  await expectCommitted(map.setStyleJson(_plateauStyle(pitch: pitch)));
  return (map, await WorkerSession.attach(map), fixture);
}

void main() {
  test('the ground under a point is drawn back at that point', () async {
    final (map, worker) = await _openPlateau();
    await within(map.requestStillImage(), 'the still image');

    const middle = ScreenPoint(x: renderSize / 2, y: renderSize / 2);
    final ground = await within(
      worker.session.queryLatLngForPixel(middle),
      'the coordinate query',
    );
    expect(ground?.coordinate.latitude, closeTo(_plateau.latitude, 1e-6));
    expect(ground?.coordinate.longitude, closeTo(_plateau.longitude, 1e-6));

    final drawn = await within(
      worker.session.queryPixelForLatLng(ground!.coordinate),
      'the pixel query',
    );
    expect(drawn?.point.x, closeTo(middle.x, 0.01));
    expect(drawn?.point.y, closeTo(middle.y, 0.01));
    expect(drawn?.occluded, isFalse);

    // Half a world away, the view loaded no DEM tile.
    final elsewhere = await within(
      worker.session.queryPixelForLatLng(
        const LatLng(latitude: -33.86, longitude: -168.79),
      ),
      'the pixel query',
    );
    expect(elsewhere, isNull);
  });

  // Seen at pitch 60, a finger below and left of the center touches the
  // plateau 1000 m up, well short of where it would meet sea level.
  test('a pitched finger touches the plateau, not sea level', () async {
    final (map, worker) = await _openPlateau(pitch: 60);
    await within(map.requestStillImage(), 'the still image');

    const finger = ScreenPoint(x: 8, y: 24);
    final ground = await within(
      worker.session.queryLatLngForPixel(finger),
      'the coordinate query',
    );
    final drawn = await within(
      worker.session.queryPixelForLatLng(ground!.coordinate),
      'the pixel query',
    );
    expect(drawn?.point.x, closeTo(finger.x, 0.01));
    expect(drawn?.point.y, closeTo(finger.y, 0.01));

    final projection = MapProjectionHandle.create(map.getSnapshot().view);
    final seaLevel = projection.latLngForPixel(finger);
    projection.close();
    expect(
      (ground.coordinate.latitude - seaLevel.latitude).abs(),
      greaterThan(0.005),
    );
  });

  // A finger goes down on the plateau with an anchored pan that doesn't move,
  // and the next frame finds the height of the ground it grabbed, which comes
  // as an event with the height in its payload.
  test('a grab reports the height of its ground as an event', () async {
    final (map, _, fixture) = await _openPlateauFixture();
    await within(map.requestStillImage(), 'the still image');

    await expectCommitted(
      map.applyCameraDelta(
        const CameraDelta(
          offset: ScreenPoint(),
          anchor: ScreenPoint(x: 8, y: 24),
          gesturePhase: GesturePhase.begin,
        ),
      ),
    );
    await within(map.requestStillImage(), 'the still image after the grab');
    final event = await within(
      fixture.awaitEventType(RuntimeEventType.mapTerrainGrabbedGround),
      'the grabbed ground event',
    );
    final payload = event.payload as RuntimeEventPayloadTerrainGrabbedGround;
    expect(payload.value.elevationMeters, closeTo(1000, 1));
  });

  // Null means the frame drew no ground there. Before the first frame there is
  // no frame to read, so the queries throw rather than completing with null.
  test('a session that never rendered throws invalidState', () async {
    final (_, worker) = await _openPlateau();
    final invalidState = throwsA(
      isA<MaplibreException>().having(
        (error) => error.status,
        'status',
        Status.invalidState,
      ),
    );

    await expectLater(
      worker.session.queryLatLngForPixel(const ScreenPoint(x: 16, y: 16)),
      invalidState,
    );
    await expectLater(
      worker.session.queryPixelForLatLng(_plateau),
      invalidState,
    );
  });
}

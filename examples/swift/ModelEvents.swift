import Foundation
import LadioCore
import LadioCoreML

/// Run off UI/audio threads after measured grid and native descriptor analysis.
func attachModelEvents(track: URL, observations: LadioModelObservations, output: URL) throws {
    let events = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".json")
    defer { try? FileManager.default.removeItem(at: events) }
    try observations.write(to: events)
    let success = track.path.withCString { trackPath in
        events.path.withCString { eventPath in
            output.path.withCString { ladio_apply_model_events(trackPath, eventPath, $0) }
        }
    }
    guard success != 0 else { throw ModelEventFailure.invalidObservations }
}

enum ModelEventFailure: Error { case invalidObservations }

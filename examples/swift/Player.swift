import LadioCore

// Control operations must be serialized by the host. Create/destroy belong off
// the real-time callback; integrate with AVAudioEngine/AudioUnit in the app.
final class LadioSession {
    private let handle: OpaquePointer
    init(path: String) throws {
        guard let pointer = path.withCString({ ladio_player_create($0, 65_536) }) else {
            throw Failure(message: String(cString: ladio_last_error()))
        }
        handle = pointer
    }
    func play() -> Bool { ladio_player_play(handle) != 0 }
    func pause() -> Bool { ladio_player_pause(handle) != 0 }
    func seek(frame: Int64) -> Bool { ladio_player_seek(handle, frame) != 0 }
    func snapshot() -> LadioSnapshot { ladio_player_snapshot(handle) }
    // Caller provides planar buffers. Zero any region beyond returned frames.
    func pull(left: UnsafeMutablePointer<Float>, right: UnsafeMutablePointer<Float>, frames: UInt32) -> UInt32 {
        ladio_player_pull(handle, left, right, frames)
    }
    deinit { ladio_player_destroy(handle) }
    struct Failure: Error { let message: String }
}

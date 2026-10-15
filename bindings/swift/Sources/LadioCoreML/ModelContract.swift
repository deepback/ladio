import Foundation
import CryptoKit

public enum LadioModelRole: String, Codable, Sendable {
    case activity, structure, downbeat, key

    var inputName: String {
        switch self {
        case .activity, .structure: return "MelSpectrogram"
        case .downbeat: return "input"
        case .key: return "audio"
        }
    }
    var outputs: [String] {
        switch self {
        case .activity: return ["vocals", "drums", "bass", "others"]
        case .structure: return ["phrases", "segments", "sections"]
        case .downbeat: return ["output_beats", "output_downbeats", "output_beats_no_downbeats", "output_tempo", "output_bpb"]
        case .key: return ["key_output"]
        }
    }
    func accepts(_ shape: [Int]) -> Bool {
        switch self {
        case .activity, .structure:
            return shape.count == 4 && shape[0] == 1 && (400...6000).contains(shape[1])
                && shape[2] == (self == .activity ? 128 : 256) && shape[3] == 1
        case .downbeat: return shape == [1, 4096, 131]
        case .key: return shape == [1, 661504]
        }
    }
}

public struct LadioTensor: Sendable {
    public let shape: [Int]
    public let values: [Float]
    public init(shape: [Int], values: [Float]) throws {
        var count = 1
        guard !shape.isEmpty, shape.count <= 4 else { throw LadioModelError.invalidTensor }
        for dimension in shape {
            guard dimension > 0, dimension <= 2_000_000,
                  count <= 2_000_000 / dimension else { throw LadioModelError.invalidTensor }
            count *= dimension
        }
        guard count == values.count, values.allSatisfy(\.isFinite) else {
            throw LadioModelError.invalidTensor
        }
        self.shape = shape; self.values = values
    }
}

public enum LadioModelError: Error, Equatable {
    case invalidTensor, invalidManifest, unsafePath, integrity(String)
    case incompatibleModel, incompatibleFrontend, invalidOutput, deadlineExceeded
}

/// Host-supplied compiled models only. No weights or models are package resources.
public struct LadioModelSpec: Codable, Sendable {
    public let id: String
    public let role: LadioModelRole
    public let directory: String
    public let frontend: String
    /// Every file below the .mlmodelc directory, including nested weight blobs.
    public let files: [String: String]

    public init(id: String, role: LadioModelRole, directory: String,
                frontend: String, files: [String: String]) {
        self.id = id; self.role = role; self.directory = directory
        self.frontend = frontend; self.files = files
    }

    public func verifiedURL(relativeTo root: URL) throws -> URL {
        guard !id.isEmpty, !frontend.isEmpty, !files.isEmpty,
              directory.hasSuffix(".mlmodelc") else { throw LadioModelError.invalidManifest }
        let model = try Self.child(directory, of: root)
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: model.path, isDirectory: &isDirectory),
              isDirectory.boolValue else { throw LadioModelError.integrity(directory) }
        // Relative enumeration avoids /var versus /private/var URL aliases on
        // macOS changing the apparent number of path components.
        let enumerator = FileManager.default.enumerator(atPath: model.path)
        var actual = Set<String>()
        while let relative = enumerator?.nextObject() as? String {
            let url = model.appendingPathComponent(relative)
            let properties = try url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
            guard properties.isSymbolicLink != true else { throw LadioModelError.unsafePath }
            if properties.isRegularFile == true {
                actual.insert(relative)
            }
        }
        guard actual == Set(files.keys) else { throw LadioModelError.integrity(directory) }
        for (path, digest) in files {
            guard digest.count == 64, digest.allSatisfy({ "0123456789abcdef".contains($0) }) else {
                throw LadioModelError.invalidManifest
            }
            let url = try Self.child(path, of: model)
            let file = try FileHandle(forReadingFrom: url)
            defer { try? file.close() }
            var hash = SHA256()
            while let block = try file.read(upToCount: 65536), !block.isEmpty { hash.update(data: block) }
            guard hash.finalize().map({ String(format: "%02x", $0) }).joined() == digest else {
                throw LadioModelError.integrity(path)
            }
        }
        return model
    }

    public var identity: String {
        let content = files.keys.sorted().map { "\($0)\u{0}\(files[$0]!)\n" }.joined()
        return SHA256.hash(data: Data(content.utf8)).map { String(format: "%02x", $0) }.joined()
    }

    private static func child(_ path: String, of root: URL) throws -> URL {
        let parts = path.split(separator: "/", omittingEmptySubsequences: false)
        guard root.isFileURL, !path.hasPrefix("/"), !path.contains("\\"),
              !parts.isEmpty, parts.allSatisfy({ !$0.isEmpty && $0 != "." && $0 != ".." }) else {
            throw LadioModelError.unsafePath
        }
        let base = root.standardizedFileURL.resolvingSymlinksInPath()
        let url = base.appendingPathComponent(path).standardizedFileURL.resolvingSymlinksInPath()
        guard url.path.hasPrefix(base.path + "/") else { throw LadioModelError.unsafePath }
        return url
    }
}

public struct LadioCoreMLRegistry: Codable, Sendable {
    public let schema_version: Int
    public let models: [LadioModelSpec]
    public static func load(from url: URL) throws -> Self {
        let registry = try JSONDecoder().decode(Self.self, from: Data(contentsOf: url))
        guard registry.schema_version == 1, !registry.models.isEmpty,
              Set(registry.models.map(\.id)).count == registry.models.count else {
            throw LadioModelError.invalidManifest
        }
        return registry
    }
}

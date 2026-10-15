import XCTest
import CryptoKit
@testable import LadioCoreML

final class ConnectorTests: XCTestCase {
    func testTensorBoundsAndFiniteness() throws {
        XCTAssertThrowsError(try LadioTensor(shape: [1, Int.max], values: []))
        XCTAssertThrowsError(try LadioTensor(shape: [1, 2], values: [.nan, 0]))
        XCTAssertThrowsError(try LadioTensor(shape: [0], values: []))
        XCTAssertThrowsError(try LadioTensor(shape: [2], values: [1]))
        XCTAssertEqual(try LadioTensor(shape: [2], values: [1, 2]).values, [1, 2])
    }

    func testRolesDoNotAcceptWrongFrontendShapes() {
        XCTAssertTrue(LadioModelRole.activity.accepts([1, 400, 128, 1]))
        XCTAssertFalse(LadioModelRole.activity.accepts([1, 400, 256, 1]))
        XCTAssertFalse(LadioModelRole.structure.accepts([1, 6001, 256, 1]))
        XCTAssertFalse(LadioModelRole.downbeat.accepts([1, 4096, 128]))
        XCTAssertTrue(LadioModelRole.key.accepts([1, 661504]))
    }

    func testIntegrityAndEscapeChecks() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let model = root.appendingPathComponent("Fixture.mlmodelc")
        try FileManager.default.createDirectory(at: model, withIntermediateDirectories: true)
        let file = model.appendingPathComponent("weight.bin")
        let content = Data([1, 2, 3])
        try content.write(to: file)
        let hash = SHA256.hash(data: content).map { String(format: "%02x", $0) }.joined()
        func spec(_ directory: String = "Fixture.mlmodelc", _ files: [String: String]? = nil) -> LadioModelSpec {
            .init(id: "fixture", role: .activity, directory: directory,
                  frontend: "test", files: files ?? ["weight.bin": hash])
        }
        XCTAssertEqual(try spec().verifiedURL(relativeTo: root).resolvingSymlinksInPath(), model.resolvingSymlinksInPath())
        XCTAssertThrowsError(try spec("../Fixture.mlmodelc").verifiedURL(relativeTo: root))
        XCTAssertThrowsError(try spec("Fixture.mlmodelc", ["../weight.bin": hash]).verifiedURL(relativeTo: root))
        try Data([1, 2, 4]).write(to: file)
        XCTAssertThrowsError(try spec().verifiedURL(relativeTo: root))
        try content.write(to: file)
        try content.write(to: model.appendingPathComponent("unregistered.bin"))
        XCTAssertThrowsError(try spec().verifiedURL(relativeTo: root))
    }

    func testDeadlineRejectsBeforeLoading() async throws {
        let connector = LadioCoreMLConnector(modelRoot: URL(fileURLWithPath: "/missing"))
        let spec = LadioModelSpec(id: "key", role: .key, directory: "Key.mlmodelc", frontend: "raw", files: [:])
        let tensor = try LadioTensor(shape: [1], values: [0])
        do {
            _ = try await connector.predict(spec: spec, input: tensor, frontend: "raw", deadline: .distantPast)
            XCTFail("Expired job must not load a model")
        } catch { XCTAssertEqual(error as? LadioModelError, .deadlineExceeded) }
    }

    // Explicit local smoke test. No private weights are needed by CI/package.
    func testLocalCoreMLEventInference() async throws {
        guard let registryPath = ProcessInfo.processInfo.environment["LADIO_TEST_COREML_REGISTRY"] else {
            throw XCTSkip("Local model registry not supplied")
        }
        let url = URL(fileURLWithPath: registryPath)
        let registry = try LadioCoreMLRegistry.load(from: url)
        let activity = try XCTUnwrap(registry.models.first { $0.role == .activity })
        let structure = try XCTUnwrap(registry.models.first { $0.role == .structure })
        let connector = LadioCoreMLConnector(modelRoot: url.deletingLastPathComponent(), computeUnits: .cpuOnly)
        let result = try await connector.musicalEvents(activity: [Float](repeating: -80, count: 400 * 128),
            structure: [Float](repeating: -80, count: 400 * 256), frameCount: 400, sourceHash: "fixture-audio",
            activityModel: activity, structureModel: structure, frontend: "magnitude-slaney-A-dB-24k-v1")
        XCTAssertEqual(result.activity["vocals"]?.count, 400)
        XCTAssertEqual(result.structure["phrases"]?.count, 400)
        XCTAssertEqual(result.identity.source_hash, "fixture-audio")
        if let output = ProcessInfo.processInfo.environment["LADIO_TEST_OBSERVATIONS"] {
            try result.write(to: URL(fileURLWithPath: output))
        }
    }
}

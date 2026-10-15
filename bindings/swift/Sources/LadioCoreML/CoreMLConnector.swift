import Foundation
import CoreML

/// Serial inference off the audio/UI threads. Holds at most one loaded model.
/// Cancellation/deadline checks occur between predictions, never inside Core ML.
public actor LadioCoreMLConnector {
    private let root: URL
    private let computeUnits: MLComputeUnits
    private var loaded: (identity: String, role: LadioModelRole, model: MLModel)?

    public init(modelRoot: URL, computeUnits: MLComputeUnits = .cpuAndNeuralEngine) {
        root = modelRoot; self.computeUnits = computeUnits
    }

    public func release() { loaded = nil }

    public func predict(spec: LadioModelSpec, input: LadioTensor, frontend: String,
                        deadline: Date? = nil) throws -> [String: LadioTensor] {
        try check(deadline)
        guard spec.frontend == frontend else { throw LadioModelError.incompatibleFrontend }
        guard spec.role.accepts(input.shape) else { throw LadioModelError.invalidTensor }
        return try autoreleasepool {
            let identity = spec.identity
            if loaded?.identity != identity || loaded?.role != spec.role {
                loaded = nil
                let url = try spec.verifiedURL(relativeTo: root)
                let configuration = MLModelConfiguration()
                configuration.computeUnits = computeUnits
                configuration.modelDisplayName = "Ladio \(spec.role.rawValue) integration"
                let model = try MLModel(contentsOf: url, configuration: configuration)
                let description = model.modelDescription
                guard Set(description.inputDescriptionsByName.keys) == [spec.role.inputName],
                      description.inputDescriptionsByName[spec.role.inputName]?.type == .multiArray,
                      spec.role.outputs.allSatisfy({ description.outputDescriptionsByName[$0]?.type == .multiArray }) else {
                    throw LadioModelError.incompatibleModel
                }
                loaded = (identity, spec.role, model)
            }
            try check(deadline)
            let array = try MLMultiArray(shape: input.shape.map(NSNumber.init), dataType: .float32)
            input.values.withUnsafeBufferPointer { values in
                array.dataPointer.assumingMemoryBound(to: Float.self).update(from: values.baseAddress!, count: values.count)
            }
            let provider = try MLDictionaryFeatureProvider(dictionary: [spec.role.inputName: MLFeatureValue(multiArray: array)])
            let prediction = try loaded!.model.prediction(from: provider)
            try check(deadline)
            var result: [String: LadioTensor] = [:]
            for name in spec.role.outputs {
                guard let output = prediction.featureValue(for: name)?.multiArrayValue,
                      output.count > 0, output.count <= 2_000_000 else { throw LadioModelError.invalidOutput }
                // Core ML outputs may be non-contiguous or FP16; indexed access
                // honours strides/type rather than assuming raw float32 layout.
                let values = (0..<output.count).map { output[$0].floatValue }
                result[name] = try LadioTensor(shape: output.shape.map(\.intValue), values: values)
            }
            return result
        }
    }

    /// Prepared feature arrays use the training frontend named in both specs.
    /// Output JSON goes directly into ladio_apply_model_events from LadioCore.
    /// Beats/tonality are retained from the host's measured track analysis.
    public func musicalEvents(activity: [Float], structure: [Float], frameCount: Int,
                              sourceHash: String, activityModel: LadioModelSpec,
                              structureModel: LadioModelSpec, frontend: String,
                              deadline: Date? = nil) throws -> LadioModelObservations {
        defer { loaded = nil }
        guard (400...12000).contains(frameCount), !sourceHash.isEmpty,
              activity.count == frameCount * 128, structure.count == frameCount * 256,
              activityModel.role == .activity, structureModel.role == .structure,
              activityModel.id != structureModel.id,
              frontend == "magnitude-slaney-A-dB-24k-v1" else { throw LadioModelError.invalidTensor }
        var groups: [String: [String: [Float]]] = [:]
        for (spec, features, bands) in [(activityModel, activity, 128), (structureModel, structure, 256)] {
            var channels = Dictionary(uniqueKeysWithValues: spec.role.outputs.map { ($0, [Float](repeating: 0, count: frameCount)) })
            let starts = frameCount <= 6000 ? [0] : [0, frameCount - 6000]
            for (index, first) in starts.enumerated() {
                try check(deadline)
                let length = min(6000, frameCount - first)
                let tensor = try LadioTensor(shape: [1, length, bands, 1],
                    values: Array(features[(first * bands)..<((first + length) * bands)]))
                let output = try predict(spec: spec, input: tensor, frontend: frontend, deadline: deadline)
                let begin = index == 0 ? 0 : frameCount / 2
                let end = index + 1 == starts.count ? frameCount : frameCount / 2
                for name in spec.role.outputs {
                    guard let row = output[name]?.values, row.count == length,
                          row.allSatisfy({ $0 >= -0.000001 && $0 <= 1.000001 }) else {
                        throw LadioModelError.invalidOutput
                    }
                    for t in begin..<end { channels[name]![t] = min(1, max(0, row[t - first])) }
                }
            }
            groups[spec.role.rawValue] = channels
        }
        return LadioModelObservations(
            identity: .init(source_hash: sourceHash,
                            models: [activityModel.id: activityModel.identity, structureModel.id: structureModel.identity],
                            frontend: frontend),
            activity: groups["activity"]!, structure: groups["structure"]!)
    }

    private func check(_ deadline: Date?) throws {
        try Task.checkCancellation()
        if let deadline, Date() >= deadline { throw LadioModelError.deadlineExceeded }
    }
}

public struct LadioModelObservations: Encodable, Sendable {
    public struct Identity: Codable, Sendable {
        public let source_hash: String
        public let models: [String: String]
        public let frontend: String
    }
    public let identity: Identity
    public let step_seconds: Double = 0.05
    public let frame_center_seconds: Double = 1024.0 / 24000
    public let activity: [String: [Float]]
    public let structure: [String: [Float]]

    public func write(to url: URL) throws {
        // Codable never writes partial JSON if the app is interrupted.
        try JSONEncoder().encode(self).write(to: url, options: .atomic)
    }
}

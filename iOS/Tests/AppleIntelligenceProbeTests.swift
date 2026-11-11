import XCTest
#if canImport(FoundationModels)
import FoundationModels

@available(iOS 26.0, *)
@Generable struct ProbeNextWords {
    @Guide(description: "The three most likely next single words, most likely first. Lowercase unless it must be capitalized.", .count(3))
    var words: [String]
}
#endif

/// Probe: is Apple's on-device model usable here, how fast is a next-word guess, and does it handle Vietnamese.
final class AppleIntelligenceProbeTests: XCTestCase {
    func testProbe() async throws {
        // exploratory: prints what the model says (run by hand). CI machines report the model but can't run it
        try XCTSkipIf(ProcessInfo.processInfo.environment["VIKEY_CI"] != nil, "Apple Intelligence probe: not on CI")
        #if canImport(FoundationModels)
        guard #available(iOS 26.0, *), SystemLanguageModel.default.isAvailable else { print("PROBE unavailable"); return }
        let instructions = """
        You are a phone keyboard's next-word predictor. Given what the user has typed so far, give the 3 words they are most likely to type NEXT. \
        Never repeat the typed text. Each item is one word. Text can be Vietnamese, English or both mixed; continue in the language the sentence is going.
        Examples: "Hôm nay mình đi" -> học, làm, chơi. "see you" -> soon, later, tomorrow. "Cảm ơn" -> bạn, anh, chị.
        """
        let warm = LanguageModelSession(instructions: instructions); warm.prewarm()
        for ctx in ["Hôm nay mình đi", "see you", "Cảm ơn", "Em đang ở công ty, chút nữa", "I'll call you", "meeting lúc 3 giờ", "Anh ơi, tối nay"] {
            let s = LanguageModelSession(instructions: instructions)
            let t0 = Date()
            let r = try await s.respond(to: ctx, generating: ProbeNextWords.self, options: GenerationOptions(sampling: .greedy))
            print(String(format: "PROBE %.0f ms | %@ -> %@", Date().timeIntervalSince(t0) * 1000, ctx, r.content.words.joined(separator: ", ")))
        }
        #endif
    }
}

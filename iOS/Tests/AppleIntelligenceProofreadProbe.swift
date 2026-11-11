import XCTest
#if canImport(FoundationModels)
import FoundationModels

@available(iOS 26.0, *)
@Generable struct Alternatives {
    @Guide(description: "Up to 5 words or short phrases that could replace the selected word in this sentence, best first. Same language as the sentence. Never the selected word itself.", .count(5))
    var options: [String]
}
#endif

/// Probe: Apple Intelligence proofreading and word alternatives (Vietnamese, English, mixed), with timings.
final class AppleIntelligenceProofreadProbe: XCTestCase {
    func testProofread() async throws {
        // exploratory: prints what the model says (run by hand). CI machines report the model but can't run it
        try XCTSkipIf(ProcessInfo.processInfo.environment["VIKEY_CI"] != nil, "Apple Intelligence probe: not on CI")
        #if canImport(FoundationModels)
        guard #available(iOS 26.0, *), SystemLanguageModel.default.isAvailable else { print("PROBE unavailable"); return }
        let fix = "Fix spelling, grammar and punctuation in the user's text. Keep the meaning, the language(s) and the tone. If the text mixes Vietnamese and English, keep the mix. Reply with only the corrected text."
        for t in ["toi di hoc hom qua nhung ma troi mua qua nen toi ve nha som", "He dont know what he want, he go to school everyday", "Em đang ở cty, chút nưa em goi lai cho anh nhe", "Mai mình họp team lúc 3h, nhớ prepare slide nha", "Their going too the meeting tomorow"] {
            let s = LanguageModelSession(instructions: fix)
            let t0 = Date()
            let r = try await s.respond(to: t, options: GenerationOptions(sampling: .greedy))
            print(String(format: "PROOF %.0f ms | %@\n      -> %@", Date().timeIntervalSince(t0) * 1000, t, r.content))
        }
        let alt = "The user selected one word in their sentence and wants other words that fit there."
        for (sentence, word) in [("Hôm nay trời rất đẹp", "đẹp"), ("This plan is really good", "good"), ("Anh ấy làm việc rất chăm chỉ", "chăm chỉ"), ("Mình cần fix cái bug này gấp", "fix")] {
            let s = LanguageModelSession(instructions: alt)
            let t0 = Date()
            let r = try await s.respond(to: "Sentence: \"\(sentence)\". Selected word: \"\(word)\".", generating: Alternatives.self, options: GenerationOptions(sampling: .greedy))
            print(String(format: "ALT %.0f ms | %@ [%@] -> %@", Date().timeIntervalSince(t0) * 1000, sentence, word, r.content.options.joined(separator: ", ")))
        }
        #endif
    }
}

import XCTest

/// Load time of the emoji index: the light part (opening the emoji keyboard) and the search words (built after).
final class EmojiIndexTimingTests: XCTestCase {
    func testLightLoadThenSearchWords() {
        let t0 = CACurrentMediaTime()
        let index = EmojiIndex(bundle: Bundle(for: Self.self))
        let t1 = CACurrentMediaTime()
        index.prepareSearch()
        let t2 = CACurrentMediaTime()
        print(String(format: "emoji index: %d emoji, light %.1f ms, search words %.1f ms", index.all.count, (t1 - t0) * 1000, (t2 - t1) * 1000))
        XCTAssertGreaterThan(index.all.count, 1500)
        XCTAssertEqual(index.search("tim", vietnamese: true, limit: 3).first?.char.isEmpty, false)
        XCTAssertEqual(index.emoji("😂")?.nameEN, "face with tears of joy")
    }
}

/// The next-word table is searched in the mapped bytes (no line index): every sampled line must be found.
final class NextWordsSearchTests: XCTestCase {
    func testSampledLinesAreFound() throws {
        let url = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "NextWords", withExtension: "tsv") ?? Bundle.main.url(forResource: "NextWords", withExtension: "tsv"))
        let lines = try String(contentsOf: url, encoding: .utf8).split(separator: "\n")
        let table = NextWords(bundle: Bundle(for: Self.self))
        var checked = 0
        for (n, line) in lines.enumerated() where n % 97 == 0 || n == 0 || n == lines.count - 1 {
            let cols = line.split(separator: "\t").map(String.init)
            guard cols.count >= 2, !cols[0].contains(" "), cols[0].allSatisfy({ $0.isLetter }) else { continue }
            XCTAssertEqual(table.predict(before: cols[0] + " ").first, cols[1], "context '\(cols[0])'")
            checked += 1
        }
        XCTAssertGreaterThan(checked, 200)
        XCTAssertTrue(table.predict(before: "zzzzqqq ").isEmpty)
    }
}

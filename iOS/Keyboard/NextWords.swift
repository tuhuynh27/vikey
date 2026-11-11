import Foundation

/// Next-word table: "context<TAB>w1<TAB>w2<TAB>w3" lines, sorted by context bytes (Tools/predict/pack.py from
/// vikey-data's counts of 2- and 3-word sequences). Memory-mapped and binary-searched: no load time, little memory.
final class NextWords {
    static let shared = NextWords()
    private let data: Data?

    init(bundle: Bundle = .main) {
        let url = bundle.url(forResource: "NextWords", withExtension: "tsv") ?? Bundle(for: NextWords.self).url(forResource: "NextWords", withExtension: "tsv")
        // no line index: the search finds line starts in the mapped bytes, so opening the keyboard neither reads
        // the whole 4.7 MB file nor keeps ~190k offsets (keyboard memory, Tony)
        data = url.flatMap { try? Data(contentsOf: $0, options: .alwaysMapped) }
    }

    var isEmpty: Bool { data?.isEmpty ?? true }

    /// Up to 3 next words after the last one or two words of `before` (2-word context first).
    func predict(before: String) -> [String] {
        let words = Self.lastWords(before)
        guard !words.isEmpty else { return [] }
        var out: [String] = []
        if words.count == 2 { out = lookup(words.joined(separator: " ")) }
        if out.count < 3, let last = words.last {
            for w in lookup(last) where !out.contains(w) && out.count < 3 { out.append(w) }
        }
        return out
    }

    /// The last two words of the text if the cursor sits after a space (lowercase; a sentence end stops it).
    static func lastWords(_ before: String) -> [String] {
        guard before.hasSuffix(" ") else { return [] }
        var words: [String] = [], cur = ""
        for ch in before.dropLast().reversed() {
            if ch.isLetter { cur.insert(ch, at: cur.startIndex); continue }
            if !cur.isEmpty { words.insert(cur.lowercased(), at: 0); cur = "" }
            if words.count == 2 || ch != " " { break }
        }
        if !cur.isEmpty && words.count < 2 { words.insert(cur.lowercased(), at: 0) }
        return Array(words.suffix(2))
    }

    private func lookup(_ key: String) -> [String] {
        guard let d = data, !d.isEmpty else { return [] }
        let k = Array(key.precomposedStringWithCanonicalMapping.utf8)
        return d.withUnsafeBytes { (p: UnsafeRawBufferPointer) -> [String] in
            let b = p.bindMemory(to: UInt8.self)
            func compare(_ start: Int) -> Int {   // the line's context vs the key
                var i = start, j = 0
                while i < b.count, b[i] != 9, b[i] != 10, j < k.count {
                    if b[i] != k[j] { return b[i] < k[j] ? -1 : 1 }
                    i += 1; j += 1
                }
                let lineEnded = i >= b.count || b[i] == 9 || b[i] == 10
                if lineEnded && j == k.count { return 0 }
                return lineEnded ? -1 : 1
            }
            func lineEnd(_ start: Int) -> Int { var e = start; while e < b.count && b[e] != 10 { e += 1 }; return e }
            // binary search over bytes: [lo, hi) holds whole lines; a probe steps back to its line's start
            var lo = 0, hi = b.count
            while lo < hi {
                var s = (lo + hi) / 2
                while s > lo && b[s - 1] != 10 { s -= 1 }
                let c = compare(s), end = lineEnd(s)
                if c == 0 {
                    var i = s
                    while i < end && b[i] != 9 { i += 1 }
                    guard i < end else { return [] }
                    let rest = String(decoding: UnsafeBufferPointer(rebasing: b[(i + 1)..<end]), as: UTF8.self)
                    return rest.split(separator: "\t").map(String.init)
                }
                if c < 0 { lo = end + 1 } else { hi = s }
            }
            return []
        }
    }
}

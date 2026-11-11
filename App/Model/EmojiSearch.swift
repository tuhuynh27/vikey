import Foundation

/// Emoji data + search for the emoji picker (Mac panel and iOS keyboard), shared by both apps.
/// Search works in English and Vietnamese, and on the raw keys: "horse" typed in Vietnamese mode
/// (shown as "hoé") still finds 🐴, and "nguwaj" / "ngựa" / "ngua" all find it too.
struct Emoji: Hashable {
    let char: String
    let group: Int          // 0 smileys … 9 flags (Unicode groups, 2 = components is left out)
    let version: Double     // Unicode emoji version
    let nameEN: String, nameVI: String
    let skins: [String]     // light … dark variants, empty when it has none
    let popularity: Int     // 0-10, breaks ties
    fileprivate let texts: [(text: String, weight: Int)]   // names and keywords as given; searched once folded (prepareSearch)

    func withSkin(_ tone: Int) -> String { tone > 0 && skins.count == 5 ? skins[tone - 1] : char }
    static func == (a: Emoji, b: Emoji) -> Bool { a.char == b.char }
    func hash(into h: inout Hasher) { h.combine(char) }
}

fileprivate struct Term {   // weight: name 3, keyword 2
    let folded: String, words: [String], exactWords: Set<String>, weight: Int
    init(_ text: String, weight: Int) {
        folded = EmojiIndex.fold(text); words = folded.split(separator: " ").map(String.init)
        exactWords = Set(text.lowercased().split(separator: " ").map(String.init)); self.weight = weight
    }
}

final class EmojiIndex {
    static let shared = EmojiIndex()
    let all: [Emoji]
    /// The folded search words of each emoji (same order as `all`), built on the first search or by `prepareSearch()`:
    /// the bulk of the index, so browsing never pays for it (iOS keyboard memory and opening time, Tony).
    private var termTable: [[Term]]?
    private let position: [String: Int]   // character → index in `all`
    private let lock = NSLock()

    init(bundle: Bundle = .main) {
        guard let url = bundle.url(forResource: "Emoji", withExtension: "json") ?? Bundle(for: EmojiIndex.self).url(forResource: "Emoji", withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let rows = try? JSONSerialization.jsonObject(with: data) as? [[Any]] else { all = []; position = [:]; return }
        all = rows.compactMap { r in
            guard r.count >= 8, let c = r[0] as? String, let g = r[1] as? Int else { return nil }
            let v = (r[2] as? NSNumber)?.doubleValue ?? 0
            let en = r[3] as? String ?? "", vi = r[4] as? String ?? ""
            var texts: [(text: String, weight: Int)] = []
            for s in [en, vi] where !s.isEmpty { texts.append((s, 3)) }
            for k in ((r[5] as? String ?? "") + " | " + (r[6] as? String ?? "")).components(separatedBy: " | ") where !k.isEmpty {
                texts.append((k, 2))
            }
            // emoticons (":D", "=))", "^_^") rank like names: typing one exactly puts its emoji first (Tony)
            for t in (r.count > 9 ? (r[9] as? String ?? "") : "").split(separator: " ") { texts.append((String(t), 3)) }
            return Emoji(char: c, group: g, version: v, nameEN: en, nameVI: vi, skins: r[7] as? [String] ?? [], popularity: r.count > 8 ? (r[8] as? Int ?? 0) : 0, texts: texts)
        }
        position = Dictionary(all.enumerated().map { ($1.char, $0) }, uniquingKeysWith: { a, _ in a })
    }

    /// Builds the search words (fold, split) for every emoji. Safe from any thread; call it ahead of a search box.
    func prepareSearch() {
        lock.lock(); defer { lock.unlock() }
        guard termTable == nil else { return }
        termTable = all.map { e in e.texts.map { Term($0.text, weight: $0.weight) } }
    }

    /// The emoji for a character (Frequently Used is stored as characters).
    func emoji(_ char: String) -> Emoji? { position[char].map { all[$0] }
    }

    /// Best matches first. `raw` is the keys as typed; the Telex reading of the same keys is searched too,
    /// and every comparison also runs without accents. `vietnamese` is kept for callers (display only).
    func search(_ raw: String, vietnamese: Bool, limit: Int = 200, among list: [Emoji]? = nil) -> [Emoji] {
        let q = raw.lowercased().trimmingCharacters(in: .whitespaces)
        guard !q.isEmpty else { return [] }
        var variants = [q]
        let t = Telex.convert(q); if t != q { variants.append(t) }   // in English mode too: "nguwaj" still finds 🐎
        let queries = variants.map { v in v.split(separator: " ").map { (exact: String($0), folded: EmojiIndex.fold(String($0))) } }
        var scored: [(Int, Int, Emoji)] = []
        prepareSearch()
        guard let table = termTable else { return [] }
        for (i, e) in (list ?? all).enumerated() {
            guard let at = list == nil ? i : position[e.char] else { continue }
            var best = 0
            for words in queries { best = max(best, score(words, table[at])) }
            if best > 0 { scored.append((best * 2 + e.popularity, i, e)) }
        }
        scored.sort { $0.0 != $1.0 ? $0.0 > $1.0 : $0.1 < $1.1 }
        return scored.prefix(limit).map { $0.2 }
    }

    /// Every query word must start some word of one term. A hit in the emoji's name beats one in its keywords,
    /// a whole word beats a prefix, and accents count when given: "tiền" ranks 💰 over 🧚 (tiên), "tim" ❤️ over 💜 (tím).
    private func score(_ words: [(exact: String, folded: String)], _ terms: [Term]) -> Int {
        var total = 0
        let whole = words.map(\.folded).joined(separator: " ")
        for w in words {
            var best = 0
            let accented = w.exact != w.folded
            for t in terms {
                let name = t.weight == 3
                var s = 0
                if t.folded == whole { s = name ? 100 : 65 }
                else {
                    for tw in t.words where tw.hasPrefix(w.folded) {
                        s = max(s, tw.count == w.folded.count ? (name ? 70 : 55) : (name ? 45 : 30))
                        if tw.count == w.folded.count { break }
                    }
                }
                guard s > 0 else { continue }
                if t.exactWords.contains(w.exact) { s += 10 } else if accented { s -= 40 }
                if name { s += max(0, 6 - t.words.count) }   // short names are about the word: ❤️ "trái tim màu đỏ" over 🥰
                best = max(best, s)
            }
            if best <= 0 { return 0 }
            total += best
        }
        return total / words.count
    }

    /// Lowercase, no accents, đ → d.
    static func fold(_ s: String) -> String {
        s.lowercased().folding(options: [.diacriticInsensitive, .widthInsensitive], locale: nil).replacingOccurrences(of: "đ", with: "d")
    }
}

/// A small Telex reader for search text: each word's keys become the Vietnamese letters they type
/// ("nguwaj" → "ngựa", "dduwowngf" → "đường"). The typing engine itself is not used here, since
/// this runs on text the picker owns.
enum Telex {
    static func convert(_ s: String) -> String { s.split(separator: " ", omittingEmptySubsequences: false).map { word(String($0)) }.joined(separator: " ") }

    private static let tones: [Character: Int] = ["s": 1, "f": 2, "r": 3, "x": 4, "j": 5]
    private static let marks: [Character: [Int: Character]] = [   // base vowel -> tone -> char
        "a": [1: "á", 2: "à", 3: "ả", 4: "ã", 5: "ạ"], "ă": [1: "ắ", 2: "ằ", 3: "ẳ", 4: "ẵ", 5: "ặ"], "â": [1: "ấ", 2: "ầ", 3: "ẩ", 4: "ẫ", 5: "ậ"],
        "e": [1: "é", 2: "è", 3: "ẻ", 4: "ẽ", 5: "ẹ"], "ê": [1: "ế", 2: "ề", 3: "ể", 4: "ễ", 5: "ệ"], "i": [1: "í", 2: "ì", 3: "ỉ", 4: "ĩ", 5: "ị"],
        "o": [1: "ó", 2: "ò", 3: "ỏ", 4: "õ", 5: "ọ"], "ô": [1: "ố", 2: "ồ", 3: "ổ", 4: "ỗ", 5: "ộ"], "ơ": [1: "ớ", 2: "ờ", 3: "ở", 4: "ỡ", 5: "ợ"],
        "u": [1: "ú", 2: "ù", 3: "ủ", 4: "ũ", 5: "ụ"], "ư": [1: "ứ", 2: "ừ", 3: "ử", 4: "ữ", 5: "ự"], "y": [1: "ý", 2: "ỳ", 3: "ỷ", 4: "ỹ", 5: "ỵ"]]
    private static let vowels: Set<Character> = ["a", "ă", "â", "e", "ê", "i", "o", "ô", "ơ", "u", "ư", "y"]

    static func word(_ keys: String) -> String {
        var out: [Character] = [], tone = 0, toneKey: Character? = nil
        for k in keys.lowercased() {
            let last = out.last
            switch k {
            case "d" where last == "d": out[out.count - 1] = "đ"
            case "a" where last == "a": out[out.count - 1] = "â"
            case "e" where last == "e": out[out.count - 1] = "ê"
            case "o" where last == "o": out[out.count - 1] = "ô"
            case "w":
                // ươ (uow / uwow), ă, ơ, ư; a lone w is ư
                if let i = out.lastIndex(where: { "aou".contains($0) }), out[(i + 1)...].allSatisfy({ !vowels.contains($0) }) || i == out.count - 1 {
                    let c = out[i]
                    out[i] = c == "a" ? "ă" : c == "o" ? "ơ" : "ư"
                    if c == "o", i > 0, out[i - 1] == "u" { out[i - 1] = "ư" }
                    if c == "u", i + 1 < out.count, out[i + 1] == "o" { out[i + 1] = "ơ" }
                } else { out.append("ư") }
            case "z": tone = 0; toneKey = nil
            default:
                if let t = tones[k], out.contains(where: { vowels.contains($0) }) {
                    if toneKey == k { tone = 0; toneKey = nil; out.append(k) }   // the same tone key twice: undo, keep the letter
                    else { tone = t; toneKey = k }
                } else { out.append(k) }
            }
        }
        guard tone > 0, let i = toneIndex(out) else { return String(out) }
        out[i] = marks[out[i]]?[tone] ?? out[i]
        return String(out)
    }

    /// Where the tone mark goes (modern style: hoà, thuý).
    private static func toneIndex(_ w: [Character]) -> Int? {
        var start = 0
        if w.count > 2, w[0] == "q", w[1] == "u" { start = 2 }                       // qu-
        else if w.count > 2, w[0] == "g", w[1] == "i", vowels.contains(w[2]) { start = 2 }   // gi-
        let idx = w.indices.filter { $0 >= start && vowels.contains(w[$0]) }
        guard let first = idx.first, let lastV = idx.last else { return nil }
        let nucleus = Array(idx.drop { $0 < first }.prefix { idx.contains($0) })
        if let m = nucleus.last(where: { "ăâêôơư".contains(w[$0]) }) { return m }
        if nucleus.count == 1 { return first }
        let endsInConsonant = lastV < w.count - 1
        if endsInConsonant || nucleus.count == 3 { return nucleus[1] }
        let pair = String([w[nucleus[0]], w[nucleus[1]]])
        return ["oa", "oe", "uy"].contains(pair) ? nucleus[1] : nucleus[0]
    }
}

/// Frequently used emoji, shared by the Mac panel and the iOS keyboard: each pick adds 1 and every older
/// score fades a little, so a favorite stays on top but this week's emoji still climbs.
enum EmojiUsage {
    static let key = "EmojiUsage"
    /// Until enough picks exist, the block is filled with the emoji people use most.
    static let starter = ["😂", "❤️", "🥰", "😍", "😭", "👍", "🙏", "😊", "🔥", "😘", "🤣", "🥺", "😅", "🎉", "💕", "😁", "🙂", "😉",
                          "🤔", "👏", "😎", "💪", "✨", "😢", "🙌", "💯", "😆", "🤗", "😴", "👌"]

    static func record(_ char: String, in d: UserDefaults) {
        var s = d.dictionary(forKey: key) as? [String: Double] ?? [:]
        for k in s.keys { s[k]! *= 0.97 }
        s[char, default: 0] += 1
        if s.count > 80 { for (k, _) in s.sorted(by: { $0.value < $1.value }).prefix(s.count - 80) { s[k] = nil } }
        d.set(s, forKey: key)
    }

    static func frequent(in d: UserDefaults, count n: Int) -> [String] {
        let s = d.dictionary(forKey: key) as? [String: Double] ?? [:]
        var list = s.sorted { $0.value != $1.value ? $0.value > $1.value : $0.key < $1.key }.map(\.key)
        for e in starter where list.count < n && !list.contains(e) { list.append(e) }
        return Array(list.prefix(n))
    }
}

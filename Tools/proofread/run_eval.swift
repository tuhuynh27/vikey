// Runs eval.jsonl through the keyboard's own Proofread code (iOS/Keyboard/AIAssist.swift: same prompt, same
// safety filter) on this Mac's Apple Intelligence model, and writes one result per line for score.py.
// Build + run: see run.sh
import Foundation

struct Item: Decodable { let id: String; let typed: String }
struct Out: Encodable { let id: String; let output: String? }

@main struct Runner {
    static func main() async {
        let args = CommandLine.arguments
        let path = args.count > 1 ? args[1] : "eval.jsonl"
        let outPath = args.count > 2 ? args[2] : "results.jsonl"
        guard AIAssist.available else { print("Apple Intelligence is not available on this Mac"); exit(1) }
        let lines = (try? String(contentsOfFile: path, encoding: .utf8))?.split(separator: "\n") ?? []
        let items = lines.compactMap { try? JSONDecoder().decode(Item.self, from: Data($0.utf8)) }
        var results = [Out](repeating: Out(id: "", output: nil), count: items.count)
        let started = Date()
        await withTaskGroup(of: (Int, String?).self) { group in
            var next = 0
            func add() { let i = next; next += 1; group.addTask {
                // PROOFREAD_PASSES=2: proofread the answer again (an experiment; the keyboard does one pass)
                var t: String? = await AIAssist.proofread(items[i].typed)
                if ProcessInfo.processInfo.environment["PROOFREAD_PASSES"] == "2", let first = t { t = await AIAssist.proofread(first) ?? first }
                return (i, t)
            } }
            for _ in 0..<min(4, items.count) { add() }
            var done = 0
            for await (i, out) in group {
                results[i] = Out(id: items[i].id, output: out); done += 1
                if done % 50 == 0 { print("\(done)/\(items.count)") }
                if next < items.count { add() }
            }
        }
        let enc = JSONEncoder(); enc.outputFormatting = [.withoutEscapingSlashes]
        let text = results.map { String(data: try! enc.encode($0), encoding: .utf8)! }.joined(separator: "\n") + "\n"
        try? text.write(toFile: outPath, atomically: true, encoding: .utf8)
        print(String(format: "%d sentences in %.0f s", items.count, Date().timeIntervalSince(started)))
    }
}

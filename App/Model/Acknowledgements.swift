import Foundation

/// Credits for the code and the data Vikey is built from (shared by the Mac and iOS apps).
/// The same list is in ACKNOWLEDGEMENTS.md.
struct Credit: Identifiable {
    let name: String, by: String, license: String, url: String
    var id: String { name }
}

enum Acknowledgements {
    /// Code Vikey uses.
    static let code: [Credit] = [
        Credit(name: "OpenKey", by: "Tuyen Mai", license: "GPL-3.0", url: "https://github.com/tuyenvm/OpenKey"),
    ]
    /// Text Vikey's word statistics are counted from (only counts are built in, no text).
    static let wordData: [Credit] = [
        Credit(name: "FrequencyWords (OpenSubtitles 2018)", by: "Hermit Dave", license: "CC BY-SA 4.0", url: "https://github.com/hermitdave/FrequencyWords"),
        Credit(name: "FineWeb-2 and FineWeb", by: "Hugging Face", license: "ODC-By 1.0", url: "https://huggingface.co/datasets/HuggingFaceFW/fineweb-2"),
        Credit(name: "HPLT v2", by: "High Performance Language Technologies", license: "CC0", url: "https://hplt-project.org"),
        Credit(name: "MADLAD-400", by: "Google", license: "ODC-By 1.0", url: "https://huggingface.co/datasets/allenai/MADLAD-400"),
        Credit(name: "Reddit comments", by: "tensorshield/reddit_dataset_157", license: "MIT", url: "https://huggingface.co/datasets/tensorshield/reddit_dataset_157"),
        Credit(name: "Vietnamese forums and reader comments", by: "VnExpress, Tuổi Trẻ, Tinhte, Otofun, Dạy Nhau Học", license: "word statistics", url: "https://vikey.app/acknowledgements"),
        Credit(name: "Emoji names and keywords (CLDR)", by: "Unicode, Inc.", license: "Unicode License v3", url: "https://cldr.unicode.org"),
        Credit(name: "Emojibase", by: "Miles Johnson", license: "MIT", url: "https://emojibase.dev"),
    ]
    /// Data used only to measure and tune accuracy (not in the app).
    static let testData: [Credit] = [
        Credit(name: "ViLexNorm", by: "Nguyen et al., EACL 2024", license: "CC BY-NC-SA 4.0", url: "https://github.com/ngxtnhi/ViLexNorm"),
        Credit(name: "UIT-ViCTSD", by: "UIT NLP Group", license: "research use", url: "https://huggingface.co/datasets/tarudesu/ViCTSD"),
        Credit(name: "VietMix", by: "EACL 2026", license: "research use", url: "https://huggingface.co/datasets/razent/vietmix"),
        Credit(name: "Tap typing with touch sensing images", by: "Google Research", license: "CC BY 4.0", url: "https://github.com/google-research-datasets/tap-typing-with-touch-sensing-images"),
        Credit(name: "How We Type: Mobile", by: "Aalto University", license: "research use", url: "https://userinterfaces.aalto.fi/how-we-type-mobile"),
        Credit(name: "GitHub Typo Corpus", by: "Hagiwara & Mita, LREC 2020", license: "research use", url: "https://github.com/mhagiwara/github-typo-corpus"),
        Credit(name: "Birkbeck spelling error corpus", by: "Roger Mitton", license: "free to use", url: "https://www.dcs.bbk.ac.uk/~roger/corpora.html"),
        Credit(name: "Lists of common misspellings", by: "Wikipedia", license: "CC BY-SA 4.0", url: "https://en.wikipedia.org/wiki/Wikipedia:Lists_of_common_misspellings"),
    ]
}

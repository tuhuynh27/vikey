import SwiftUI

/// Vikey's windows are plain NSWindows hosting SwiftUI, so they can be opened from anywhere
/// (menu bar, reopening the app from Finder, onboarding) and always come to the front.
@MainActor
final class HostedWindow<Content: View> {
    private var window: NSWindow?
    private var host: NSHostingController<AnyView>?
    private let title: String
    private let size: NSSize?
    private let resizable: Bool
    var hideTitle = false
    private let content: () -> Content

    /// size: nil = fit the SwiftUI content exactly.
    init(title: String, size: NSSize?, resizable: Bool, content: @escaping () -> Content) {
        self.title = title
        self.size = size
        self.resizable = resizable
        self.content = content
    }

    func show() {
        if window == nil {
            let host = NSHostingController(rootView: AnyView(content().environmentObject(AppState.shared)))
            host.sizingOptions = []   // size is set once below; live content-size tracking loops in AppKit's constraint pass
            self.host = host
            let w = NSWindow(contentViewController: host)
            w.title = title
            w.styleMask = resizable ? [.titled, .closable, .miniaturizable, .resizable] : [.titled, .closable, .miniaturizable]
            w.isReleasedWhenClosed = false
            if hideTitle {
                w.titleVisibility = .hidden
                w.titlebarAppearsTransparent = true
            }
            // Unbounded proposal measures natural content width; works because current content-sized views
            // (MainView at 460pt, AcknowledgementsView at 520pt) declare explicit fixed frame widths.
            let measured = host.sizeThatFits(in: NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude))
            let targetWidth = size?.width ?? (measured.width > 0 ? measured.width : 460)
            let fit = host.sizeThatFits(in: NSSize(width: targetWidth, height: CGFloat.greatestFiniteMagnitude))
            let initialSize = size ?? NSSize(width: targetWidth, height: fit.height > 0 ? fit.height : 460)
            w.setContentSize(initialSize)
            window = w
            Self.centerExactly(w)
        }
        let first = window?.isVisible == false && !centered
        WindowFocus.bringForward()
        window?.makeKeyAndOrderFront(nil)
        if first, let w = window, let host = self.host {
            // SwiftUI settles the content size after the window is on screen; measure and center again then
            // (Tony: the main window still opened off-center)
            centered = true
            DispatchQueue.main.async { [size] in
                if size == nil {
                    let measured = host.sizeThatFits(in: NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude))
                    let targetWidth = measured.width > 0 ? measured.width : 460
                    let fit = host.sizeThatFits(in: NSSize(width: targetWidth, height: CGFloat.greatestFiniteMagnitude))
                    let current = w.contentLayoutRect.size
                    if fit.width > 0, fit.height > 0, (abs(fit.width - current.width) > 1 || abs(fit.height - current.height) > 1) {
                        w.setContentSize(fit)
                    }
                }
                w.centerExactly()
            }
        }
    }
    private var centered = false

    /// After the content changed height (a card folded, tab switched): resize, keeping the top edge in place.
    func fitToContent(height: CGFloat? = nil) {
        guard let w = window, let host = self.host else { return }
        let measured = host.sizeThatFits(in: NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude))
        let targetWidth = size?.width ?? (measured.width > 0 ? measured.width : (w.contentLayoutRect.width > 0 ? w.contentLayoutRect.width : 460))
        let h = height ?? host.sizeThatFits(in: NSSize(width: targetWidth, height: CGFloat.greatestFiniteMagnitude)).height
        guard h > 0, abs(h - w.contentLayoutRect.height) > 1 else { return }
        let content = NSRect(x: 0, y: 0, width: targetWidth, height: h)
        var frame = w.frame
        var newSize = w.frameRect(forContentRect: content).size
        let v = (w.screen ?? NSScreen.main)?.visibleFrame
        if let v { newSize.height = min(newSize.height, v.height) }   // no taller than the screen: the content scrolls (#51)
        frame.origin.y += frame.height - newSize.height
        if let v { frame.origin.y = max(frame.origin.y, v.minY) }   // grown past the bottom of the screen: move up
        frame.size = newSize
        w.setFrame(frame, display: true, animate: true)
    }

    func close() { window?.close() }

    private static func centerExactly(_ w: NSWindow) { w.centerExactly() }
}

@MainActor
enum Windows {
    static let main: HostedWindow<MainView> = {
        let w = HostedWindow(title: "Vikey", size: nil, resizable: false) { MainView() }
        w.hideTitle = true
        return w
    }()
    static let history = HostedWindow(title: "Nhật ký gõ", size: NSSize(width: 780, height: 640), resizable: true) { TypingLogView() }
    static let acknowledgements = HostedWindow(title: "Ghi nhận", size: nil, resizable: false) { AcknowledgementsView() }
}

extension NSWindow {
    /// NSWindow.center() puts a window a third of the way down; this is the true middle of the screen it's on.
    func centerExactly() {
        // the screen with the mouse (where the user is looking), then the window's own, then the main one
        let mouse = NSScreen.screens.first { NSMouseInRect(NSEvent.mouseLocation, $0.frame, false) }
        guard let screen = mouse ?? screen ?? NSScreen.main else { center(); return }
        let v = screen.visibleFrame, f = frame
        let h = min(f.height, v.height)   // no taller than the screen (#51)
        setFrame(NSRect(x: (v.midX - f.width / 2).rounded(), y: (v.midY - h / 2).rounded(), width: f.width, height: h), display: false)
    }
}

/// A menu bar app has no Dock icon, so its windows can open behind others. While a window is
/// open, Vikey becomes a regular app (Dock icon, ⌘Tab) and goes back to menu-bar-only when the last one closes.
enum WindowFocus {
    private static var observer: Any?

    static func bringForward() {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        if observer == nil {
            observer = NotificationCenter.default.addObserver(forName: NSWindow.willCloseNotification, object: nil, queue: .main) { note in
                DispatchQueue.main.async {
                    //panels count too: the About panel can't become main, and closing the menu bar menu must not
                    //turn Vikey back into a menu-bar-only app under it (it vanished right after opening)
                    let open = NSApp.windows.contains { $0.isVisible && $0 !== note.object as? NSWindow &&
                        ($0.canBecomeMain || ($0 is NSPanel && $0.styleMask.contains(.titled))) }
                    if !open { NSApp.setActivationPolicy(.accessory) }
                }
            }
        }
    }
}

/// The standard macOS About window, with the author and the OpenKey credit.
enum AboutPanel {
    @MainActor static func show() {
        WindowFocus.bringForward()
        let credits = NSMutableAttributedString()
        let center = NSMutableParagraphStyle()
        center.alignment = .center
        let base: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 11), .foregroundColor: NSColor.secondaryLabelColor, .paragraphStyle: center]
        func add(_ text: String, link: String? = nil) {
            var attrs = base
            if let link { attrs[.link] = URL(string: link) }
            credits.append(NSAttributedString(string: text, attributes: attrs))
        }
        add("Tony Dinh\n")
        add("Dựa trên ")
        add("OpenKey", link: "https://github.com/tuyenvm/OpenKey")
        add(" của Tuyen Mai\nMã nguồn mở theo giấy phép GPL-3.0\n\n")
        add("vikey.app", link: "https://vikey.app")
        add("  ·  ")
        add("GitHub", link: "https://github.com/tonyminibot/Vikey")
        add("  ·  ")
        add("Ghi nhận", link: "https://vikey.app/acknowledgements")
        NSApp.orderFrontStandardAboutPanel(options: [.credits: credits, .applicationName: "Vikey"])
    }
}

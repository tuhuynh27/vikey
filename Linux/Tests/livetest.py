#!/usr/bin/env python3
"""Live typing test on Linux: types each case into a GTK text entry (X11/XWayland) with xdotool while the
Vikey IBus engine is active, reads the text back, compares. Cases: Windows/Tests/cases.tsv (shared with Windows)
then Linux/Tests/cases-linux.tsv (the Mac core's later decisions and the emoji command).
usage: GDK_BACKEND=x11 python3 Linux/Tests/livetest.py [cases.tsv ...]
needs: xdotool, ibus running with vikey as the current engine (ibus engine vikey), python3-gi."""
import gi, sys, os, subprocess, time, json
gi.require_version('Gtk', '4.0'); from gi.repository import Gtk, GLib
HERE = os.path.dirname(os.path.abspath(__file__))
cases = sys.argv[1:] or [os.path.join(HERE, '..', '..', 'Windows', 'Tests', 'cases.tsv'), os.path.join(HERE, 'cases-linux.tsv')]
SET = os.path.expanduser('~/.config/vikey/settings.json')
CMD = os.path.expanduser('~/.config/vikey/command')
def set_lang(vi):
    os.makedirs(os.path.dirname(CMD), exist_ok=True)
    open(CMD + '.tmp', 'w').write('lang %s quiet\n' % ('vi' if vi else 'en')); os.replace(CMD + '.tmp', CMD)   # the engine consumes the command file
    for _ in range(40):
        time.sleep(0.05)
        if not os.path.exists(CMD): break
    time.sleep(0.15)
def xdo(*a): subprocess.run(['xdotool', *a], check=False)
subprocess.run(['ibus', 'engine', 'vikey'], check=False); time.sleep(0.8)   # IBus drops the global engine when the last client goes away
class App(Gtk.Application):
    def __init__(self): super().__init__(application_id='app.vikey.LiveTest'); self.results = []
    def do_activate(self):
        self.win = Gtk.ApplicationWindow(application=self, title='VikeyLiveTest'); self.win.set_default_size(600, 120)
        self.entry = Gtk.Entry(); self.win.set_child(self.entry); self.win.present()
        GLib.timeout_add(800, self.run_cases)
    def run_cases(self):
        xdo('search', '--sync', '--name', 'VikeyLiveTest', 'windowraise'); time.sleep(0.3)
        xdo('search', '--sync', '--name', 'VikeyLiveTest', 'mousemove', '--window', '%1', '300', '60', 'click', '1'); time.sleep(0.5)   # a real click gives the window the focus under mutter
        xdo('type', '--delay', '30', 'ok'); time.sleep(0.4)
        while GLib.MainContext.default().iteration(False): pass
        print('smoke: entry has', repr(self.entry.get_text()), 'focus', self.entry.has_focus(), flush=True)
        total = fails = 0
        for line in (l for c in cases for l in open(c, encoding='utf-8')):
            if not line.strip() or line.startswith('#'): continue
            name, start, keys, expected = line.rstrip('\n').split('\t')[:4]
            total += 1
            self.entry.set_text(''); self.entry.grab_focus()
            xdo('key', '--clearmodifiers', 'Escape'); time.sleep(0.15)
            while GLib.MainContext.default().iteration(False): pass     # deliver the Esc to IBus now (the engine must see it before the language command)
            time.sleep(0.3)
            set_lang(start == 'vi')                                      # a fresh session, so the first word is a whole word
            # xdotool cannot type non-ASCII through IBus (its keymap trick is undone before the engine forwards the key): type the Telex keys instead
            keys = keys.replace('tô thí', 'too this')
            i = 0
            while i < len(keys):
                c = keys[i]
                if c == '\\' and i + 1 < len(keys): d = keys[i+1]; xdo('key', {'b': 'BackSpace', 'n': 'Return', 't': 'Tab'}.get(d, d)); i += 2; continue
                if keys.startswith('{cmd:', i):   # a command to the engine (e.g. the emoji picker's choice)
                    time.sleep(0.4)
                    while GLib.MainContext.default().iteration(False): pass   # the keys typed so far reach the engine first
                    time.sleep(0.3)
                    e = keys.index('}', i); open(CMD + '.tmp', 'w').write(keys[i+5:e] + '\n'); os.replace(CMD + '.tmp', CMD); time.sleep(0.6); i = e + 1; continue
                if c == '{':
                    e = keys.index('}', i); cmd = keys[i+1:e]
                    if cmd.startswith(('keydown ', 'keyup ')): xdo(*cmd.split())   # a key held across others: {keydown alt}zz{keyup alt}
                    elif cmd == 'caps': xdo('key', 'Caps_Lock')   # --clearmodifiers would toggle Caps Lock back
                    else: xdo('key', '--clearmodifiers', cmd)
                    time.sleep(0.3); i = e + 1; continue
                if c == ' ': xdo('key', 'space')
                else: xdo('type', '--delay', '25', c)
                i += 1
            time.sleep(0.5)
            while GLib.MainContext.default().iteration(False): pass
            got = self.entry.get_text(); ok = got == expected
            if not ok: fails += 1
            print(f"{'ok  ' if ok else 'FAIL'} {name}: got {got!r} expected {expected!r}", flush=True)
            xdo('key', 'Escape')
        print(f"{total - fails}/{total} passed", flush=True)
        self.quit(); os._exit(0 if fails == 0 else 1)
App().run(None)

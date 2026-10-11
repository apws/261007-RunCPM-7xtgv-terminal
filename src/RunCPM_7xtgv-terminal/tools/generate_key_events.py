#!/usr/bin/env python3
"""Regenerate the Konsole/kitty host-to-7xtgv keyboard event sections.

The configured number row is supplied by XTGV_HACK_TOPROW. Remaining
punctuation bindings retain the tested Czech host layout. Unbound events carry the host's ordinary bytes as decimal triplets.
Do not add a key if its ordinary fallback is unknown (notably dead accents).
"""
from pathlib import Path
import argparse
import hashlib
import os

from host_key_config import TOPROW_SETTING, keyboard_environment


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
BEGIN = "# BEGIN GENERATED KEY EVENTS"
END = "# END GENERATED KEY EVENTS"


from key_event_ids import key_ids


IDS = key_ids()

def generate(top_row):
    KONSOLE = []
    KITTY = []
    SEEN_KONSOLE = set()
    SEEN_KITTY = set()

    def host_event(base, modifiers, fallback):
        assert len(fallback) <= 16
        assert all(0 <= value <= 255 for value in fallback)
        encoded = "".join(f"{value:03d}" for value in fallback)
        return f">70;1;{IDS[base]};{modifiers};{encoded}v"

    def shortcut(key, modifiers):
        prefixes = []
        if modifiers & 1:
            prefixes.append("ctrl")
        if modifiers & 2:
            prefixes.append("shift")
        if modifiers & 4:
            prefixes.append("alt")
        return "+".join([*prefixes, key])

    def konsole_flags(modifiers):
        return "".join(("+" if modifiers & bit else "-") + name
                       for bit, name in ((2, "Shift"), (4, "Alt"), (1, "Ctrl")))

    def add(base, qt_key, kitty_key, modifiers, fallback, *, keypad=False,
            konsole=True, kitty=True):
        event = host_event(base, modifiers, fallback)
        if konsole:
            flags = konsole_flags(modifiers) + ("+KeyPad" if keypad else "-KeyPad")
            identity = (qt_key, flags)
            if identity in SEEN_KONSOLE:
                raise RuntimeError(f'{TOPROW_SETTING}: conflicting Konsole key {identity!r}')
            SEEN_KONSOLE.add(identity)
            KONSOLE.append(f'key {qt_key:<10} {flags} : "\\E[{event}"')
        if kitty:
            name = shortcut(kitty_key, modifiers)
            if name in SEEN_KITTY:
                raise RuntimeError(f'{TOPROW_SETTING}: conflicting Kitty key {name!r}')
            SEEN_KITTY.add(name)
            KITTY.append(f"map --allow-fallback=none {name} send_text all \\x1b[{event}")

    def xterm_modifier(modifiers):
        return 1 + bool(modifiers & 2) + 2 * bool(modifiers & 4) + 4 * bool(modifiers & 1)

    # Plain row and Shift-digit bindings share one setting. Empty removes both.
    # Alt-bank, brightness, punctuation and keypad routes remain independent.
    for digit, cz in zip("1234567890", top_row):
        if cz.isspace() or cz in '\"\\#':
            raise RuntimeError(f'{TOPROW_SETTING}: unsupported Linux keymap character {cz!r}')
        base = f"KeyNum{digit}"
        add(base, cz, "plus" if cz == "+" else cz, 0, cz.encode("utf-8"))
        add(base, digit, digit, 2, digit.encode("ascii"))

    # KeyPad is a distinct Konsole mode, and kitty has a distinct keypad name.
    # A comma-producing physical keypad key is covered alongside the period seen
    # in the synthetic X11 probe. The device default for both is ASCII dot.
    add("KeyKpDecimal", ",", "kp_decimal", 0, b",", keypad=True, kitty=False)
    add("KeyKpDecimal", ".", "kp_decimal", 0, b".", keypad=True)
    add("KeyKpDelete", "Delete", "kp_delete", 0, b"\x1b[3~", keypad=True)

    # Existing plain/Ctrl/Alt navigation routes keep their profile editor
    # semantics. Extend only missing modifier combinations through the new path.
    navigation = (
        ("KeyLeft", "Left", "left", "D", None),
        ("KeyRight", "Right", "right", "C", None),
        ("KeyUp", "Up", "up", "A", None),
        ("KeyDown", "Down", "down", "B", None),
        ("KeyHome", "Home", "home", "H", None),
        ("KeyEnd", "End", "end", "F", None),
        ("KeyDelete", "Delete", "delete", None, 3),
        ("KeyInsert", "Insert", "insert", None, 2),
        ("KeyPageUp", "PgUp", "page_up", None, 5),
        ("KeyPageDown", "PgDown", "page_down", None, 6),
    )
    for base, qt, kitty_key, final, number in navigation:
        for mods in (2, 3, 5, 6, 7):
            if base in ("KeyHome", "KeyEnd") and mods == 6:
                continue  # established Alt+Shift horizontal-only alias
            if base in ("KeyPageUp", "KeyPageDown") and mods in (3, 7):
                continue  # view zoom and fine backlight
            modifier = xterm_modifier(mods)
            fallback = (f"\x1b[1;{modifier}{final}" if final else
                        f"\x1b[{number};{modifier}~").encode("ascii")
            add(base, qt, kitty_key, mods, fallback)

    # Standard xterm function-key fallbacks. F11 without modifiers remains the
    # host fullscreen shortcut; that chord is not intercepted by our keymap.
    function_codes = ("P", "Q", "R", "S", 15, 17, 18, 19, 20, 21, 23, 24)
    for index, code in enumerate(function_codes, 1):
        base = f"KeyF{index}"
        for mods in range(8):
            if index == 11 and mods == 0:
                continue
            if mods == 0:
                fallback = (f"\x1bO{code}" if isinstance(code, str) else
                            f"\x1b[{code}~").encode("ascii")
            else:
                modifier = xterm_modifier(mods)
                fallback = (f"\x1b[1;{modifier}{code}" if isinstance(code, str) else
                            f"\x1b[{code};{modifier}~").encode("ascii")
            add(base, f"F{index}", f"f{index}", mods, fallback)

    # Enter, Escape, Space, and Backspace have conventional fallback bytes. The
    # known terminal-owned Alt gestures and Ctrl+Shift+Backspace are left alone.
    special = (
        ("KeyBackspace", "Backspace", "backspace", b"\x08"),
        ("KeyEscape", "Escape", "escape", b"\x1b"),
        ("KeyEnter", "Return", "enter", b"\r"),
        ("KeySpace", "Space", "space", b" "),
    )
    for base, qt, kitty_key, ordinary in special:
        for mods in range(8):
            if mods in (5, 7):
                continue  # Ctrl+Alt can be Right-Alt/AltGr on national layouts.
            if base == "KeyBackspace" and mods in (3, 4, 6):
                continue
            if base == "KeyEscape" and mods == 4:
                continue
            if base == "KeySpace" and mods == 4:
                continue
            if base == "KeyBackspace" and mods & 1:
                fallback = b"\x18"  # existing Ctrl+Backspace convention
            elif base == "KeySpace" and mods & 1:
                fallback = b"\0"
            else:
                fallback = ordinary
            if mods & 4:
                fallback = b"\x1b" + fallback
            add(base, qt, kitty_key, mods, fallback)

    # Modifier-bearing letters can be represented exactly; unmodified and
    # Shift-only letters stay native host typing. Some desktop shortcuts may still
    # consume a chord before the keytab; the firmware cannot override those.
    for index, letter in enumerate("abcdefghijklmnopqrstuvwxyz", 1):
        base = f"Key{letter.upper()}"
        for mods in (1, 3, 4, 6):
            if letter == "q" and mods == 4:
                continue  # shared configured application-exit alias
            if mods & 1:
                fallback = bytes((index,))
            else:
                fallback = (letter.upper() if mods & 2 else letter).encode("ascii")
            if mods & 4:
                fallback = b"\x1b" + fallback
            add(base, letter.upper(), letter, mods, fallback)

    # Deliberate emulator escape. No fallback bytes: it is inert outside an
    # external COM and must never degrade to ordinary Ctrl+C or AltGr text.
    add("KeyC", "C", "c", 5, b"")

    # Non-dead Czech punctuation positions. Preserve the original UTF-8/ASCII
    # character whenever no device binding is present. Quote/backslash positions
    # need separate keytab-syntax probing before they can be safely intercepted.
    punctuation = (
        ("KeyBs2", "=", "%"),
        ("KeyP1", "ú", "/"),
        ("KeyP2", ")", "("),
        ("KeyL2", "§", "!"),
        ("KeyM1", ",", "?"),
        ("KeyM2", ".", ":"),
        ("KeyM3", "-", "_"),
    )
    kitty_alias = {"+": "plus", "-": "minus", "=": "equal"}
    for base, plain, shifted in punctuation:
        for mods, symbol in ((0, plain), (2, shifted)):
            add(base, symbol, kitty_alias.get(symbol, symbol), mods,
                symbol.encode("utf-8"))

    # L1's plain ů was observed separately; its Shift result is not established.
    add("KeyL1", "ů", "ů", 0, "ů".encode("utf-8"))
    return KONSOLE, KITTY


def rendered(path, lines):
    old = path.read_text(encoding='utf-8')
    assert old.count(BEGIN) == 1 and old.count(END) == 1, path
    before, tail = old.split(BEGIN, 1)
    _, after = tail.split(END, 1)
    return before + BEGIN + "\n" + "\n".join(lines) + "\n" + END + after


def launch_config(host, directory, top_row):
    konsole, kitty = generate(top_row)
    template = TOOLS / ('7xtgv-terminal.keytab' if host == 'konsole' else 'kitty.conf')
    content = rendered(template, konsole if host == 'konsole' else kitty)
    # Each configuration is immutable: a new launch must not alter live windows.
    digest = hashlib.sha256(content.encode('utf-8')).hexdigest()[:20]
    name = '7xtgv-terminal-' + digest
    path = Path(directory) / (name + ('.keytab' if host == 'konsole' else '.conf'))
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        temporary = path.with_suffix(path.suffix + f'.{os.getpid()}.tmp')
        temporary.write_text(content, encoding='utf-8')
        temporary.replace(path)
    return name if host == 'konsole' else str(path.resolve())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', choices=('konsole', 'kitty'))
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    if bool(args.host) != bool(args.output_dir):
        parser.error('--host and --output-dir must be used together')
    top_row = keyboard_environment()[TOPROW_SETTING]
    if args.host:
        print(launch_config(args.host, args.output_dir, top_row))
        return
    konsole, kitty = generate(top_row)
    for name, lines in (('7xtgv-terminal.keytab', konsole), ('kitty.conf', kitty), ('kitty-cpm.conf', kitty)):
        path = TOOLS / name
        content = rendered(path, lines)
        if path.read_text(encoding='utf-8') != content:
            path.write_text(content, encoding='utf-8')
    print(f'Generated {len(konsole)} Konsole and {len(kitty)} kitty rules')


if __name__ == '__main__':
    main()

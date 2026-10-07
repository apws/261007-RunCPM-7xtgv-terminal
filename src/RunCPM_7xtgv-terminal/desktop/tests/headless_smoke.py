"""Integrated sketch proof on a disposable card tree; never use the live mirror."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="xtgv-headless-card-") as temp:
    root = Path(temp)
    user = root / "A" / "0"
    user.mkdir(parents=True)
    (user / "VTHELLO.TXT").write_bytes(b"7xtgv desktop fixture\r\n\x1a")
    # Real Z80 COM: BDOS 9 text, then BDOS 4 writes 'X' to PUN.TXT as proof
    # of actual guest execution, not merely successful host compilation.
    (user / "VTHELLO.COM").write_bytes(
        bytes.fromhex("1110010e09cd05001e580e04cd0500c9") + b"Desktop Z80 guest works\r\n$")
    commands = root / "keyboard.bin"
    commands.write_bytes(b"DIR\rTYPE VTHELLO.TXT\rVTHELLO\rEXIT\r")
    result = subprocess.run([sys.argv[1], str(root), str(commands)],
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "finished normally" in result.stdout, result.stdout
    assert (user / "VTHELLO.COM").read_bytes().startswith(bytes.fromhex("111001"))
    assert (user / "PUN.TXT").read_bytes() == b"X", "Z80/BDOS punch output missing"
    print("Original Arduino sketch + 7XF + host storage + real GFX: PASS")

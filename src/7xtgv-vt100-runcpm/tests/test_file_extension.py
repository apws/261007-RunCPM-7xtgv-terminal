"""Exercise the production SD extension function against a bounded fake file."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class FileExtensionTest(unittest.TestCase):
    def test_zero_fill_bounds_and_failures(self):
        source = (Path(__file__).parents[1] / 'abstraction_arduino_7xtgv.h').read_text()
        function = source[source.index('bool _sys_extendfile('):source.index('\nuint8 _sys_readseq')]
        harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>
using uint8 = uint8_t;
constexpr int O_WRITE = 1, O_APPEND = 2;
std::vector<uint8> bytes;
int writes = 0, closes = 0;
bool failOpen = false, shortWrite = false, activity = false;
void _diskActivity(bool value) { activity = value; }
struct File {
    bool opened = false;
    operator bool() const { return opened; }
    unsigned long size() const { return bytes.size(); }
    size_t write(const uint8 *p, size_t n) {
        assert(++writes < 20);
        if (shortWrite) --n;
        bytes.insert(bytes.end(), p, p+n);
        return n;
    }
    void close() { ++closes; }
};
struct SDType { File open(const char *, int) { return {!failOpen}; } } SD;
'''
        cases = r'''
int main() {
    char name[] = "TEST.DAT";
    bytes.assign(17, 0x5a);
    assert(_sys_extendfile(name, 300));
    assert(bytes.size() == 300 && writes == 3 && !activity && closes == 1);
    assert(std::all_of(bytes.begin(), bytes.begin()+17, [](uint8 v){return v==0x5a;}));
    assert(std::all_of(bytes.begin()+17, bytes.end(), [](uint8 v){return v==0;}));
    assert(_sys_extendfile(name, 10));
    assert(_sys_extendfile(name, 300));
    assert(bytes.size() == 300 && writes == 3);
    bytes.clear(); writes = 0;
    assert(_sys_extendfile(name, 1) && bytes.size() == 1 && writes == 1);
    shortWrite = true;
    assert(!_sys_extendfile(name, 300) && !activity);
    failOpen = true;
    assert(!_sys_extendfile(name, 300) && !activity);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / 'extend.cpp'
            binary = Path(tmp) / 'extend'
            cpp.write_text(harness + function + cases)
            subprocess.run(['g++', '-std=c++17', '-O3', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)

if __name__ == '__main__':
    unittest.main()

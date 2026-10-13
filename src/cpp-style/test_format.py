"""Guard formatting boundaries using actual clang-format and Clang lexing."""
from pathlib import Path
import tempfile
import unittest

from format import format_source, protected
from verify import tokens


class FormatTests(unittest.TestCase):
    def test_tables_macros_literals_and_using_order(self):
        source = '''#define SCALE(x) ((x) * 2) \\
    /* preserve this continuation */
using std::min;
using std::max;
static const int table[] = {
    1,  2,  3,
    4,  5,  6};
const char* raw = R"x({ not a table })x";
namespace Board {
class Sample {
public:
int value() { return SCALE(table[0]); }
};
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'sample.cpp'
            path.write_text(source)
            old, result = format_source(path)
            self.assertEqual(tokens(old), tokens(result))
            self.assertEqual([old[a:b] for a, b in protected(old)],
                             [result[a:b] for a, b in protected(result)])
            self.assertIn('namespace Board\n{', result)
            self.assertIn('    class Sample\n    {', result)
            self.assertIn('    public:', result)
            self.assertIn('        int value()\n        {', result)
            self.assertLess(result.index('using std::min;'), result.index('using std::max;'))
            path.write_text(result)
            self.assertEqual(format_source(path)[1], result)

    def test_token_audit_rejects_changed_literals_and_operators(self):
        self.assertNotEqual(tokens('int n=1;'), tokens('int n=2;'))
        self.assertNotEqual(tokens('n++;'), tokens('n--;'))
        self.assertNotEqual(tokens('char* p="a b";'), tokens('char* p="ab";'))
        self.assertEqual(tokens('int n=1; // note'), tokens('int n = 1; /* note */'))


if __name__ == '__main__':
    unittest.main()

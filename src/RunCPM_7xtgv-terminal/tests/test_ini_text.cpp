#include "../src/terminal/ini_text.h"
#include <cassert>
#include <string>

static bool failAllocation = false;
static unsigned allocations = 0;

static void* resize(void* p, size_t n)
{
    ++allocations;
    return failAllocation ? nullptr : realloc(p, n);
}

static std::string contents(const vt::IniText& text)
{
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

static void load(vt::IniText& text, const std::string& bytes)
{
    uint8_t* data = text.beginLoad(bytes.size());
    assert(data);
    memcpy(data, bytes.data(), bytes.size());
    text.finishLoad();
}

int main()
{
    vt::IniText text(resize);
    assert(!text.valid() && !text.set("global", "x", "1"));
    // force a subsequent growth: 251 data bytes fit the initial 256 capacity
    const std::string initial(251, ';');
    load(text, initial);
    failAllocation = true;
    assert(!text.set("global", "backlight_level", "17"));
    assert(text.valid() && contents(text) == initial);
    failAllocation = false;
    assert(text.set("global", "backlight_level", "17"));
    const unsigned grown = allocations;
    for (unsigned i = 0; i < 1000; ++i)
        assert(text.set("global", "backlight_level", i % 2 ? "1" : "31"));
    assert(allocations == grown); // no allocate/free cycle on each setting change
    assert(!text.beginLoad(vt::IniText::MaxBytes + 1));
    assert(!text.valid());
    load(text, "[global]\r\nx=0\r\nx=1\r\n[other]\r\nx=2\r\n");
    assert(text.set("global", "x", "3"));
    assert(contents(text) == "[global]\r\nx=3\r\n[other]\r\nx=2\r\n");
    // A malformed/overlong section must not leave the previous section active.
    const std::string unknown = "[" + std::string(300, 'q') + "]\nx=4\n";
    load(text, "[global]\nx=0\n" + unknown);
    assert(text.set("global", "x", "3"));
    assert(contents(text) == "[global]\nx=3\n" + unknown);
    load(text, std::string(";kept\r\n\x1a") + "ignored");
    assert(contents(text) == ";kept\r\n");
    assert(vt::IniText::samePath("./a/0/7xtgvtrm.ini", "A/0/7XTGVTRM.INI"));
    assert(vt::IniText::samePath("/A/0/7XTGVTRM.INI", "A/0/7XTGVTRM.INI"));
    assert(!vt::IniText::samePath("A/7/7XTGVTRM.INI", "A/0/7XTGVTRM.INI"));
}

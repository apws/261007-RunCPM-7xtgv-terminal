#pragma once
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

inline std::string profileTestFile(const char* relative)
{
    const std::string source = __FILE__;
    const std::string directory = source.substr(0, source.find_last_of("/\\") + 1);
    std::ifstream file(directory + relative, std::ios::binary);
    assert(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

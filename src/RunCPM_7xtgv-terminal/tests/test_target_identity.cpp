#include "../hardware/arduino_7xtgv/target_identity.h"
#include <cassert>
#include <cstring>
int main(int argc, char **argv) {
    assert(argc == 3);
    assert(!strcmp(xtgvFirmwareTarget(), argv[1]));
    assert(!strcmp(xtgvFirmwareBuild(), argv[2]));
}

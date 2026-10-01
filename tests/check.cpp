#include "check.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>

extern bool gQuiet;

static int gFailures = 0;

std::vector<TestCase> &testRegistry() {
    static std::vector<TestCase> registry;
    return registry;
}

void checkFailed(
    const char *file,
    int line,
    const std::string &what
) {
    ++gFailures;
    fprintf(stderr, "%s:%d: %s\n", file, line, what.c_str());
}

// usage: run_tests [NAME_SUBSTRING]
int main(
    int argc,
    char *argv[]
) {
    gQuiet = true;
    setenv("TZ", "Europe/Paris", 1);
    tzset();

    int ran = 0;
    for(const auto &t : testRegistry()) {
        if(1<argc && 0==strstr(t.name, argv[1])) {
            continue;
        }
        auto before = gFailures;
        t.fn();
        ++ran;
        if(before!=gFailures) {
            fprintf(stderr, "FAIL %s\n", t.name);
        }
    }
    printf("accuchek: %d tests, %d failed checks\n", ran, gFailures);
    return (0==gFailures && 0<ran) ? 0 : 1;
}

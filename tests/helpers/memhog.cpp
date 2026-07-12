#include <cstdio>
#include <cstdlib>
#include <cstring>

// Allocates argv[1] mebibytes of heap memory and touches every page.
// Exits 0 on success, 1 on bad usage.
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fputs("usage: memhog <mib>\n", stderr);
        return 1;
    }
    long mib = std::strtol(argv[1], nullptr, 10);
    if (mib <= 0) {
        std::fputs("memhog: mib must be > 0\n", stderr);
        return 1;
    }

    std::size_t bytes = static_cast<std::size_t>(mib) * 1024UL * 1024UL;
    auto* buf = static_cast<volatile char*>(std::malloc(bytes));
    if (!buf) {
        std::fputs("memhog: malloc failed\n", stderr);
        return 1;
    }

    // Touch every page to force physical allocation.
    for (std::size_t i = 0; i < bytes; i += 4096) {
        buf[i] = static_cast<char>(i & 0xFF);
    }
    return 0;
}

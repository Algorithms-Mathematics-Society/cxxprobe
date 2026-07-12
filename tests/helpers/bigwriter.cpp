#include <cstdio>
#include <cstdlib>

// Writes argv[1] bytes of 'x' to stdout.
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fputs("usage: bigwriter <bytes>\n", stderr);
        return 1;
    }
    long long count = std::strtoll(argv[1], nullptr, 10);
    if (count < 0) {
        return 1;
    }
    for (long long i = 0; i < count; ++i) {
        if (std::putchar('x') == EOF) {
            break;
        }
    }
    return 0;
}

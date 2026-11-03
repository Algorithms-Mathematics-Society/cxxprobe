#include <unistd.h>

#include <cstdio>
#include <cstdlib>

// Reads argv[1] bytes from stdin, copies them to stdout, then stops reading and
// exits — the shape of a submission that returns from main after its first
// line, or bails out on a malformed token. Whatever the harness is still
// writing into the pipe then has nowhere to go.
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fputs("usage: earlycloser <bytes-to-read>\n", stderr);
        return 1;
    }
    long long want = std::strtoll(argv[1], nullptr, 10);
    if (want < 0) {
        return 1;
    }
    for (long long i = 0; i < want; ++i) {
        int ch = std::getchar();
        if (ch == EOF) {
            break;
        }
        if (std::putchar(ch) == EOF) {
            break;
        }
    }
    std::fflush(stdout);
    ::close(STDIN_FILENO);  // explicit: drop the read end while input remains
    return 0;
}

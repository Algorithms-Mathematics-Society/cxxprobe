// Pure CPU busy-loop with no syscalls in the hot path — accumulates CPU
// time as fast as possible, for exercising RLIMIT_CPU enforcement. Never
// exits on its own; relies on the sandbox to kill it.
int main() {
    volatile unsigned long counter = 0;
    while (true) {
        counter = counter + 1;
    }
    return 0;
}

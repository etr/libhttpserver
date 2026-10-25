// A clock observation deliberately overwrites the simulated last error.
#ifndef SCRIPTS_LOCAL_FIXTURES_WINDOWS_H_
#define SCRIPTS_LOCAL_FIXTURES_WINDOWS_H_
struct LARGE_INTEGER { std::int64_t QuadPart; };
inline int QueryPerformanceCounter(LARGE_INTEGER* ticks) {
    static std::int64_t now = 0;
    ticks->QuadPart = ++now;
    fake_error = 999;
    return 1;
}
inline int QueryPerformanceFrequency(LARGE_INTEGER* frequency) {
    frequency->QuadPart = 1000000;
    return 1;
}
#endif  // SCRIPTS_LOCAL_FIXTURES_WINDOWS_H_

/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifndef _WIN32
#include <future>
#endif
#include <memory>
#include <string>
#include <utility>
#include <httpserver/concurrency/resume_signal.hpp>

namespace {
using httpserver::detail::timer_queue;

bool timer_runs(timer_queue& queue) {
#ifdef _WIN32
    std::shared_ptr<void> ready(CreateEventW(nullptr, TRUE, FALSE, nullptr), [](void* event) { CloseHandle(event); });
    if (!ready) return false;
    queue.schedule(timer_queue::clock::now(), [ready] { SetEvent(ready.get()); });
    return WaitForSingleObject(ready.get(), 1000) == WAIT_OBJECT_0;
#else
    auto ready = std::make_shared<std::promise<void>>();
    auto done = ready->get_future();
    queue.schedule(timer_queue::clock::now(), [ready] { ready->set_value(); });
    return done.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
#endif
}

int retire_explicit_timer() {
    std::weak_ptr<int> pending_capture;
    {
        timer_queue queue;
        if (!timer_runs(queue)) return 1;
        auto capture = std::make_shared<int>(42);
        pending_capture = capture;
        queue.schedule(timer_queue::clock::now() + std::chrono::hours(1), [capture] {});
        capture.reset();
        if (pending_capture.expired()) return 2;
    }
    // Retirement joins the worker and releases pending callbacks promptly,
    // even though their deadlines are an hour away.
    return pending_capture.expired() ? 0 : 3;
}

#ifdef _WIN32
int run_child(const wchar_t* mode) {
    wchar_t image[32768];
    const DWORD length = GetModuleFileNameW(nullptr, image, 32768);
    if (length == 0 || length == 32768) return 4;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE destroyed = CreateEventW(&attributes, TRUE, FALSE, nullptr);
    if (destroyed == nullptr) return 5;
    std::wstring command = L"\"" + std::wstring(image, length) + L"\" " + mode + L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(destroyed));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(image, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &child)) {
        CloseHandle(destroyed);
        return 5;
    }
    CloseHandle(child.hThread);
    const DWORD wait = WaitForSingleObject(child.hProcess, 9000);
    DWORD code = 6;
    if (wait == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(child.hProcess, &code)) code = 7;
    } else {
        // Only this retained child handle is retired; no name-based cleanup.
        TerminateProcess(child.hProcess, 8);
        WaitForSingleObject(child.hProcess, 1000);
    }
    CloseHandle(child.hProcess);
    // Captures may retire normally in a static executable's CRT teardown,
    // but must survive DLL process detach after other threads are killed.
    if (WaitForSingleObject(destroyed, 0) != WAIT_TIMEOUT) code = 10;
    CloseHandle(destroyed);
    if (code != 0) std::fprintf(stderr, "timer child failed: wait=%lu exit=%lu\n", wait, code);
    return code == 0 ? 0 : 1;
}
#endif
}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc == 3 && std::strcmp(argv[1], "--default-exit") == 0) {
        using shutdown_query = BOOLEAN (NTAPI*)();
        const auto terminating = reinterpret_cast<shutdown_query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlDllShutdownInProgress"));
        if (!terminating) return 11;  // documented Windows 10+ contract
        auto& queue = httpserver::detail::default_timer_queue();
        if (!timer_runs(queue)) return 9;
        HANDLE destroyed = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::strtoull(argv[2], nullptr, 10)));
        auto marker = std::shared_ptr<void>(destroyed, [terminating](void* event) {
            if (terminating()) SetEvent(event);
        });
        queue.schedule(timer_queue::clock::now() + std::chrono::hours(1), [marker = std::move(marker)] {});
        // Leave the DLL timer worker waiting when ExitProcess retires other
        // threads, before invoking DLL static destructors under loader lock.
        Sleep(100);
        std::puts("default timer active; returning from main");
        std::fflush(stdout);
        return 0;
    }
    if (argc == 3 && std::strcmp(argv[1], "--retire") == 0) return retire_explicit_timer();
    if (run_child(L"--retire") != 0) return 1;
    return run_child(L"--default-exit");
#else
    static_cast<void>(argc);
    static_cast<void>(argv);
    return retire_explicit_timer();
#endif
}

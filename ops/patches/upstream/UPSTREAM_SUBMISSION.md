# Upstream Poppler Submission: Native Win32 CRITICAL_SECTION Synchronization

**Target Project**: Poppler ([freedesktop.org / GitLab](https://gitlab.freedesktop.org/poppler/poppler))  
**Patch File**: [`0001-poppler-win32-critical-section.patch`](file:///d:/FluidCorePDF/fluidcore-platform/ops/patches/upstream/0001-poppler-win32-critical-section.patch)  
**Affects**: Windows builds using MinGW-w64 (`libwinpthread-1.dll`) across all modern GCC releases (including MSYS2 UCRT64 / MINGW64)  
**Historical Context**: Thread-safety introduced in 2019 (Poppler issue #784, merge requests adding `std::recursive_mutex` to `Array.h` and `Dict.h`)

---

## 1. Problem Description

When Poppler is built on Windows with MinGW-w64 (GCC toolchains using `winpthreads`), parsing, rendering, or searching PDF documents causes an unbounded memory leak. 

In an empirical benchmark across an 892-page technical document, repetitive full-text searches accumulated **+5.08 MB of unreclaimed private heap memory per search pass**. Profiling with IAT hooking and module-level call stack attribution identified that **100% of this recurring leak** originates from:
- **Module**: `libwinpthread-1.dll`
- **Call site**: `pthread_mutex_lock + 0x88`
- **Allocation signature**: Exactly 221,870 allocations of 24 bytes each per search pass.

---

## 2. Root Cause Analysis

The leak is caused by a known flaw in the interaction between GCC `libstdc++` and `libwinpthread`:

1. In GCC `<mutex>`, when `__GTHREAD_RECURSIVE_MUTEX_INIT` is defined:
   ```cpp
   class recursive_mutex {
       __gthread_recursive_mutex_t _M_mutex;
   public:
       recursive_mutex() = default;
       ~recursive_mutex() = default; // <-- pthread_mutex_destroy is NEVER called!
   ```
2. In `libwinpthread`, `pthread_mutex_t` uses lazy initialization. When `pthread_mutex_lock` is invoked on a statically or defaulted-initialized mutex, it allocates an internal 24-byte handle structure on the process CRT heap (`malloc(24)`).
3. Under POSIX specifications, this internal structure is only deallocated when `pthread_mutex_destroy` is called.
4. Because `~recursive_mutex() = default;` omits `pthread_mutex_destroy()`, every instance of `std::recursive_mutex` that is locked at least once permanently orphans 24 bytes on the Windows CRT heap upon destruction.
5. In Poppler, `Array` and `Dict` are core Abstract Syntax Tree (AST) node objects instantiated and destructed by the hundreds of thousands while parsing PDF content streams. Consequently, megabytes of memory are leaked on every page or document processed.

---

## 3. Minimal Standalone Reproduction

The following standalone C++ program compiles under MinGW-w64 GCC and demonstrates the 24-byte leak:

```cpp
// repro_winpthread_mutex_leak.cpp
// Compile: g++ -O2 repro_winpthread_mutex_leak.cpp -o repro.exe
#include <iostream>
#include <mutex>
#include <windows.h>
#include <psapi.h>

size_t getPrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    return pmc.PrivateUsage;
}

int main() {
    size_t before = getPrivateBytes();
    std::cout << "Starting 500,000 recursive_mutex allocation cycles...\n";

    for (int i = 0; i < 500000; ++i) {
        std::recursive_mutex m;
        m.lock();
        m.unlock();
        // m is destructed here without pthread_mutex_destroy
    }

    size_t after = getPrivateBytes();
    double leakedMB = (after > before) ? (after - before) / (1024.0 * 1024.0) : 0.0;
    std::cout << "Private Bytes Before: " << before / 1024 << " KB\n";
    std::cout << "Private Bytes After:  " << after / 1024 << " KB\n";
    std::cout << "Memory Leaked:        " << leakedMB << " MB (expected ~11.4 MB of 24-byte handles)\n";
    return 0;
}
```

---

## 4. Proposed Solution

We provide a lightweight, zero-dependency header wrapper `poppler/PopplerMutex.h`:

- **On Windows (`_WIN32`)**: Maps `PopplerRecursiveMutex` to a native Win32 `CRITICAL_SECTION`.
  - `InitializeCriticalSectionAndSpinCount(&cs_, 4000)` on construction.
  - `DeleteCriticalSection(&cs_)` in the destructor.
  - Native Win32 critical sections do not allocate auxiliary handles via winpthreads, completely eliminating the leak.
  - Provides `lock()`, `unlock()`, and `try_lock()` compliant with C++ BasicLockable concepts, making it compatible with `std::scoped_lock` and `std::unique_lock`.
- **On Non-Windows Platforms**: Aliases directly to `std::recursive_mutex` via `using PopplerRecursiveMutex = std::recursive_mutex;`, preserving zero-overhead semantics.

---

## 5. Empirical Verification Results

Across 20 consecutive 892-page search passes on the reference document:
- **Unpatched (std::recursive_mutex)**: +5.08 MB LiveAlloc per pass (+101.12 MB total retained).
- **Patched (PopplerRecursiveMutex / Win32 CRITICAL_SECTION)**: **+0.00 MB LiveAlloc per pass (0 bytes retained)**.
- **Concurrency Gate**: 25 back-to-back iterations with thread scheduling jitter across 3 concurrent threads: **0 deadlocks, 0 races, deterministic match counts**.
- **Malformed Input Suite**: 55 synthetic malformed and bit-flipped PDF inputs: **0 crashes, 100% clean stack unwinding**.

# bench-scalability

## Machine specs
Linux (x86_64), POSIX
CPU: Multicore Host Environment

## Environment
GCC / Clang, CMake, Ninja, RelWithDebInfo

## Results
cold load: 0.01 s
ram working set: 0.036 GB
spatial p99: 0.05 ms
inking latency: 8.05 ms
squeeze fps: 60.0

### Detailed Diagnostic Breakdown
- **Methodology**: Fresh-process cold start with process address space isolation
- **Document Scale**: 50 PDF documents (5000 vector pages total)
- **Cold Load Duration (T0 -> T1)**: 0.01 s (Budget: <= 8.00 s) -> PASS
- **Baseline RAM**: 19.0 MB
- **Working Set after Cold Load**: 28.6 MB
- **Working Set after Pass 1 (50 docs)**: 36.2 MB
- **Working Set after Pass 2 (50 docs)**: 36.9 MB
- **Inter-Pass Working Set Growth**: 0.7 MB (demonstrates cache boundedness)
- **Peak Working Set**: 0.036 GB (36.7 MB, Budget: <= 1.2 GB) -> PASS

## Verdict
PASS

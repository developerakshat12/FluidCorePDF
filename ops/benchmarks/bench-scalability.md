# bench-scalability

## Machine specs
Windows 11 (x86_64), Native MSYS2 UCRT64
CPU: Multicore Host Environment

## Environment
UCRT64 GCC 16, CMake, Ninja, RelWithDebInfo

## Results
cold load: 0.02 s
ram working set: 0.060 GB
spatial p99: 0.05 ms
inking latency: 8.05 ms
squeeze fps: 60.0

### Detailed Diagnostic Breakdown
- **Methodology**: Fresh-process cold start with process address space isolation
- **Document Scale**: 50 PDF documents (5000 vector pages total)
- **Cold Load Duration (T0 -> T1)**: 0.02 s (Budget: <= 8.00 s) -> PASS
- **Baseline RAM**: 13.6 MB
- **Working Set after Cold Load**: 20.9 MB
- **Working Set after Pass 1 (50 docs)**: 59.6 MB
- **Working Set after Pass 2 (50 docs)**: 61.7 MB
- **Inter-Pass Working Set Growth**: 2.1 MB (demonstrates cache boundedness)
- **Peak Working Set**: 0.060 GB (61.7 MB, Budget: <= 1.2 GB) -> PASS

## Verdict
PASS

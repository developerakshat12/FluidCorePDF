# bench-scalability

## Machine specs
Windows 11 (x86_64), Native MSYS2 UCRT64
CPU: Multicore Host Environment

## Environment
UCRT64 GCC 16, CMake, Ninja, RelWithDebInfo

## Results
cold load: 0.01 s
ram working set: 0.061 GB
spatial p99: 0.05 ms
inking latency: 8.05 ms
squeeze fps: 60.0

### Detailed Diagnostic Breakdown
- **Methodology**: Fresh-process cold start with process address space isolation
- **Document Scale**: 50 PDF documents (5000 vector pages total)
- **Cold Load Duration (T0 -> T1)**: 0.01 s (Budget: <= 8.00 s) -> PASS
- **Baseline RAM**: 13.6 MB
- **Working Set after Cold Load**: 20.8 MB
- **Working Set after Pass 1 (50 docs)**: 59.4 MB
- **Working Set after Pass 2 (50 docs)**: 62.1 MB
- **Inter-Pass Working Set Growth**: 2.7 MB (demonstrates cache boundedness)
- **Peak Working Set**: 0.061 GB (62.1 MB, Budget: <= 1.2 GB) -> PASS

## Verdict
PASS

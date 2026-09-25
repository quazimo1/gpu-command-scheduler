# GPU Command-Scheduler Simulator

A small, public-safe C++17 simulator for exploring GPU work submission and scheduling without modelling any commercial GPU or using confidential implementation details.

## Why it exists

GPU driver debugging made me curious about the lifecycle of submitted work: when it becomes runnable, how dependencies affect it, what different policies optimize, and what clients observe after a timeout. This project turns those questions into deterministic experiments.

## What it models

```text
CSV workload -> validation -> dependency tracking -> runnable jobs
             -> scheduling policy -> simulated engine -> completion/recovery
             -> CSV event trace + aggregate metrics
```

Implemented behavior:

- multiple client contexts and ordered job submission;
- fence-like dependencies with cycle and invalid-reference rejection;
- FIFO, strict-priority, and weighted round-robin policies;
- deterministic execution time and queue-wait metrics;
- timeout/hang injection, engine reset, and dependent-job cancellation;
- versionable CSV workloads and event traces;
- dependency-free C++ tests for ordering, policy, recovery, and validation.

This is a discrete-event learning model. It is not cycle-accurate and does not claim to reproduce NVIDIA, Intel, or any other vendor's scheduler.

## Build and test

Requires CMake 3.20+ and a C++17 compiler.

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Run an experiment

```powershell
./build/Release/gpu-scheduler.exe fifo workloads/mixed.csv fifo-events.csv
./build/Release/gpu-scheduler.exe priority workloads/mixed.csv priority-events.csv
./build/Release/gpu-scheduler.exe wrr workloads/mixed.csv wrr-events.csv
```

Each run prints completed, timed-out, and cancelled counts together with elapsed time, mean queue wait, and p95 queue wait. The event CSV preserves submitted, runnable, dispatched, completed, timed-out, recovered, and cancelled transitions for inspection.

## Workload format

```text
id,context,priority,duration,timeout,hang,dependencies
3,1,2,5,10,false,1|2
```

Durations and timeouts use abstract ticks. Priorities are positive integers; larger values receive precedence under strict priority and more consecutive service under weighted round robin.

## Design invariants

- A job is dispatched only after all dependencies complete.
- A failed dependency cancels its dependents.
- Every accepted job reaches exactly one terminal state.
- A timed-out job produces one recovery event before later work executes.
- Deterministic input produces deterministic output.

## Deliberate limits

The first release uses one non-preemptive simulated engine and assumes all jobs arrive at time zero. It does not model threads, interrupts, memory hierarchy, hardware queues, or real GPU timing.

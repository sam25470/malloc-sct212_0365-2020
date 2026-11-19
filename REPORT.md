# Memory Allocator Benchmark Report

## Workload and method
SAMUEL KIMOTHO GITHAIGA 
SCT212-0365/2020

The benchmark performs 50,000 allocation/free operations using a fixed random seed (`20261119`). Each operation selects one of 1,000 pointer slots. An occupied slot is freed; an empty slot receives an allocation of between 1 and 4,096 bytes. The same pre-generated sequence is replayed for first-fit, best-fit and the C library's `malloc()`/`free()`.

Elapsed time is measured with `clock_gettime(CLOCK_MONOTONIC, ...)`. The custom allocator's heap statistics are collected after all remaining allocations have been freed. External fragmentation is calculated as:

`1 - largest_free / free_bytes`

## Measurements

One run in the supplied Linux/GCC environment produced:

| Policy | heap_bytes | free_bytes | free_blocks | largest_free | External fragmentation | Time (seconds) |
|---|---:|---:|---:|---:|---:|---:|
| FIT_FIRST | 1,310,720 | 1,310,688 | 1 | 1,310,688 | 0.0000 | 0.300434 |
| FIT_BEST | 1,310,720 | 1,310,688 | 1 | 1,310,688 | 0.0000 | 0.373777 |
| C library malloc/free | Not exposed by this allocator's `heap_stats()` API | — | — | — | — | 0.004150 |

The C library timing is included for comparison. The custom allocator's heap statistics describe only memory obtained and managed by this implementation, not the C library allocator.

## Discussion

For this workload, I would choose first-fit. Both policies ended with one fully coalesced free block and therefore had the same measured external-fragmentation value after the workload. First-fit was faster in this run because it stops at the first suitable free block, while best-fit may scan the entire list to find the smallest suitable block. The C library allocator was substantially faster in this particular benchmark. One reason this allocator is slower is that its placement searches and free-list coalescing are linear scans, whereas a mature allocator can use more sophisticated size classes, bins and other data structures to reduce search costs. These measurements are specific to this run and machine; rerunning `bench` may produce different timings.

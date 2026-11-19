# Custom C Memory Allocator

**Name:** YOUR NAME  
**Student ID:** YOUR STUDENT ID

## Build and run

Keep the lecturer-provided `myalloc.h`, `test_myalloc.c`, and `Makefile` unchanged.

Build and run the supplied test harness:

```sh
make clean
make test
```

Build and run the benchmark:

```sh
gcc -std=c11 -Wall -Wextra -g -O0 -o bench myalloc.c bench.c
./bench
```

Run the test harness with sanitizers:

```sh
gcc -std=c11 -g -Wall -Wextra -fsanitize=address,undefined \
    -o t_asan myalloc.c test_myalloc.c
ASAN_OPTIONS=detect_leaks=0 ./t_asan
```

## Implementation checklist

| Task | Feature | Status |
|---|---|---|
| 1 | Block header, alignment and helper functions | Complete |
| 2 | Heap growth using `sbrk()` | Complete |
| 3 | `my_malloc()` and first-fit placement | Complete |
| 4 | Splitting free blocks | Complete |
| 5 | `my_free()` and coalescing | Complete |
| 6 | `my_calloc()` and `my_realloc()` | Complete |
| 7 | Best-fit, statistics, heap dump and consistency checker | Complete |
| 8 | Benchmark and fragmentation report | Complete |

## Design explanation

Each heap block begins with a header containing its payload capacity, a pointer to the next block, a free/used flag and a magic value. The header size is rounded up to a multiple of 16 bytes. Requested payload sizes are also rounded up to 16 bytes, which ensures that every pointer returned to a caller is suitably aligned.

The allocator keeps a singly linked list ordered by increasing memory address. The list contains both allocated and free blocks. When an allocation is requested, the selected placement policy searches for a sufficiently large free block. First-fit selects the first suitable block, while best-fit scans the list and selects the smallest suitable block. If no block fits, `grow()` obtains a region from the process heap with `sbrk()`, requesting at least 64 KiB. It accounts for alignment padding because the program break may not initially be aligned.

A selected block is split when the unused remainder can contain another header and at least 16 payload bytes. When memory is released, the allocator validates the exact payload pointer, detects double frees, marks the block free and scans the list to coalesce physically adjacent free blocks. The physical-address check is important: two consecutive list entries may be separated by a gap if another allocator moved the program break.

## Test results

The supplied test harness completed successfully:

```text
T1 through T8: PASS
Result: 29 passed, 0 failed
```

The same 29 checks also passed with AddressSanitizer and UndefinedBehaviorSanitizer enabled.

## Questions

### 1. Why must the header size be a multiple of 16 as well as the payload sizes?

The payload begins immediately after the header. If the header size were not a multiple of 16, an aligned header could produce a misaligned payload. Rounding both the header size and payload capacities to multiples of 16 preserves alignment for every block.

### 2. Why can `printf()` inside `my_malloc()` cause infinite recursion?

A standard-library output function may allocate memory internally. If it calls the system allocator, that allocation can enter allocator code while `my_malloc()` is already running. This can cause recursive allocator calls and potentially infinite recursion, so this implementation uses `write()` for diagnostics.

### 3. Your `my_free()` is O(n). Which extra field would let you find a block's previous neighbor in O(1), and what is the name of the technique?

Add a `prev` pointer to each block header, creating a doubly linked list. The previous list node can then be reached directly in O(1), although physical adjacency must still be checked before merging. This is commonly called a doubly linked list.

### 4. Explain internal and external fragmentation, and name one place each comes from.

Internal fragmentation is unused space inside an allocated block; here, rounding a requested size up to a multiple of 16 can create it. External fragmentation is free memory divided into separate holes; it can arise when allocated blocks remain between free blocks and prevent those free regions from being merged.

### 5. Why does `sbrk()` memory never get returned to the OS in this allocator? How could memory at the end of the heap be returned?

This implementation manages and reuses released blocks but does not lower the program break. A possible extension is to detect when the final block is free and physically reaches the current program break, then use `brk()` to move the break backwards. That must be done carefully because other library code may also use the process heap.

### 6. Two blocks are consecutive in your list but not adjacent in memory. How did this happen, and what does coalescing do?

Another allocator, such as the C library allocator, may have requested memory between two calls to this allocator, creating a gap in the address space. Coalescing compares the end address of the first payload with the address of the next header and merges only when those addresses are equal.

## Known limitations

- The allocator is intended for a single-threaded test program; it does not use locks.
- It does not return unused heap memory to the operating system.
- The singly linked list makes allocation searches and pointer validation O(n).
- The allocator uses `sbrk()`, which is deprecated and should not be mixed casually with other heap managers in production software.

## Sources and AI-tool declaration

- ICS 2305 Systems Programming Practical Assignment, Question 2: requirements, design constraints and test plan.
- Lecturer-provided `myalloc.h`, `test_myalloc.c` and `Makefile`: API definitions and test harness.
- OpenAI ChatGPT: used to assist with implementation, code review, explanations and preparation of documentation. The submitted implementation and test results should be reviewed and understood by the student.

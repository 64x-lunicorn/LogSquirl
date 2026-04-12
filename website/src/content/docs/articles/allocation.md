---
title: Allocation Matters
description: How reducing memory allocations and switching allocators improved LogSquirl's search performance by 20%+.
---

## Performance testing

After switching to Hyperscan (see [Switching to Hyperscan](/articles/hyperscan)), studying the `perf` tool report revealed some surprising lines:

```
# Overhead  Command          Shared Object                  Symbol
     4.84%  Thread (pooled)  logsquirl_portable             [.] std::vector<QString>::~vector
     3.83%  Thread (pooled)  liblogsquirl_tbbmalloc.so      [.] rml::internal::internalPoolMalloc
     3.15%  Thread (pooled)  logsquirl_portable             [.] noodExec
     2.97%  logsquirl_portable   libc-2.32.so               [.] 0x000000000015e01f
     2.92%  Thread (pooled)  logsquirl_portable             [.] hs_scan
     2.78%  Thread (pooled)  libQt5Core.so.5.15.2           [.] QString::toUtf8_helper
     2.37%  Thread (pooled)  logsquirl_portable             [.] HsMatcher::hasMatch
     2.07%  Thread (pooled)  liblogsquirl_tbbmalloc.so      [.] __TBB_malloc_safer_free
```

The top lines belong to memory management code — destroying vectors of `QString` objects and TBB malloc internals.

## Reducing memory allocations

The perf report shows that a lot of time is wasted destroying vectors of `QString`. When search is executing there is one thread that reads raw data from the file, and several threads that do searching through the blocks of lines. Block size is configurable in settings, typically 5,000 or 10,000 lines. When a thread gets new raw data from file, it first transforms raw bytes to a vector of `QString` objects converting each line from file text encoding to UTF-16 (internal Qt string representation). Finally, each line is converted to UTF-8 so it can be passed to the Hyperscan regex matching engine.

During search, LogSquirl was creating and destroying `QString` and `QByteArray` objects for **each line** in the file.

All these allocations can be avoided by modifying the encoding conversion algorithm. A thread receives a block of raw bytes guaranteed to contain several full lines ending with `\n`. That allows converting the whole block to a single `QString` (since the block starts and ends on multi-byte encoding boundaries). Then `QString` can be converted to a single UTF-8 `QByteArray` in one go. Using very fast `std::memchr`, a vector of `std::string_view` objects is created where each `string_view` corresponds to a single line of the original data block.

The number of allocations is reduced from **two per line** to **two per search block**. With search blocks containing at least 1,000 lines, this leads to several orders of magnitude fewer memory allocations. On a test machine this provided about **20% performance improvement**.

## Switching the application-wide memory allocator

LogSquirl had been using the scalable memory allocator provided by the Intel TBB library for several years. It is designed to work well for multi-threaded applications. Using it instead of the default system memory allocator resulted in 5–10% performance improvement. It is very easy to use — a program only needs to link with the `tbbmalloc_proxy` dynamic library, and then all memory allocation is done by the TBB allocator.

Although the TBB scalable allocator is well-built and has good performance, it is quite complex and designed for cases when there are many threads allocating and freeing memory concurrently. With the reduced number of allocations described above, a lighter-weight allocator could perform as well or better with less overhead.

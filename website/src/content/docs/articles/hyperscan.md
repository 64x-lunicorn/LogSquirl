---
title: Switching to Hyperscan
description: How replacing Qt's PCRE2 regex engine with Hyperscan doubled LogSquirl's search throughput.
---

## Performance testing

For years LogSquirl has been using the regular expression engine provided by Qt library. It is based on PCRE2 with JIT compilation. However, recent performance tests proved that regular expression matching is a bottleneck. For example, this is a report from `perf` tool after running a simple string search in a 1 GB text file:

```
# Overhead  Command          Shared Object                  Symbol
    17.10%  Thread (pooled)  libpcre2-16.so.0.10.0          [.] 0x000000000005956f
     7.29%  Thread (pooled)  libpcre2-16.so.0.10.0          [.] 0x000000000005956c
     6.95%  Thread (pooled)  libpcre2-16.so.0.10.0          [.] 0x0000000000059560
     2.89%  Thread (pooled)  libQt5Core.so.5.15.2           [.] 0x0000000000167498
     2.48%  Thread (pooled)  liblogsquirl_tbbmalloc.so      [.] rml::internal::internalPoolMalloc
     2.06%  Thread (pooled)  liblogsquirl_tbbmalloc.so      [.] __TBB_malloc_safer_free
     2.01%  Thread (pooled)  logsquirl_portable_pcre        [.] std::vector<QString>::~vector
     1.80%  Thread (pooled)  libpcre2-16.so.0.10.0          [.] pcre2_match_16
     1.70%  Thread (pooled)  libQt5Core.so.5.15.2           [.] QMutex::lock
     1.47%  Thread (pooled)  libQt5Core.so.5.15.2           [.] QRegularExpression::QRegularExpression
```

Most time is spent inside the PCRE2 library. Moreover, there is a noticeable impact of `QMutex`. LogSquirl does not use `QMutex`, so this must be from `QRegularExpression` implementation details.

On a development PC the above search takes about 3.5 seconds:

```
Searching done, overall duration 3570.39 ms
Line reading took 814.359 ms
Results combining took 112.078 ms
Matching took 3311.02 ms
Searching perf 2548970 lines/s
Searching io perf 251.642 MiB/s
```

## Hyperscan regular expressions engine

After some research about existing regular expressions libraries, it turned out that PCRE2 is the only one that can do matching directly on UTF-16 encoded strings. This is important because LogSquirl uses Qt for text encoding conversions, and `QTextCodec` can only convert input data to UTF-16. In order to use other libraries, UTF-16 strings have to be encoded to UTF-8. That additional overhead has to be taken into account when evaluating other regular expression engines.

Several articles pointed out that [Hyperscan](https://www.hyperscan.io/) library shows very promising results. Rust Leipzig's [research](https://rust-leipzig.github.io/regex/2017/03/28/comparison-of-regex-engines/) claimed that Hyperscan can be 3 times faster than PCRE2. This result appears in another [article](https://software.intel.com/content/www/us/en/develop/articles/why-and-how-to-replace-pcre-with-hyperscan.html) from Intel.

The 3x speedup was so significant that the text re-encoding overhead could be ignored. Integrating Hyperscan was rather easy, thanks to good [documentation](http://intel.github.io/hyperscan/dev-reference/). It is a C library, so some RAII had to be implemented to avoid memory leaks.

The results for the same file after switching:

```
Searching done, overall duration 1804.83 ms
Line reading took 907.165 ms
Results combining took 49.838 ms
Matching took 1428.87 ms
Searching perf 5042484 lines/s
Searching io perf 497.81 MiB/s
```

Nearly **2x faster** overall, with matching performance jumping from ~2.5 million to ~5 million lines per second.

:::note
LogSquirl has since transitioned from Hyperscan to [Vectorscan](https://github.com/VectorCamp/vectorscan), a maintained fork with native ARM/NEON support and continued development.
:::

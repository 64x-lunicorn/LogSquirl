---
title: Combining Search Expressions
description: How LogSquirl implements boolean combination of regular expressions using AND, OR, NOT operators.
---

## When regular expression syntax is not enough

Regular expressions are very powerful. However, sometimes their syntax becomes too complex and cumbersome. This is the case when you want to search for "this, but not that". Writing a single regular expression for such a pattern requires some look-ahead magic. Things become a lot easier if search patterns can be combined from several simple regular expressions using boolean logic operators `and`, `or` and `not`. Compare:

```
(?=.*word1)(?=.*word2)(?!.*word3)
```

with:

```
word1 and word2 and not(word3)
```

## How it works

Matching a boolean combination of regular expressions has several steps. First, a pattern has to be split into sub-patterns and transformed into a symbolic expression that can be repeatedly evaluated:

```
"pattern 1" and ("pattern 2" or "pattern 3") and not("pattern 4")
```

is translated into the following symbolic boolean expression:

```
p1 and (p2 or p3) and not(p4)
```

where `p1`, `p2`, `p3` and `p4` are variables that correspond to sub-patterns.

Then for each line of text all sub-patterns are run through the regex engine to determine which patterns match the input string:

```
p1 = true
p2 = false
p3 = true
p4 = false
```

After that the boolean expression is evaluated to get the final verdict for the line.

## Implementation journey

The first two steps were easy to implement both for Hyperscan and Qt regular expression engines. Hyperscan has a big advantage here as it compiles a set of regular expressions into a single database and matches them all at once, so it does not take too much time to match all sub-patterns.

For the first prototype, [LibBoolEE](https://github.com/xstreck1/LibBoolEE) was used. It is very easy to integrate:

```cpp
LibBoolEE::Vals vals = { { "A", true }, { "B", false } };
return LibBoolEE::resolve("A|B&B", vals); // returns 1
```

This was good enough for a proof of concept. However, `LibBoolEE::resolve` parses the expression each time, causing big performance degradation — the expression is evaluated for each line from the opened file, so it has to be very fast.

Next came embedded scripting engines. First was the JavaScript engine provided by Qt (`QJsEngine`). The results were better but still not very impressive. Then came [LuaJit](https://luajit.org/) via [sol2](https://github.com/ThePhD/sol2) and [Jinx](https://github.com/JamesBoer/Jinx) — both performed better but still not great.

After more research, [The Great C++ Mathematical Expression Parser Benchmark](https://github.com/ArashPartow/math-parser-benchmark-project) pointed to [ExprTk](http://www.partow.net/programming/exprtk/) library. For this particular task it performed much better than general purpose embedded scripting engines, providing the right balance of speed and flexibility for boolean expression evaluation on millions of log lines.

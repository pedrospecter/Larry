# Comparison suites

One file per comparison (PLAN.md, track C). Each line is one pair, with fields
separated by " | ":

    expected | sentence a | sentence b | categories of a | categories of b

The categories are optional and separated by commas, one per entity in order.
`expected` is either `yes` or `no`, which checks only whether the comparison
holds, or the full result as bytes, for example
`C4 yes 0=0 1~1 2=2 3=3 pattern:the [noun] is blue`, which checks the matches
too: `i=j` lines up the same word, `i~j` a different word with the same
category, `ai` and `bj` are entities that matched nothing.

Lines that start with `#` are comments.

#!/usr/bin/env bash
# Builds dictionary/en/words.txt from the Moby Part-of-Speech list (Grady Ward,
# public domain, Project Gutenberg ebook 3203): one word per line, a tab, and
# its categories in Larry's names separated by commas. Single words only
# (hyphens and apostrophes allowed), in lower case, so an entry is found by the
# folded word. A capitalized noun in Moby is a proper noun here. Auxiliary verbs
# are the ones in base_rules/en/auxiliaries.txt. The list is a fallback for
# words no conception has taught: see PLAN.md, item A2b.
#
#   scripts/dictionary.sh [mobypos.txt]     # downloads the list when no file is given
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
URL="https://www.gutenberg.org/files/3203/files/mobypos.txt"
OUT="$ROOT/dictionary/en/words.txt"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
if [ -n "${1-}" ]; then
    cp "$1" "$work/mobypos.txt"
else
    curl -fsSL -o "$work/mobypos.txt" "$URL"
fi

# The auxiliary verbs, decoded from the hex rule file.
auxiliaries="$work/auxiliaries.txt"
grep -v '^#' "$ROOT/base_rules/en/auxiliaries.txt" | grep . | cut -d= -f1 |
    while IFS= read -r hex; do printf '%s' "$hex" | sed 's/../\\x&/g' | xargs -0 printf '%b\n'; done > "$auxiliaries"

# Moby is Mac Roman with CRLF. Codes: N noun, p plural noun, h noun phrase,
# V verb, t transitive verb, i intransitive verb, A adjective, v adverb,
# C conjunction, P preposition, ! interjection, r pronoun, D definite article,
# I indefinite article, o nominative.
iconv -f MACINTOSH -t UTF-8 "$work/mobypos.txt" | tr -d '\r' |
    awk -F'\\' -v aux="$auxiliaries" '
    BEGIN {
        while ((getline line < aux) > 0) { is_aux[line] = 1 }
        map["N"] = "noun"; map["p"] = "noun"; map["h"] = "noun"; map["V"] = "verb"; map["t"] = "verb";
        map["i"] = "verb"; map["A"] = "adjective"; map["v"] = "adverb"; map["C"] = "conjunction";
        map["P"] = "preposition"; map["!"] = "interjection"; map["r"] = "pronoun"; map["D"] = "determiner";
        map["I"] = "determiner"; map["o"] = "pronoun";
    }
    NF == 2 && $1 !~ / / {
        word = $1; codes = $2;
        key = tolower(word);
        proper = (word ~ /^[A-Z]/);
        n = length(codes);
        for (k = 1; k <= n; k++) {
            c = substr(codes, k, 1);
            if (!(c in map)) continue;
            category = map[c];
            if (category == "noun" && proper) category = "proper noun";
            if (category == "verb" && (key in is_aux)) category = "auxiliary verb";
            seen[key, category] = 1;
            if (!(key in order)) { order[key] = ++count; keys[count] = key }
        }
    }
    END {
        cats[1] = "noun"; cats[2] = "proper noun"; cats[3] = "verb"; cats[4] = "auxiliary verb"; cats[5] = "adjective";
        cats[6] = "adverb"; cats[7] = "pronoun"; cats[8] = "determiner"; cats[9] = "preposition";
        cats[10] = "conjunction"; cats[11] = "interjection";
        for (i = 1; i <= count; i++) {
            key = keys[i]; list = "";
            for (j = 1; j <= 11; j++) if ((key, cats[j]) in seen) list = list (list == "" ? "" : ",") cats[j];
            if (list != "") print key "\t" list;
        }
    }' | LC_ALL=C sort > "$OUT"
printf 'dictionary: %s words in %s\n' "$(wc -l < "$OUT" | tr -d ' ')" "$OUT"

#!/usr/bin/env bash
# Builds the dictionary of a constellation, dictionary/<locale>/words.txt: one
# word per line, a tab, and its categories in Larry's names separated by
# commas. Single words only (hyphens and apostrophes allowed), in lower case, so
# an entry is found by the folded word. The list is a fallback for words no
# conception has taught: see PLAN.md, item A2b.
#
# English comes from the Moby Part-of-Speech list (Grady Ward, public domain,
# Project Gutenberg ebook 3203). A capitalized noun in Moby is a proper noun
# here. Auxiliary verbs are the ones in base_rules/en/auxiliaries.txt.
# Portuguese comes from the word forms of the Universal Dependencies Bosque
# treebank's training set (CC BY-SA 4.0), each with the categories its tags map
# to; a word written as one and analysed as two ("do", "de o") takes the
# category of its first part, as the measurement of A6 does.
#
#   scripts/dictionary.sh [en] [mobypos.txt]     # downloads the list when no file is given
#   scripts/dictionary.sh pt [train.conllu]      # fetches the treebank (scripts/ud.sh pt) when no file is given
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
locale=en
if [ "${1-}" = en ] || [ "${1-}" = pt ]; then
    locale="$1"
    shift
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

if [ "$locale" = pt ]; then
    OUT="$ROOT/dictionary/pt/words.txt"
    mkdir -p "$(dirname "$OUT")"
    if [ -n "${1-}" ]; then
        cp "$1" "$work/train.conllu"
    else
        "$ROOT/scripts/ud.sh" pt > /dev/null
        cp "$ROOT/content/ud/pt_bosque-ud-train.conllu" "$work/train.conllu"
    fi
    # Token lines: the form and its tag. A range line ("3-4 do") is a word
    # written as one: its form takes the tag of the part that follows it. A
    # word starts with a letter (ASCII or a UTF-8 lead byte) and is folded to
    # lower case, the Portuguese capitals with accents by hand. The categories
    # of a word come most counted first; one seen once, or under two in a
    # hundred of the word's uses, is an annotator's slip and is left out.
    awk -F'\t' '
    BEGIN {
        map["NOUN"] = "noun"; map["PROPN"] = "proper noun"; map["VERB"] = "verb"; map["AUX"] = "auxiliary verb";
        map["ADJ"] = "adjective"; map["ADV"] = "adverb"; map["ADP"] = "preposition"; map["DET"] = "determiner";
        map["PRON"] = "pronoun"; map["NUM"] = "numeral"; map["CCONJ"] = "conjunction"; map["SCONJ"] = "conjunction";
        map["INTJ"] = "interjection"; map["PART"] = "particle";
        caps = "É=é À=à Á=á Â=â Ã=ã Ê=ê Í=í Ó=ó Ô=ô Õ=õ Ú=ú Ç=ç";
        n = split(caps, pairs, " ");
        for (k = 1; k <= n; k++) { split(pairs[k], pair, "="); lower[pair[1]] = pair[2] }
    }
    function fold(word,    k) {
        word = tolower(word);
        for (k in lower) gsub(k, lower[k], word);
        return word;
    }
    function is_word(word,    c) {
        c = substr(word, 1, 1);
        return word != "" && word !~ / / && (c ~ /^[A-Za-z]$/ || c > "\177");
    }
    function note(key, category) {
        counts[key, category]++;
        total[key]++;
        if (!(key in order)) { order[key] = ++count; keys[count] = key }
        if (!((key, category) in listed)) { listed[key, category] = 1; cats_of[key] = cats_of[key] category "\n" }
    }
    $1 ~ /^[0-9]+-[0-9]+$/ { pending = is_word($2) ? fold($2) : ""; next }
    $1 ~ /^[0-9]+$/ && NF >= 4 {
        if (!($4 in map) || !is_word($2)) { pending = ""; next }
        note(fold($2), map[$4]);
        if (pending != "") { note(pending, map[$4]); pending = "" }
    }
    END {
        for (i = 1; i <= count; i++) {
            key = keys[i];
            m = split(cats_of[key], list, "\n");
            # Order by count, most first (a few categories: a plain sort).
            for (a = 1; a < m; a++) for (b = a + 1; b <= m; b++)
                if (list[b] != "" && (list[a] == "" || counts[key, list[b]] > counts[key, list[a]])) { t = list[a]; list[a] = list[b]; list[b] = t }
            out = "";
            for (a = 1; a <= m; a++) {
                c = list[a];
                if (c == "") continue;
                if (out != "" && (counts[key, c] < 2 || counts[key, c] * 100 < total[key] * 2)) continue;
                out = out (out == "" ? "" : ",") c;
            }
            if (out != "") print key "\t" out;
        }
    }' "$work/train.conllu" | LC_ALL=C sort > "$OUT"
    printf 'dictionary: %s words in %s\n' "$(wc -l < "$OUT" | tr -d ' ')" "$OUT"
    exit 0
fi

URL="https://www.gutenberg.org/files/3203/files/mobypos.txt"
OUT="$ROOT/dictionary/en/words.txt"
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

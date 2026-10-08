#!/usr/bin/env bash
# Fetches a Universal Dependencies treebank for the measurement of A6 into
# content/ud/: English EWT (en_ewt-ud-train.conllu, en_ewt-ud-test.conllu) or
# Portuguese Bosque (pt_bosque-ud-train.conllu, pt_bosque-ud-test.conllu). The
# files stay out of git (content/ is ignored). Then: larry measure, or
# LARRY_LANGUAGE=pt larry measure.
#
#   scripts/ud.sh [en|pt]        # English when no locale is given
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
dir="$root/content/ud"
mkdir -p "$dir"
case "${1:-en}" in
en) repo=UD_English-EWT; name=en_ewt ;;
pt) repo=UD_Portuguese-Bosque; name=pt_bosque ;;
*) echo "ud.sh: unknown locale ${1}; use en or pt" >&2; exit 1 ;;
esac
for f in "$name-ud-train.conllu" "$name-ud-test.conllu"; do
    if [ ! -s "$dir/$f" ]; then
        echo "fetching $f"
        curl -sSL --max-time 300 -o "$dir/$f" "https://raw.githubusercontent.com/UniversalDependencies/$repo/master/$f"
    fi
    echo "$f: $(grep -c '^# sent_id' "$dir/$f") sentences"
done

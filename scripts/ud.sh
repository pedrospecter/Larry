#!/usr/bin/env bash
# Fetches the Universal Dependencies English treebank (EWT) for the measurement
# of A6: content/ud/en_ewt-ud-train.conllu and en_ewt-ud-test.conllu. The files
# stay out of git (content/ is ignored). Then: larry measure
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
dir="$root/content/ud"
mkdir -p "$dir"
for f in en_ewt-ud-train.conllu en_ewt-ud-test.conllu; do
    if [ ! -s "$dir/$f" ]; then
        echo "fetching $f"
        curl -sSL --max-time 300 -o "$dir/$f" "https://raw.githubusercontent.com/UniversalDependencies/UD_English-EWT/master/$f"
    fi
    echo "$f: $(grep -c '^# sent_id' "$dir/$f") sentences"
done

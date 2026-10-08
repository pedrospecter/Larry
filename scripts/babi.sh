#!/usr/bin/env bash
# Fetches the bAbI tasks (Weston and others, 2015) for the measurements of
# tracks R and A: the test split of each of the 20 tasks, 200 stories each,
# from the rows API of Hugging Face (facebook/babi_qa, config en-qaN), as
# JSON pages of 100 rows, into content/babi/qaN_test.json. The files stay out
# of git (content/ is ignored). Then: larry babi [task]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
dir="$root/content/babi"
mkdir -p "$dir"
for task in $(seq 1 20); do
    for offset in 0 100; do
        f="$dir/qa${task}_test_${offset}.json"
        if [ ! -s "$f" ]; then
            curl -sSL --max-time 120 -o "$f" \
                "https://datasets-server.huggingface.co/rows?dataset=facebook/babi_qa&config=en-qa${task}&split=test&offset=${offset}&length=100"
        fi
    done
    echo "task $task: $(cat "$dir/qa${task}_test_0.json" "$dir/qa${task}_test_100.json" | grep -o '"row_idx"' | wc -l) stories"
done

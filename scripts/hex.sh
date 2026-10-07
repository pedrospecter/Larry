#!/usr/bin/env bash
# Turns a plain list into a base-rule file: one item per line, each byte as two
# hex digits (PLAN.md, section 3, rule 4). Lines that start with '#' and empty
# lines are kept as they are, so a rule file can carry its own comments.
#
#   scripts/hex.sh < plain.txt > base_rules/en/rule.txt
#   scripts/hex.sh --decode < base_rules/en/rule.txt
set -euo pipefail

decode=false
if [ "${1-}" = "--decode" ]; then
    decode=true
fi

while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
    '' | '#'*)
        printf '%s\n' "$line"
        ;;
    *)
        if $decode; then
            # shellcheck disable=SC2059
            printf "$(printf '%s' "$line" | sed 's/../\\x&/g')"
            printf '\n'
        else
            printf '%s' "$line" | od -An -v -tx1 | tr -d ' \n'
            printf '\n'
        fi
        ;;
    esac
done

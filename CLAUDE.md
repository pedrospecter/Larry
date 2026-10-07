# Larry

Read `PLAN.md` before anything else. It is the working plan: what Larry is, the user's vocabulary, the rules for every session (section 3), the build order (section 6) and the open questions (section 10).

- Follow section 3 of the plan in every change. Use the user's terms as they are.
- Take the first unticked item in section 6 whose needs are met. If it needs an open question from section 10, ask the user at the start. If there is no answer, take the next item that is not blocked.
- Before you change anything: run `scripts/setup.sh`, then `cmake -S . -B build && cmake --build build && ctest --test-dir build`. If something fails, fix that first and say so.
- Every item lands with tests. The build and all tests pass before a commit.
- Before you finish: tick the item in section 6, add a line to the log in section 12 with the numbers you measured, add new questions to section 10, and correct anything in `PLAN.md` the work showed to be wrong.

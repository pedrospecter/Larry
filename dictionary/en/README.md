# The English dictionary

`words.txt` is the vocabulary that is always on the machine: 192,960 words,
one per line, a tab, and their categories in Larry's names separated by
commas, sorted, in lower case. Larry reads it when a word no conception has
taught comes up (PLAN.md, A2b), and uses it to suggest the word one typing
slip away from an unknown one.

It is built by `scripts/dictionary.sh` from the Moby Part-of-Speech list
(Grady Ward, 1996), which is in the public domain: Project Gutenberg ebook
3203, https://www.gutenberg.org/ebooks/3203. Moby's codes map to the
categories as the script says; a capitalized noun in Moby is a proper noun
here, and the auxiliary verbs are the ones in `base_rules/en/auxiliaries.txt`.
Phrases of several words are left out.

-- Larry's database (PLAN.md, section 3, rule 3): every table has an id key,
-- and anything Larry reads is a bytea column.

-- Atoms: assumed truths. The neural network finds an atom by its metadata, so
-- the metadata is unique and indexed; bytes is the sentence itself.
create table if not exists atoms (
    id       bigint generated always as identity primary key,
    metadata bytea  not null unique,
    bytes    bytea  not null
);

-- The word index (N1): for each word, the atoms that contain it, the position
-- of the entity in the atom, and the category it has there. The word is
-- stored as the index keys it: ASCII letters in lower case.
create table if not exists words (
    id       bigint  generated always as identity primary key,
    word     bytea   not null,
    atom     bigint  not null references atoms (id) on delete cascade,
    position integer not null,
    category bytea   not null,
    unique (atom, position)
);
create index if not exists words_word on words (word);

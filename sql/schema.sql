-- Atoms: assumed truths. The neural network finds an atom by its metadata, so
-- the metadata is the key and is indexed; bytes is the sentence itself.
create table if not exists atoms (
    metadata bytea primary key,
    bytes    bytea not null
);

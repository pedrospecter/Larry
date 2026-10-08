-- Larry's database: the cloud, the record of conceptions (PLAN.md, section 3,
-- rule 3: every table has an id key, and anything Larry reads is bytea).

-- Conceptions: the atoms Larry holds. The neural network finds one by its
-- identity (Q28): its qualification and its words, taken from the metadata,
-- so a description with corrected types or roles stays the same conception;
-- the metadata is the current description; bytes is the sentence. status is
-- proposed, validated or withdrawn (R2, first step). identity is filled by
-- Larry on first contact for rows from before it existed (schema v3), and
-- rows that turn out to be the same conception are merged then.
create table if not exists conceptions (
    id         bigint      generated always as identity primary key,
    identity   bytea,
    metadata   bytea       not null unique,
    bytes      bytea       not null,
    status     bytea       not null,
    decided_by bytea       not null default '',
    created    timestamptz not null default now()
);
alter table conceptions add column if not exists decided_by bytea not null default '';
alter table conceptions add column if not exists identity bytea;
create unique index if not exists conceptions_identity on conceptions (identity);
-- reading (Q29): the sentence as Larry read it when it was stored, when that
-- differed from what was said; empty when it was read as said.
alter table conceptions add column if not exists reading bytea not null default '';

-- The validators: the only people who validate or withdraw a conception. The
-- user adds themself once; Larry refuses a decision from anyone else.
create table if not exists validators (
    id    bigint      generated always as identity primary key,
    name  bytea       not null unique,
    added timestamptz not null default now()
);

-- Where each conception came from: "lesson:<file>", "user", "read:<file>".
-- A second source validates a proposed conception.
create table if not exists sources (
    id         bigint generated always as identity primary key,
    conception bigint not null references conceptions (id) on delete cascade,
    source     bytea  not null,
    unique (conception, source)
);

-- The word index (N1): for each word, the conceptions that contain it, the
-- position of the entity, the category it has there, and its context: the
-- words before and after, with their categories. The word is stored as the
-- index keys it: ASCII letters in lower case.
create table if not exists words (
    id              bigint  generated always as identity primary key,
    word            bytea   not null,
    conception      bigint  not null references conceptions (id) on delete cascade,
    position        integer not null,
    category        bytea   not null,
    word_before     bytea   not null,
    word_after      bytea   not null,
    category_before bytea   not null,
    category_after  bytea   not null,
    unique (conception, position)
);
create index if not exists words_word on words (word);

-- Bonds (N3): typed links between two ends, each a conception (by its
-- identity, Q28) or an entity (a word as the index keys it), and where each
-- came from: taught ("user:pedro"), or the rule or comparison that produced
-- it ("rule: ..."). The same kind and ends are one bond. A clear keeps them,
-- like the validators: a rebuild forgets atoms, not what was bonded.
create table if not exists bonds (
    id         bigint      generated always as identity primary key,
    kind       bytea       not null,
    from_kind  bytea       not null,
    from_bytes bytea       not null,
    to_kind    bytea       not null,
    to_bytes   bytea       not null,
    created    timestamptz not null default now(),
    unique (kind, from_kind, from_bytes, to_kind, to_bytes)
);
create index if not exists bonds_from on bonds (from_kind, from_bytes);
create index if not exists bonds_to on bonds (to_kind, to_bytes);
create table if not exists bond_origins (
    id     bigint generated always as identity primary key,
    bond   bigint not null references bonds (id) on delete cascade,
    origin bytea  not null,
    unique (bond, origin)
);

-- Molecules (N4): a text or a conversation, as the conceptions it gave, in
-- order, with who said each one and when (ISO 8601 in UTC, as bytes). The
-- name says where it came from and when it began: "read:sky.txt:<when>",
-- "chat:pedro:<when>". A clear keeps them, like the bonds.
create table if not exists molecules (
    id      bigint      generated always as identity primary key,
    name    bytea       not null unique,
    created timestamptz not null default now()
);
create table if not exists molecule_members (
    id       bigint  generated always as identity primary key,
    molecule bigint  not null references molecules (id) on delete cascade,
    position integer not null,
    identity bytea   not null,
    who      bytea   not null,
    said_at  bytea   not null,
    unique (molecule, position)
);
create index if not exists molecule_members_identity on molecule_members (identity);

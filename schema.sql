create table message (
    id integer primary key,
    station varchar(8),
    raw varchar(300),
    originator char(3),
    event char(3),
    issued timestamp without time zone,
    received timestamp without time zone,
    purge timestamp without time zone,
    sender varchar(20),
    filename varchar(30)
);

create table message_area (
    message_id integer references message on delete cascade,
    code char(6),
    part integer,
    state integer,
    county integer
);

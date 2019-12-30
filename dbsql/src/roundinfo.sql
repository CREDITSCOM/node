
CREATE DATABASE roundinfo WITH TEMPLATE = template0;

ALTER DATABASE roundinfo OWNER TO postgres;

\connect roundinfo

CREATE TABLE public_keys (
  id serial PRIMARY KEY,
  public_key character(44) NOT NULL UNIQUE
);

ALTER TABLE public_keys OWNER TO postgres;

CREATE TABLE round_info (
  round_num bigint NOT NULL,
  public_id integer NOT NULL REFERENCES public_keys ON DELETE RESTRICT,
  real_trusted BOOLEAN DEFAULT true
);

ALTER TABLE round_info OWNER TO postgres;

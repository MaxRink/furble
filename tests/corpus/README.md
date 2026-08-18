# Golden capture format

Golden captures are normalized text files. Raw HCI snoops and camera identity
material stay outside the repository. A file starts with a schema header:

```text
FURBLE-GOLDEN-CAPTURE 1
camera=x100vi
source=synthetic
```

Metadata uses `key=value`. Protocol events use four pipe-separated fields:

```text
advertisement|name||FUJIFILM X100VI
advertisement|manufacturer||d80402a1b2c3d4
advertisement|service|af854c2e-b214-458e-97e2-912c4ecf2cb8|
connect|||
subscribe|service-uuid|characteristic-uuid|notification
write|service-uuid|characteristic-uuid|a1b2c3d4
notify|service-uuid|characteristic-uuid|0100
disconnect|||
```

Advertisement fields are `name`, `address`, `manufacturer`, or `service`.
Write and notification payloads are even-length hexadecimal strings. Empty
payloads are valid. Lines beginning with `#` and blank lines are ignored.

The loader is deliberately dependency-free so corpus replay stays a short
host build. The synthetic X100VI file exercises the Fujifilm Basic
advertisement, token write, identifier write, configuration notification, and
geotag request/write path. It is a protocol fixture, not evidence from an
X100VI. Real captures are expected from plan 64's BT journal work.

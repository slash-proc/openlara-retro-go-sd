# third_party

`OpenLara/` is a **git submodule** of [sylverb/OpenLara](https://github.com/sylverb/OpenLara)
on branch `gnw`.

After cloning this repo:

```bash
git submodule update --init --recursive
```

Then ensure the include symlink (also done by `make prepare`):

```bash
ln -sfn ../../../third_party/OpenLara/src/fixed src/platform/gnw/ol
```

GNW / host changes live on the submodule `gnw` branch (applied from
`patches/` during the port). Do not re-apply those patches on top of an
already-patched checkout.

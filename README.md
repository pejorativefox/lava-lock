# lava-lock

An X11 screen locker with an animated lava-lamp or fire background, an Atom One
Dark password field, and PAM authentication. Built on SDL3; the animation is
rendered on the GPU and pauses while the monitors are powered off.

![lava-lock with the lava animation](docs/lava.png)

<details>
<summary>Fire animation (<code>--fire</code>)</summary>

![lava-lock with the fire animation](docs/fire.png)

</details>

## Install

Dependencies (Fedora names):

```sh
sudo dnf install gcc meson ninja-build SDL3-devel libX11-devel libXext-devel pam-devel
```

Build and install:

```sh
meson setup build
meson compile -C build
sudo meson install -C build
```

Then lock with `lava-lock` (or `lava-lock --fire`), or hook it up to
`xss-lock -- lava-lock --fork`. Try it without locking using
`lava-lock --preview --no-auth`, and see `lava-lock --help` for all options.

The embedded bitmap font is derived from Terminus Font, licensed under the SIL
Open Font License 1.1 (`third_party/terminus-OFL.txt`).

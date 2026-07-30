# CHEAP

**C**heapest **H**ierarchical **E**nchant-**A**nvil **P**lanner — a native, compiled
desktop app for the Minecraft anvil-combining problem: given a set of enchanted
books to apply to an item, find the cheapest order to combine them in the
anvil, minimizing either total XP spent or the item's final prior-work penalty.

This is a from-scratch C + GTK4 port of [iamcal/enchant-order](https://github.com/iamcal/enchant-order)
(originally a client-side web app). There is no browser or webview involved —
the optimizer is plain C, and the UI is built with native GTK4 widgets,
producing a single compiled binary.

- `src/enchant_data.{h,c}` — the enchantment/item data table (ported from `data.js`)
- `src/optimizer.{h,c}` — the cost-minimizing search (ported from `work.js`), using
  a bitmask dynamic-programming scheme in place of the original's memoized
  recursion over hashed object lists
- `src/ui.c` — the GTK4 interface
- `resources/icons/` — item and enchanted-book artwork from the original project,
  compiled into the binary via `GResource`

The optimizer was validated against the original JavaScript implementation
(run headlessly under Node) across thousands of randomized scenarios to
confirm identical optimal costs.

## Installing

### Arch Linux (AUR)

```sh
yay -S cheap
```

### Debian / Ubuntu (apt)

```sh
curl -fsSL https://lokifisch.github.io/cheap/cheap-archive-keyring.asc \
  | sudo gpg --dearmor -o /usr/share/keyrings/cheap-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/cheap-archive-keyring.gpg] https://lokifisch.github.io/cheap stable main" \
  | sudo tee /etc/apt/sources.list.d/cheap.list
sudo apt update
sudo apt install cheap
```

`apt upgrade` picks up new releases automatically from then on.

## Building from source

Dependencies: a C11 compiler, CMake (3.16+), and GTK4 development headers
(4.10+) with `pkg-config`.

```sh
# Arch
sudo pacman -S base-devel cmake gtk4

# Debian/Ubuntu
sudo apt install build-essential cmake libgtk-4-dev pkg-config

# Fedora
sudo dnf install gcc cmake gtk4-devel pkgconf-pkg-config

# macOS (Homebrew)
brew install cmake gtk4
```

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/cheap
```

## Packaging

- `packaging/aur/PKGBUILD` — builds from the tagged GitHub release tarball;
  tested locally with `makepkg -si`.
- `.github/workflows/apt-repo.yml` — on every `v*` tag, builds a `.deb` via
  CPack, attaches it to the GitHub release, and publishes a signed apt
  repository to GitHub Pages (`https://lokifisch.github.io/cheap/`).

## License

MIT — see `LICENSE`. Algorithm, data, and icon artwork are derived from the
original `enchant-order` project by Cal Henderson.

# bitpad

A minimal, fast, terminal-based text editor written in C — inspired by [kilo by antirez](https://github.com/antirez/kilo), but customized and extended.

> Works on Linux and macOS, in any terminal. Perfect for learning how terminal editors work under the hood.

---

## Features

- Arrow key navigation
- Status bar with filename and line number
- Open and view text files
- Cross-platform (Linux/macOS)
- Raw terminal control with zero dependencies

---

## Installation

### One-line install (Linux/macOS)

```bash
curl -sL https://github.com/loki760/bitpad/raw/main/install.sh | bash
```
### Build from source

```bash
git clone https://github.com/loki760/bitpad.git
cd bitpad
make          
./bitpad        # run without a file
./bitpad file.txt  # open a file
```



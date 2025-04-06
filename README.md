# Sommateur

I originally wrote Sommateur as a command-line utility for CyberPatriot to identify system file modifications quickly using CRC32 checksums. As I am no longer in the CyberPatriot program, I'm making it public to act as a reliable baseline for competitors working on similar projects.

Sommateur can:

- Generate and display checksums for all files
- Save checksums to a binary baseline file for future comparisons
- Compare current files against a baseline to identify modifications

## Installation

### Releases

- [Linux](https://github.com/gussieIsASuccessfulWarlock/Sommateur/releases/download/v1.0.2/sommateur-linux-latest)
- [Windows](https://github.com/gussieIsASuccessfulWarlock/Sommateur/releases/download/v1.0.2/sommateur-windows-latest.exe)
- [Mac](https://github.com/gussieIsASuccessfulWarlock/Sommateur/releases/download/v1.0.2/sommateur-macos-latest)

### Building from Source on Linux

Prerequisites:

- C++ compiler (g++ recommended) with C++17 support
- `libcurl` development package
- Standard libraries:
  - `iostream`, `fstream`, `sstream`
  - `vector`, `queue`, `unordered_map`, `string`
  - `cstdint`
  - `filesystem`
  - `thread`, `mutex`, `condition_variable`
  - `atomic`, `chrono`, `future`

Compile with:

```bash
g++ -o sommateur sommateur.cpp -lcurl -std=c++17 -pthread
```

This creates the executable `sommateur`.

## Command Syntax

```
sommateur [directory] [options]
```

If no directory is specified, Sommateur defaults to:
- Windows: `C:\`
- Linux/macOS: `/`

## Core Operations

### 1. Display Checksums

Display checksums of all files in a directory:

```bash
sommateur /path/to/directory
```

### 2. Create Baseline

Save checksums to a binary file:

```bash
sommateur /path/to/directory -output baseline_file
```

### 3. Compare Against Baseline

Identify changes by comparing current files with a saved baseline (local file or remote URL):

```bash
sommateur /path/to/directory -checks baseline_file
sommateur /path/to/directory -checks https://example.com/baseline_file
```

## Optional Flags

| Flag             | Alternative         | Description                                             |
|------------------|---------------------|---------------------------------------------------------|
| `-v`             | `-verbose`          | Display all files during comparison (including unchanged) |
| `-np`            | `-no-progress`      | Suppress progress bar                                   |
| `-t [ms]`        | `-timeout [ms]`     | Skip files taking longer than `[ms]` milliseconds       |
| `-h`             | `--help`            | Display help and usage information                      |
| `-o [file]`      | `-output [file]`    | Shorthand for baseline output mode                      |
| `-c [file]`      | `-checks [file]`    | Shorthand for comparison mode                           |

## Use Cases

### Initial Backup Verification

Create a baseline after backups:

```bash
sommateur /backup/directory -output backup_baseline
```

### System Integrity Monitoring

Generate baselines for critical directories:

```bash
sommateur /etc -output etc_baseline
sommateur /bin -output bin_baseline
```

Verify integrity later:

```bash
sommateur /etc -checks etc_baseline
sommateur /bin -checks bin_baseline
```

### Verbose Comparison

Display all files during comparison:

```bash
sommateur /etc -checks etc_baseline -v
```

### Processing Large Files

Skip files taking too long:

```bash
sommateur /home/user -t 1000  # skips files taking longer than 1 second
```

## License

Sommateur is distributed under the Apache License 2.0.

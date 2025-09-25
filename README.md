# duplicator
Automatically symlinks from source directory to target directory.

The current implementation is a small daemon which listens on a path and symlinks directories over another, zero levels deep.

## Usage
```
$ duplicator -l <source_dir> -t <target_dir>
```

## Building
```
$ mkdir build
$ cmake -S . -B build
$ cmake --build build
```

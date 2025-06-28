# Flat File System (FFS)

A simple flat file system simulator implemented for educational purposes.  
This project demonstrates the internal mechanisms of file systems such as inodes and data blocks.

## Objective

The goal is to understand how file systems manage files by manually implementing:
- An inode table
- Data block management
- File metadata and storage structures

The system is **flat**, meaning there is no directory hierarchy. All files exist in a single level.

## How to Run

Compile and execute:

```bash
make
./ffs
```

If successful, you will enter the FFS shell with a prompt like:

```
$ 
```

## Supported Commands

- `import a.txt`  
  Import a file from the Linux file system into FFS. The file `a.txt` must exist in the same directory as the `ffs` executable.

- `del a.txt`  
  Delete a file from FFS. Properly updates the inode and data block bitmaps.

- `ls`  
  List all files currently in FFS. Shows filenames and their sizes.

- `mv a.txt b.txt`  
  Rename a file in FFS.

- `cp a.txt c.txt`  
  Copy a file within FFS.

## File System Layout

The FFS uses a single binary file named `ffs_data` of size 8200 KB.

Layout:

```
+----------------------------+---------------------------+
| Inode Bitmap (4 KB)        | 1024 entries (int[1024])  |
| Data Block Bitmap (4 KB)   | 1024 entries (int[1024])  |
| Inode Table (4096 KB)      | 1024 inodes × 4 KB each   |
| Data Blocks (4096 KB)      | 1024 blocks × 4 KB each   |
+----------------------------+---------------------------+
```

- Block size: 4096 bytes (4 KB)
- Total number of blocks: 2050
- Each file can use:
  - Up to 500 direct blocks
  - One indirect inode pointing to up to 1023 more blocks
- Maximum file size: 4 MB
- Maximum filename length: 256 characters

## File Creation Process in FFS

To store a new file in FFS:
1. Scan the inode bitmap to find a free inode.
2. Scan the data bitmap to find enough free data blocks.
3. Fill the inode with filename, file size, and block pointers.
4. Set the corresponding bits in the inode and data bitmaps.

## Notes

- The file `ffs_data` is persistent across runs.
- FFS does not support subdirectories.
- Importing a file larger than 4 MB is not supported.
- Single-level indirect blocks are used once 500 direct blocks are exhausted.

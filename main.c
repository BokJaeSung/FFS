
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <sys/mman.h>

#define BLOCK_SIZE 4096
#define MAX_FILE_SIZE (4 * 1024 * 1024)
#define MAX_FILENAME 256
#define MAX_DIRECT_BLOCKS 500
#define MAX_INODES 1024
#define MAX_DATABLOCKS 1024

#define FFS_FILENAME "ffs_data"
#define FFS_SIZE (8200 * 1024) // 8388608 bytes

unsigned char *ffs_data = NULL;
int *inode_bitmap;
int *data_bitmap;
unsigned char *inode_table;
unsigned char *data_blocks;


typedef struct
{
    int type; // 0 = metadata inode, 1 = indirect block inode
    char filename[MAX_FILENAME];
    int filesize;
    int direct[MAX_DIRECT_BLOCKS];
    int indirect;
    char padding[1828];
} Inode;

typedef struct
{
    int type;
    int pointers[1023];
} IndirectBlock;

int required_blocks(int filesize)
{
    return (filesize + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

int available_data_blocks()
{
    int count = 0;
    for (int i = 0; i < MAX_DATABLOCKS; i++)
    {
        if (data_bitmap[i] == 0)
            count++;
    }
    return count;
}

void init_ffs_from_file()
{
    int fd = open(FFS_FILENAME, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
    {
        perror("open ffs_data");
        exit(1);
    }

    // Check current file size
    struct stat st;
    if (fstat(fd, &st) != 0)
    {
        perror("fstat");
        close(fd);
        exit(1);
    }

    if (st.st_size != FFS_SIZE)
    {
        if (ftruncate(fd, FFS_SIZE) == -1)
        {
            perror("ftruncate");
            close(fd);
            exit(1);
        }

        unsigned char zero = 0;
        lseek(fd, 0, SEEK_SET);
        for (size_t i = 0; i < FFS_SIZE; i++)
            write(fd, &zero, 1);
        lseek(fd, 0, SEEK_SET);
    }

    ffs_data = mmap(NULL, FFS_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ffs_data == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        exit(1);
    }

    close(fd);
}
void init_ffs()
{
    inode_bitmap = (int *)(ffs_data);
    data_bitmap = (int *)(ffs_data + sizeof(int) * MAX_INODES);
    inode_table = ffs_data + BLOCK_SIZE * 2;
    data_blocks = ffs_data + BLOCK_SIZE * (2 + MAX_INODES);
}

Inode *get_inode(int i)
{
    return (Inode *)(inode_table + i * BLOCK_SIZE);
}

IndirectBlock *get_indirect_block(int i)
{
    return (IndirectBlock *)(inode_table + i * BLOCK_SIZE);
}

int find_free_inode()
{
    int start = rand() % MAX_INODES;
    for (int i = 0; i < MAX_INODES; i++)
    {
        int idx = (start + i) % MAX_INODES;
        if (inode_bitmap[idx] == 0)
            return idx;
    }
    return -1;
}

int find_free_data_block()
{
    int start = rand() % MAX_DATABLOCKS;
    for (int i = 0; i < MAX_DATABLOCKS; i++)
    {
        int idx = (start + i) % MAX_DATABLOCKS;
        if (data_bitmap[idx] == 0)
            return idx;
    }
    return -1;
}
void cmd_import(const char *filename)
{
    struct stat st;
    if (stat(filename, &st) != 0)
    {
        printf("Error: Cannot stat file %s\n", filename);
        return;
    }
    int filesize = st.st_size;
    int need_blocks = required_blocks(filesize);
    if (need_blocks > available_data_blocks())
    {
        printf("Error: Not enough space to import %s\n", filename);
        return;
    }

    if (filesize > MAX_FILE_SIZE)
    {
        printf("Error: File too large\n");
        return;
    }

    int fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        printf("Error: Cannot open file %s\n", filename);
        return;
    }

    int inode_idx = find_free_inode();
    if (inode_idx == -1)
    {
        printf("Error: No free inode\n");
        close(fd);
        return;
    }
    inode_bitmap[inode_idx] = 1;
    Inode *inode = get_inode(inode_idx);
    inode->type = 0;
    strncpy(inode->filename, filename, MAX_FILENAME);
    inode->filesize = filesize;

    char buffer[BLOCK_SIZE];
    int total_read = 0;
    int used_blocks = 0;

    for (int i = 0; i < MAX_DIRECT_BLOCKS && total_read < filesize; i++)
    {
        int db = find_free_data_block();
        if (db == -1)
            break;

        int n = read(fd, buffer, BLOCK_SIZE);
        if (n <= 0)
            break;

        memcpy(data_blocks + db * BLOCK_SIZE, buffer, n);
        inode->direct[i] = db;
        data_bitmap[db] = 1;
        total_read += n;
        used_blocks++;
    }

    if (total_read < filesize)
    {
        int indirect_idx = find_free_inode();
        if (indirect_idx == -1)
        {
            printf("Error: No free inode for indirect\n");
            close(fd);
            return;
        }
        inode->indirect = indirect_idx;
        inode_bitmap[indirect_idx] = 1;

        IndirectBlock *ind = get_indirect_block(indirect_idx);
        ind->type = 1;

        for (int i = 0; i < 1023 && total_read < filesize; i++)
        {
            int db = find_free_data_block();
            if (db == -1)
                break;
            int n = read(fd, buffer, BLOCK_SIZE);
            if (n <= 0)
                break;

            memcpy(data_blocks + db * BLOCK_SIZE, buffer, n);
            ind->pointers[i] = db;
            data_bitmap[db] = 1;
            total_read += n;
        }
    }

    inode_bitmap[inode_idx] = 1;
    close(fd);
}

void cmd_ls()
{
    for (int i = 0; i < MAX_INODES; i++)
    {
        if (inode_bitmap[i] && get_inode(i)->type == 0)
        {
            Inode *inode = get_inode(i);
            printf("%s %d\n", inode->filename, inode->filesize);
        }
    }
}

void cmd_del(const char *filename)
{
    for (int i = 0; i < MAX_INODES; i++)
    {
        if (inode_bitmap[i])
        {
            Inode *inode = get_inode(i);
            if (inode->type == 0 && strcmp(inode->filename, filename) == 0)
            {
                int size = inode->filesize;
                int blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

                for (int j = 0; j < MAX_DIRECT_BLOCKS && blocks > 0; j++, blocks--)
                {
                    data_bitmap[inode->direct[j]] = 0;
                }

                if (blocks > 0 && inode->indirect >= 0)
                {
                    IndirectBlock *ind = get_indirect_block(inode->indirect);
                    for (int j = 0; j < 1023 && blocks > 0; j++, blocks--)
                    {
                        data_bitmap[ind->pointers[j]] = 0;
                    }
                    inode_bitmap[inode->indirect] = 0;
                }

                inode_bitmap[i] = 0;
                return;
            }
        }
    }
    printf("Error: File not found\n");
}

void cmd_mv(const char *src, const char *dest)
{
    for (int i = 0; i < MAX_INODES; i++)
    {
        if (inode_bitmap[i])
        {
            Inode *inode = get_inode(i);
            if (inode->type == 0 && strcmp(inode->filename, src) == 0)
            {
                strncpy(inode->filename, dest, MAX_FILENAME);
                return;
            }
        }
    }
    printf("Error: Source file not found\n");
}
void cmd_cp(const char *src, const char *dest)
{
    int src_idx = -1;

    // Find source inode
    for (int i = 0; i < MAX_INODES; i++)
    {
        if (inode_bitmap[i])
        {
            Inode *inode = get_inode(i);
            if (inode->type == 0 && strcmp(inode->filename, src) == 0)
            {
                src_idx = i;
                break;
            }
        }
    }

    if (src_idx == -1)
    {
        printf("Error: Source file not found\n");
        return;
    }
    Inode *src_inode = get_inode(src_idx);
    int need_blocks = required_blocks(src_inode->filesize);
    if (need_blocks > available_data_blocks())
    {
        printf("Error: Not enough space to copy %s\n", src_inode->filename);
        return;
    }

    int new_idx = find_free_inode();
    if (new_idx == -1)
    {
        printf("Error: No free inode for copy\n");
        return;
    }

    Inode *dst_inode = get_inode(new_idx);

    dst_inode->type = 0;
    dst_inode->filesize = src_inode->filesize;
    strncpy(dst_inode->filename, dest, MAX_FILENAME);
    inode_bitmap[new_idx] = 1;

    int size_remaining = src_inode->filesize;

    // Copy direct blocks
    for (int i = 0; i < MAX_DIRECT_BLOCKS && size_remaining > 0; i++)
    {
        if (src_inode->direct[i] == -1)
            break;

        int new_block = find_free_data_block();
        if (new_block == -1)
        {
            printf("Error: No free data block for copy\n");
            inode_bitmap[new_idx] = 0;
            return;
        }

        memcpy(data_blocks + new_block * BLOCK_SIZE,
               data_blocks + src_inode->direct[i] * BLOCK_SIZE,
               (size_remaining > BLOCK_SIZE) ? BLOCK_SIZE : size_remaining);

        dst_inode->direct[i] = new_block;
        data_bitmap[new_block] = 1;

        size_remaining -= BLOCK_SIZE;
    }

    // Copy indirect blocks (if needed)
    if (size_remaining > 0 && src_inode->indirect != -1)
    {
        int indirect_inode_idx = find_free_inode();
        if (indirect_inode_idx == -1)
        {
            printf("Error: No free inode for indirect copy\n");
            inode_bitmap[new_idx] = 0;
            return;
        }

        inode_bitmap[indirect_inode_idx] = 1;
        dst_inode->indirect = indirect_inode_idx;

        IndirectBlock *src_ind = get_indirect_block(src_inode->indirect);
        IndirectBlock *dst_ind = get_indirect_block(indirect_inode_idx);
        dst_ind->type = 1;

        for (int i = 0; i < 1023 && size_remaining > 0; i++)
        {
            if (src_ind->pointers[i] == -1)
                break;

            int new_block = find_free_data_block();
            if (new_block == -1)
            {
                printf("Error: No free data block for indirect copy\n");
                inode_bitmap[new_idx] = 0;
                inode_bitmap[indirect_inode_idx] = 0;
                return;
            }

            memcpy(data_blocks + new_block * BLOCK_SIZE,
                   data_blocks + src_ind->pointers[i] * BLOCK_SIZE,
                   (size_remaining > BLOCK_SIZE) ? BLOCK_SIZE : size_remaining);

            dst_ind->pointers[i] = new_block;
            data_bitmap[new_block] = 1;

            size_remaining -= BLOCK_SIZE;
        }
    }
}
void cmd_cat(const char *filename)
{
    int inode_index = -1;
    for (int i = 0; i < MAX_INODES; i++)
    {
        if (inode_bitmap[i])
        {
            Inode *inode = get_inode(i);
            if (inode->type == 0 && strcmp(inode->filename, filename) == 0)
            {
                inode_index = i;
                break;
            }
        }
    }

    if (inode_index == -1)
    {
        printf("Error: File '%s' not found in FFS\n", filename);
        return;
    }

    Inode *inode = get_inode(inode_index);
    int remaining = inode->filesize;

    // Print direct blocks
    for (int i = 0; i < MAX_DIRECT_BLOCKS && remaining > 0; i++)
    {
        int block_num = inode->direct[i];
        if (block_num < 0 || block_num >= MAX_DATABLOCKS)
            continue;

        unsigned char *data = data_blocks + block_num * BLOCK_SIZE;
        int to_read = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        fwrite(data, 1, to_read, stdout);
        remaining -= to_read;
    }

    // Print indirect blocks if any
    if (remaining > 0 && inode->indirect != -1)
    {
        IndirectBlock *ind = get_indirect_block(inode->indirect);

        if (ind->type != 1)
        {
            printf("\nError: Indirect block type mismatch\n");
            return;
        }

        for (int i = 0; i < 1023 && remaining > 0; i++)
        {
            int block_num = ind->pointers[i];
            if (block_num < 0 || block_num >= MAX_DATABLOCKS)
                continue;

            unsigned char *data = data_blocks + block_num * BLOCK_SIZE;
            int to_read = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
            fwrite(data, 1, to_read, stdout);
            remaining -= to_read;
        }
    }

    printf("\n");
}
void cmd_debugfs()
{
    printf("=== Inode Bitmap (used=1 / free=0) ===\n");
    for (int i = 0; i < MAX_INODES; i++)
    {
        printf("%d", inode_bitmap[i]);
        if ((i + 1) % 64 == 0)
            printf("\n");
    }

    printf("\n=== Data Block Bitmap (used=1 / free=0) ===\n");
    for (int i = 0; i < MAX_DATABLOCKS; i++)
    {
        printf("%d", data_bitmap[i]);
        if ((i + 1) % 64 == 0)
            printf("\n");
    }

    printf("\n=== Inode Table Summary ===\n");
    for (int i = 0; i < MAX_INODES; i++)
    {
        if (inode_bitmap[i])
        {
            Inode *inode = get_inode(i);
            if (inode->type == 0)
            {
                printf("[Inode %d] FILE: %s | Size: %d | Direct: ", i, inode->filename, inode->filesize);
                for (int j = 0; j < 5 && inode->direct[j] != -1; j++)
                    printf("%d ", inode->direct[j]);
                if (inode->indirect != -1)
                    printf("| Indirect: inode %d", inode->indirect);
                printf("\n");
            }
            else if (inode->type == 1)
            {
                printf("[Inode %d] INDIRECT BLOCK\n", i);
            }
        }
    }
}
int main()
{
    srand(time(NULL));
    init_ffs_from_file();

    char *cmd,
        *arg;
    char line[512];
    init_ffs();
    while (1)
    {
        printf("$ ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
            break;

        // remove trailing newline
        line[strcspn(line, "\n")] = '\0';

        // tokenize input
        cmd = strtok(line, " \t");
        arg = strtok(NULL, " \t");

        if (cmd == NULL)
            continue;

        if (strcmp(cmd, "import") == 0 && arg != NULL)
        {
            cmd_import(arg);
        }
        else if (strcmp(cmd, "del") == 0 && arg != NULL)
        {
            cmd_del(arg);
        }
        else if (strcmp(cmd, "ls") == 0)
        {
            cmd_ls();
        }
        else if (strcmp(cmd, "mv") == 0)
        {
            char *arg2 = strtok(NULL, " \t");
            if (arg && arg2)
                cmd_mv(arg, arg2);
            else
                printf("Usage: mv oldname newname\n");
        }
        else if (strcmp(cmd, "cp") == 0)
        {
            char *arg2 = strtok(NULL, " \t");
            if (arg && arg2)
                cmd_cp(arg, arg2);
            else
                printf("Usage: cp src dest\n");
        }
        else if (strcmp(cmd, "cat") == 0 && arg != NULL)
        {
            cmd_cat(arg);
        }
        else if (strcmp(cmd, "debugfs") == 0)
        {
            cmd_debugfs();
        }
        else
        {
            printf("Unknown command: %s\n", cmd);
        }
    }
    msync(ffs_data, FFS_SIZE, MS_SYNC);
    munmap(ffs_data, FFS_SIZE);
    return 0;
}

#define FUSE_USE_VERSION 26
#define _FILE_OFFSET_BITS 64
#include <stddef.h>
#include <fcntl.h>
#include <string.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <memory.h>
#include <sys/mman.h>
static const size_t SIZE = 256 * 1024 * (size_t)1024;
static void *mem[8*1024];
#define MAX_BITMAP_BLOCK_NUM 1
#define MAX_FILENAME 12
#define MAX_DATA_IN_BLOCK (32*1024-16)
#define BLOCK_BYTES (32*1024)
int count = 0;
long max_filesystem_in_block;

struct super_block {//super_block
    long fs_size;
    long first_blk;
    long bitmap;
};

struct oshfs_inode {
    char fname[MAX_FILENAME + 1];
    size_t fsize;
    long nStartBlock;
    int flag;
};

struct oshfs_data_block {//data_block
    size_t size;
    long nNextBlock;
    char data[MAX_DATA_IN_BLOCK];
};

int oper_write_blk(long blk, struct oshfs_data_block *content)//write_block
{
    mem[blk] = mmap(NULL,BLOCK_BYTES,PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,-1,0);
    *((struct oshfs_data_block *)mem[blk]) = *content;
    return 0;
}

int oper_read_blk(long blk, struct oshfs_data_block *content)//read_block
{
    if(blk == -1)return -1;
    *content = *(struct oshfs_data_block *)mem[blk];
    return 0;
}


int oper_set_blk(long blk, int flag)//set is the block is used
{
    int start, left, f, mask;
    if (blk == -1) {
        return -1;
    }
    start = blk / 8;left = blk % 8;
    mask = 1;mask <<= left;
    void *temp;
    temp = (char *)mem[1]+start;
    f = *(int *)temp;//set the bit
    if (flag) f |= mask;
    else f &= ~mask;
    *(int *)temp = f;
    return 0;
}

int oper_search_free_blk(int num, long *start_blk)//find the free block to write
{
    int temp = 0, max = 0;
    long start,left, i, j;
    unsigned int *flag;
    unsigned int mask, f;
    long max_start = -1;
    *start_blk = 1 + MAX_BITMAP_BLOCK_NUM  + 1;
    while (*start_blk < max_filesystem_in_block - 1) {
        start = *start_blk/8;
        left = *start_blk%8;
        mask = 1;
        mask <<= left;
        void *var;
        var = (char *)mem[1]+start;
        flag = (unsigned int *)var;
        f = *flag;
        for (temp = 0; temp < num; temp++) {
            if ((f & mask) == mask) {
                break;
            }
            if ((mask & 0x80000000) == 0x80000000) {/*read next flag*/
                flag = (unsigned int *)var++;
                f = *flag;
                mask = 1;
            } else {
                mask <<= 1;
            }
        }
        if (temp > max) {
            max = temp;
            max_start = *start_blk;
        }
        if (temp == num) {
            break;
        }
        *start_blk = (temp + 1) + *start_blk;
        temp = 0;
    }
    *start_blk = max_start;
    j = max_start;
    for (i = 0; i < max; ++i) {
        if (oper_set_blk(j++, 1) == -1) {
            return -1;
        }
    }
    return max;
}


int init_new_blk(long blk)//init a new block
{
    int err = 0;
    struct oshfs_data_block *content;
    content = malloc(sizeof(struct oshfs_data_block));
    if (!content) {
        printf("Malloc failed!\n");
        return -1;
    }
    content->size = 0;
    content->nNextBlock = -1;
    strcpy(content->data, "\0");
    if (oper_write_blk(blk, content) == -1) {
        printf("oper_write_blk failed\n");
        err = -1;
    }
    free(content);
    return err;
}



int find_parent(char *path, char **parent, char **fname)//find parent
{
    char *q_temp;
    char *ptr, *c = "/";
    q_temp = path;
    q_temp++;
    ptr = strrchr(q_temp, '/');
    if (ptr) {
        *ptr = '\0';
        ptr++;
        *fname = ptr;
        *parent = path;
    } else {
        *fname = q_temp;
        *parent = c;
    }
    return 0;
}


int is_exist(const char *fname, struct oshfs_data_block **content)//if the file is exist
{
    struct oshfs_inode *dirent;
    int pos = 0;
    dirent = (struct oshfs_inode *)(*content)->data;
    while (1) {
        pos = 0;
        while (dirent->flag != 0 && pos < MAX_DATA_IN_BLOCK) {
            if (strcmp(fname, dirent->fname) == 0) {
                return 1;
            }
            dirent++;
            pos += sizeof(struct oshfs_inode);
        }
        if ((*content)->nNextBlock != -1) {
            if (oper_read_blk((*content)->nNextBlock, *content) == -1) {
                printf("is_exist_oper_read_blk failed! \n");
                return -1;
            }
            dirent = (struct oshfs_inode *)(*content)->data;
        } else {
            return 0;
        }
    }
}


int div_string(char *path, char **surplus, char **fname)//div the path string
{
    char *q_temp;
    char *ptr;
    q_temp = path;
    if (*q_temp == '/') {
        q_temp++;
    }
    ptr = strchr(q_temp, '/');
    if (ptr) {
        *ptr = '\0';
        ptr++;
        *surplus = ptr;
        *fname = q_temp;
    } else {
        *fname = q_temp;
        *surplus = NULL;
    }
    return 0;
}

int find_dirent(char *fname, struct oshfs_data_block *content, struct oshfs_inode **dirent)//find the dirent by fname
{
    int pos;
    *dirent = (struct oshfs_inode *)content->data;
    while (1) {
        pos = 0;
        while (pos < MAX_DATA_IN_BLOCK) {
            if (((*dirent)->flag == 1 || (*dirent)->flag == 2) && strcmp((*dirent)->fname, fname) == 0) {
                return 0;
            }
            (*dirent)++;
            pos += sizeof(struct oshfs_inode);
        }
        if (content->nNextBlock != -1) {
            if (oper_read_blk(content->nNextBlock, content) == -1) {
                printf("find_dirent_oper_read_blk failed! \n");
                return -1;
            }
            *dirent = (struct oshfs_inode *)content->data;
        } else {
            return -1;
        }
    }
}

int oper_open(const char *path, struct oshfs_inode *inode)//open a file
{
    char *subpath, *fname, *path_temp, *temp;
    long start_blk;
    struct super_block *super_block_record;
    struct oshfs_inode *dirent;
    struct oshfs_data_block *content;
    int res = 0;

    content = malloc(sizeof(struct oshfs_data_block));

    path_temp = strdup(path);

    if (!content) {
        printf("Malloc failed!\n");
        res = -ENOMEM;
        goto out;
    }

    if (oper_read_blk(0, content) == -1) {
        printf("oper_open_oper_read_blk failed! \n");
        res = -1;
        goto out;
    }
    super_block_record = (struct super_block *)content;
    start_blk = super_block_record->first_blk;

    if (strcmp(path, "/") == 0) {
        inode->flag = 2;
        inode->nStartBlock = start_blk;
        goto out;
    }

    if (oper_read_blk(start_blk, content) == -1) {
        printf("oper_open_oper_read_blk failed! \n");
        res = -1;
        goto out;
    }
    temp = path_temp;
    while (1) {
        div_string(temp, &subpath, &fname);
        if (find_dirent(fname, content, &dirent) == -1) {
            printf("find_dirent failed! \n");
            res = -1;
            goto out;
        }
        if (subpath == NULL) {
            strcpy(inode->fname, dirent->fname);
            inode->fsize = dirent->fsize;
            inode->nStartBlock = dirent->nStartBlock;
            inode->flag = dirent->flag;
            break;
        } else {
            if (oper_read_blk(dirent->nStartBlock, content) == -1) {
                printf("oper_open_oper_read_blk failed! \n");
                res = -1;
                goto out;
            }
        }
        temp = subpath;
    }

    out:
    if (content) {
        free(content);
    }
    if (path_temp) {
        free(path_temp);
    }
    return res;
}


int oper_modify_attr(const char *path, struct oshfs_inode *inode)
{
    int position = 0;
    char *fname, *parent, *path_temp;
    long start_blk;
    struct oshfs_inode *dirent, *f_inode;
    struct oshfs_data_block *content;
    int res = 0;
    int test_flag = 0;

    content = malloc(sizeof(struct oshfs_data_block));
    f_inode = malloc(sizeof(struct oshfs_inode));

    path_temp = strdup(path);

    if (!content || !f_inode) {
        printf("Malloc failed! \n");
        res = -ENOMEM;
        goto out;
    }

    find_parent(path_temp, &parent, &fname);
    if (oper_open(parent, f_inode) != 0) {
        res = -ENOENT;
        goto out;
    }
    start_blk = f_inode->nStartBlock;
    if (oper_read_blk(start_blk, content) == -1) {
        printf("oper_modify_addr_oper_read_blk failed! \n");
        res = -1;
        goto out;
    }

    while (1) {
        position = 0;
        dirent = (struct oshfs_inode *)content->data;
        while (position < MAX_DATA_IN_BLOCK) {
            if (dirent->flag != 0 && strcmp(fname, dirent->fname) == 0) {
                dirent->fsize = inode->fsize;
                dirent->nStartBlock = inode->nStartBlock;
                dirent->flag = inode->flag;
                test_flag = 1;
                break;
            }
            dirent++;
            position += sizeof(struct oshfs_inode);
        }

        if (test_flag == 0 && content->nNextBlock != -1) {
            start_blk = content->nNextBlock;
            if (oper_read_blk(start_blk, content) == -1) {
                printf("oper_modify_addr_oper_read_blk failed! \n");
                res = -1;
                goto out;
            }
        } else {
            break;
        }
    }

    if (test_flag == 0) {
        res = -ENOENT;
        goto out;
    }

    if (oper_write_blk(start_blk, content) == -1) {
        printf("oper_write_blk failed! \n");
        res = -1;
        goto out;
    }

    out:
    if (content) {
        free(content);
    }
    if (f_inode) {
        free(f_inode);
    }
    if (path_temp) {
        free(path_temp);
    }
    return res;
}

int oper_create(const char *path, int flag)
{
    long p_dir, new_blk;
    char *fname, *parent, *path_temp;
    int offset = 0, res = 0;
    long temp, t;
    struct oshfs_inode *dirent, *inode;
    struct oshfs_data_block *content, *new_block;

    inode = malloc(sizeof(struct oshfs_inode));
    content = malloc(sizeof(struct oshfs_data_block));
    new_block = malloc(sizeof(struct oshfs_data_block));
    path_temp = strdup(path);

    if (!inode || !content ||!new_block) {
        printf("Malloc failed! \n");
        res = -ENOMEM;
        goto out;
    }

    find_parent(path_temp, &parent, &fname);
    if (strlen(fname) > MAX_FILENAME) {
        res = -ENAMETOOLONG;
        goto out;
    }

    if (oper_open(parent, inode) != 0) {
        res = -ENOENT;
        goto out;
    }
    p_dir = inode->nStartBlock;
    if (oper_read_blk(p_dir, content) == -1) {
        printf("oper_create_oper_read_blk failed! \n");
        res = -1;
        goto out;
    }

    if (is_exist(fname, &content)) {
        res = -EEXIST;
        goto out;
    }

    while (1) {
        dirent = (struct oshfs_inode*)content->data;
        offset = 0;
        while (offset < MAX_DATA_IN_BLOCK) {
            if (dirent->flag == 0) {
                if (offset + sizeof(struct oshfs_inode) < MAX_DATA_IN_BLOCK) {
                    goto find;
                } else {
                    break;
                }
            }
            dirent++;
            offset += sizeof(struct oshfs_inode);
        }

        if (content->nNextBlock != -1) {
            p_dir = content->nNextBlock;
            if (oper_read_blk(p_dir, content) == -1) {
                printf("oper_create_oper_read_blk failed! \n");
                res = -1;
                goto out;
            }
        } else {
            goto new;
        }
    }

    find:
    strcpy(dirent->fname, fname);
    dirent->fsize = 0;
    dirent->flag = flag;
    if (oper_search_free_blk(1, &temp) < 1) {
        printf("oper_search_free_blk failed! \n");
        res = -1;
        goto out;
    }
    dirent->nStartBlock = temp;
    if (oper_write_blk(p_dir, content) == -1) {
        printf("oper_write_blk failed! \n");
        res = -1;
        goto out;
    }
    if (init_new_blk(dirent->nStartBlock) == -1) {
        printf("init_new_blk failed! \n");
        res = -1;
        goto out;
    }
    goto out;

    new:
    if (oper_search_free_blk(1, &temp) < 1) {
        printf("oper_search_free_blk failed! \n");
        res = -1;
        goto out;
    }
    new_blk = temp;
    content->nNextBlock = new_blk;
    if (oper_write_blk(p_dir, content) == -1) {
        printf("oper_write_blk failed! \n");
        res = -1;
        goto out;
    }

    new_block->nNextBlock = -1;
    dirent = (struct oshfs_inode*)new_block->data;
    strcpy(dirent->fname, fname);
    dirent->fsize = 0;
    dirent->flag = flag;
    if (oper_search_free_blk(1, &t) < 1) {
        printf("oper_search_free_blk failed! \n");
        res = -1;
        goto out;
    }
    dirent->nStartBlock = t;
    if (oper_write_blk(new_blk, new_block) == -1) {
        printf("oper_write_blk failed! \n");
        res = -1;
        goto out;
    }
    if (init_new_blk(dirent->nStartBlock) == -1) {
        printf("init_new_blk failed! \n");
        res = -1;
        goto out;
    }

    out:
    if (path_temp) {
        free(path_temp);
    }
    if (inode) {
        free(inode);
    }
    if (content) {
        free(content);
    }
    if (new_block) {
        free(new_block);
    }
    return res;
}


int is_empty_dir(const char *path)
{
    long start_blk;
    int position = 0, res = 0;
    int test_flag = 0;
    struct oshfs_inode *dirent, *inode;
    struct oshfs_data_block *content;

    content = malloc(sizeof(struct oshfs_data_block));
    inode = malloc(sizeof(struct oshfs_inode));

    if (!content || !inode) {
        printf("Malloc failed!\n");
        res = -ENOMEM;
        goto out;
    }

    if (oper_open(path, inode) != 0) {
        res = -ENOENT;
        goto out;
    }
    start_blk = inode->nStartBlock;

    if (inode->flag == 1) {
        res = -ENOTDIR;
        goto out;
    }

    if (oper_read_blk(start_blk, content) == -1) {
        printf("is_empty_dir_oper_read_blk failed! \n");
        res = -1;
        goto out;
    }
    dirent = (struct oshfs_inode*)content->data;

    while (1) {
        position = 0;
        while (position < MAX_DATA_IN_BLOCK) {
            if (dirent->flag == 1 || dirent->flag == 2) {
                test_flag = 1;
                goto out;
            }
            dirent++;
            position += sizeof(struct oshfs_inode);
        }
        if (content->nNextBlock != -1) {
            if (oper_read_blk(content->nNextBlock, content) == -1) {
                printf("is_empty_dir_oper_read_blk failed! \n");
                res = -1;
                goto out;
            }
            dirent = (struct oshfs_inode*)content->data;
        } else {
            break;
        }
    }

    if (test_flag == 0) {
        res = 1;
    }

    out:
    if (inode) {
        free(inode);
    }
    if (content) {
        free(content);
    }
    return res;
}




int oper_rm(const char *path, int flag)
{
    long next_blk;
    int res = 0;
    struct oshfs_inode *inode;
    struct oshfs_data_block *content;

    inode = malloc(sizeof(struct oshfs_inode));
    content = malloc(sizeof(struct oshfs_data_block));

    if (!inode || !content) {
        printf("Malloc failed!\n");
        res = -ENOMEM;
        goto out;
    }

    if (oper_open(path, inode) != 0) {
        res = -ENOENT;
        goto out;
    }
    if (flag == 1 && inode->flag == 2) {
        res = -EISDIR;
        goto out;
    } else if (flag == 2 && inode->flag == 1) {
        res = -ENOTDIR;
        goto out;
    }
    next_blk = inode->nStartBlock;
    if (flag == 1 || (flag == 2 && is_empty_dir(path))) {
        while (next_blk != -1) {
            if (oper_set_blk(next_blk, 0) == -1) {
                printf("oper_set_blk failed! \n");
                res = -1;
                goto out;
            }
            if (oper_read_blk(next_blk, content) == -1) {
                printf("oper_rm_oper_read_blk failed! \n");
                res = -1;
                goto out;
            }
            if (init_new_blk(next_blk) == -1) {
                printf("init_new_blk failed! n");
                res = -1;
                goto out;
            }
            munmap(mem[next_blk],BLOCK_BYTES);
            next_blk = content->nNextBlock;
        }
    } else if (!is_empty_dir(path)) {
        res = -ENOTEMPTY;
        goto out;
    }

    inode->flag = 0;
    inode->nStartBlock = -1;
    inode->fsize = 0;

    if (oper_modify_attr(path, inode) != 0) {
        printf("oper_modify_attr failed! \n");
        res = -1;
        goto out;
    }

    out:
    if (inode) {
        free(inode);
    }
    if (content) {
        free(content);
    }
    return res;
}


static int oshfs_getattr(const char *path, struct stat *stbuf)
{
    struct oshfs_inode inode;
    memset(stbuf, 0, sizeof(struct stat));
    if(oper_open(path, &inode) != 0) {
        return -ENOENT;
    }

    if (inode.flag==2) {
        stbuf->st_mode = S_IFDIR | 0666;
    } else if (inode.flag==1) {
        stbuf->st_mode = S_IFREG | 0666;
        stbuf->st_size = inode.fsize;
    }

    return 0;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    struct oshfs_data_block *content;
    struct oshfs_inode *dirent, *inode;
    int position = 0, res = 0;
    char name[MAX_FILENAME + 1];
    long start_blk;

    content = malloc(sizeof(struct oshfs_data_block));
    inode = malloc(sizeof(struct oshfs_inode));

    if (!content || !inode) {
        printf("Malloc failed!\n");
        res = -ENOMEM;
        goto out;
    }

    if (oper_open(path, inode) != 0) {
        res = -ENOENT;
        goto out;
    }

    if(inode->flag == 1) {
        res = -ENOTDIR;
        goto out;
    }
    start_blk = inode->nStartBlock;
    if (oper_read_blk(start_blk, content) == -1) {
        printf("ufs_readdir_oper_read_blk failed!\n");
        res = -1;
        goto out;
    }
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    dirent = (struct oshfs_inode*)content->data;

    while (1) {
        position = 0;
        while( position < MAX_DATA_IN_BLOCK ) {
            strcpy(name, dirent->fname);
            if ((dirent->flag == 1 || dirent->flag == 2) && filler(buf, name, NULL, 0)) {
                break;
            }
            dirent++;
            position += sizeof(struct oshfs_inode);
        }
        if (content->nNextBlock != -1) {
            if (oper_read_blk(content->nNextBlock, content) == -1) {
                printf("ufs_readdir_oper_read_blk failed!\n");
                res = -1;
                goto out;
            }
            dirent = (struct oshfs_inode*)content->data;
        } else {
            break;
        }
    }

    out:
    if (inode) {
        free(inode);
    }
    if (content) {
        free(content);
    }
    return res;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
    struct oshfs_inode inode;
    if (oper_open(path, &inode) != 0) {
        return -ENOENT;
    }
    return 0;
}

static int oshfs_mkdir(const char *path, mode_t mode)
{
    int res = oper_create(path, 2);
    return res;
}

static int oshfs_rmdir(const char *path)
{
    int res = oper_rm(path, 2);
    return res;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res = oper_create(path, 1);
    return res;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    size_t temp;
    int res = 0;
    char *pt;
    int real_offset, blk_num, i;
    size_t ret = 0;
    long start_blk;
    struct oshfs_inode *inode;
    struct oshfs_data_block *content;

    inode = malloc(sizeof(struct oshfs_inode));
    content = malloc(sizeof(struct oshfs_data_block));

    if (!content || !inode) {
        printf("Malloc failed!\n");
        res = -ENOMEM;
        goto out;
    }

    if(oper_open(path,inode) != 0) {
        res = -ENOENT;
        goto out;
    }
    if(inode->flag == 2 ) {
        res = -EISDIR;
        goto out;
    }

    start_blk = inode->nStartBlock;
    if (inode->fsize <= offset) {
        size = 0;
    } else {
        if (offset + size > inode->fsize) {
            size = inode->fsize - offset;
        }

        blk_num = offset / MAX_DATA_IN_BLOCK;
        real_offset = offset % MAX_DATA_IN_BLOCK;
        for (i = 0; i < blk_num; i++ ) {
            if (oper_read_blk(start_blk, content) == -1) {
                printf("ufs_read_oper_read_blk failed!\n");
                res = -1;
                goto out;
            }
            start_blk = content->nNextBlock;
        }
        if (oper_read_blk(start_blk, content) == -1) {
            printf("ufs_read_oper_read_blk failed!\n");
            res = -1;
            goto out;
        }
        temp = size;
        pt = content->data;
        pt += real_offset;
        ret = (MAX_DATA_IN_BLOCK - real_offset < size ? MAX_DATA_IN_BLOCK - real_offset : size);
        memcpy(buf, pt, ret);
        temp -= ret;

        while (temp > 0) {
            if (oper_read_blk(content->nNextBlock, content) == -1) {
                printf("ufs_read_oper_read_blk failed!\n");
                res = -1;
                goto out;
            }
            if (temp > MAX_DATA_IN_BLOCK) {
                memcpy(buf + size - temp, content->data, MAX_DATA_IN_BLOCK);
                temp -= MAX_DATA_IN_BLOCK;
            } else {
                memcpy(buf + size - temp, content->data, temp);
                break;
            }
        }
    }
    res = size;
    out:
    if (inode) {
        free(inode);
    }
    if (content) {
        free(content);
    }
    return res;
}

int find_offset_to_write(struct oshfs_inode *inode, off_t *offset, long *start_blk, struct oshfs_data_block **content)
{
    long blk_num, need_num, real_num, s_num, i, j;
    *start_blk = inode->nStartBlock;
    if (*offset <= inode->fsize) {
        blk_num = (*offset) / MAX_DATA_IN_BLOCK;
        *offset = (*offset) % MAX_DATA_IN_BLOCK;
        for (i = 0; i < blk_num; i++ ) {
            if (oper_read_blk(*start_blk, *content) == -1) {
                printf("find_offset_to_write_oper_read_blk1 failed!\n");
                return -1;
            }
            *start_blk = (*content)->nNextBlock;
        }
        if (oper_read_blk(*start_blk, *content) == -1) {
            printf("find_offset_to_write_oper_read_blk2 failed!\n");
            return -1;
        }
    } else {
        blk_num = inode->fsize / MAX_DATA_IN_BLOCK;
        for (i = 0; i < blk_num; i++ ) {
            if (oper_read_blk(*start_blk, *content) == -1) {
                printf("find_offset_to_write_oper_read_blk3 failed!\n");
                return -1;
            }
            *start_blk = (*content)->nNextBlock;
        }
        if (oper_read_blk(*start_blk, *content) == -1) {
            printf("find_offset_to_write_oper_read_blk4 failed!\n");
            return -1;
        }
        *offset -= (blk_num + 1) * MAX_DATA_IN_BLOCK;
        need_num = (*offset - inode->fsize) / MAX_DATA_IN_BLOCK + 1;
        real_num = oper_search_free_blk(need_num, &s_num);
        if (real_num < 1) {
            printf("oper_search_free_blk failed!\n");
            return -1;
        }
        while (1) {
            for (j = 0; j < real_num; j++) {
                (*content)->nNextBlock = s_num;
                (*content)->size = MAX_DATA_IN_BLOCK;
                if (oper_write_blk(*start_blk, *content) == -1) {
                    printf("oper_write_blk failed!\n");
                    return -1;
                }
                if (oper_read_blk((*content)->nNextBlock, *content) == -1) {
                    printf("find_offset_to_write_oper_read_blk5 failed!\n");
                    return -1;
                }
                *start_blk = s_num;
                s_num = s_num + 1;
                *offset -= MAX_DATA_IN_BLOCK;
            }
            need_num -= real_num;
            if (need_num == 0) {
                break;
            } else {
                real_num = oper_search_free_blk(need_num, &s_num);
                if (real_num < 1) {
                    printf("oper_search_free_blk failed!\n");
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size,
                       off_t offset, struct fuse_file_info *fi)
{
    long start_blk, next_blk, next_b;

    int org_offset = offset;
    size_t ret = 0, total = 0;
    int num, i, res = 0;
    char *pt;
    struct oshfs_inode *inode;
    struct oshfs_data_block *content;

    inode = malloc( sizeof(struct oshfs_inode));
    content = malloc( sizeof(struct oshfs_data_block));

    if (!content || !inode) {
        printf("Malloc failed!\n");
        res = -ENOMEM;
        goto out;
    }

    if (oper_open(path, inode) != 0) {
        res = -ENOENT;
        goto out;
    }

    if (find_offset_to_write(inode, &offset, &start_blk, &content) == -1) {
        printf("find_offset_to_write failed!\n");
        res = -1;
        goto out;
    }
    pt = content->data;
    pt += offset;
    ret = (MAX_DATA_IN_BLOCK - offset < size ? MAX_DATA_IN_BLOCK - offset : size);
    strncpy(pt, buf, ret);
    buf += ret;
    content->size += ret;
    total += ret;
    size -= ret;

    if(size > 0) {
        num = oper_search_free_blk(size/MAX_DATA_IN_BLOCK + 1, &next_b);
        if (num < 0) {
            printf("oper_search_free_blk failed!\n");
            res = -1;
            goto out;
        }
        content->nNextBlock = next_b;
        if (oper_write_blk(start_blk, content) == -1) {
            printf("oper_write_blk failed!\n");
            res = -1;
            goto out;
        }
        while(1) {
            for(i = 0; i < num; ++i) {
                ret = (MAX_DATA_IN_BLOCK < size ? MAX_DATA_IN_BLOCK : size);
                content->size = ret;
                strncpy(content->data, buf, ret);
                buf += ret;
                size -= ret;
                total += ret;
                if(size == 0) {
                    content->nNextBlock = -1;
                } else {
                    content->nNextBlock = next_b + 1;
                }
                if (oper_write_blk(next_b, content) == -1) {
                    printf("oper_write_blk failed!\n");
                    res = -1;
                    goto out;
                }
                next_b = next_b + 1;
            }
            if(size == 0) {
                break;
            }
            num = oper_search_free_blk( size/MAX_DATA_IN_BLOCK + 1, &next_b);
            if (num < 0) {
                printf("oper_search_free_blk failed!\n");
                res = -1;
                goto out;
            }
        }
    } else if(size == 0){
        next_blk = content->nNextBlock;
        content->nNextBlock = -1;
        if (oper_write_blk(start_blk, content) == -1) {
            printf("oper_write_blk failed!\n");
            res = -1;
            goto out;
        }
        while(next_blk != -1) {
            if (oper_set_blk(next_blk, 0) == -1) {
                printf("oper_set_blk failed!\n");
                res = -1;
                goto out;
            }
            if (oper_read_blk(next_blk, content) == -1) {
                printf("oshfs_write_oper_read_blk failed!\n");
                res = -1;
                goto out;
            }
            next_blk = content->nNextBlock;
        }
    }
    size = total;
    if (inode->fsize < org_offset + size) {
        inode->fsize = org_offset + size;
        if (oper_modify_attr(path, inode) != 0) {
            printf("oper_modify_attr failed!\n");
            res = -1;
            goto out;
        }
    }
    res = size;
    out:
    if (inode) {
        free(inode);
    }
    if (content) {
        free(content);
    }

    return res;
}

static int oshfs_unlink(const char *path)
{
    int res = oper_rm(path,1);
    return res;
}

void *oshfs_init (struct fuse_conn_info *conn)
{
    max_filesystem_in_block = ((struct super_block *)mem[0])->fs_size;
    return (long*)max_filesystem_in_block;
}

static struct fuse_operations oshfs_oper = {
        .getattr	= oshfs_getattr,
        .readdir	= oshfs_readdir,
        .mkdir		= oshfs_mkdir,
        .rmdir		= oshfs_rmdir,
        .mknod		= oshfs_mknod,
        .read		= oshfs_read,
        .write		= oshfs_write,
        .unlink		= oshfs_unlink,
        .open		= oshfs_open,
        .init		= oshfs_init,
};

int main(int argc, char *argv[])
{
    struct super_block *super_block_record;
    struct oshfs_data_block *root;
    super_block_record = malloc(sizeof(struct super_block));
    root = malloc(sizeof(struct oshfs_data_block));
    if (!super_block_record || !root) {
        printf("Malloc failed!\n");
        free(super_block_record);
        free(root);
        return  -ENOMEM;
    }
    mem[0] = mmap(NULL,BLOCK_BYTES*3,PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for(int i = 1;i < 3; i++)
    {
        mem[i] = (char *)mem[0] + BLOCK_BYTES * i;
        memset(mem[i],0,BLOCK_BYTES);
    }
    super_block_record->fs_size = SIZE/BLOCK_BYTES;
    super_block_record->first_blk = 1 + MAX_BITMAP_BLOCK_NUM;
    super_block_record->bitmap = MAX_BITMAP_BLOCK_NUM;
    *((struct super_block *)mem[0]) = *super_block_record;
    *((char *)mem[1]) = 0xe0;
    for(int i = 1;i < 32*1024; i++)
        *((char *)mem[1]+i) = 0;
    root->size = 0;
    root->nNextBlock = -1;
    root->data[0] = '\0';
    *((struct oshfs_data_block *)mem[MAX_BITMAP_BLOCK_NUM+1]) = *root;
    if (super_block_record) {
        free(super_block_record);
    }
    if (root) {
        free(root);
    }
    return fuse_main(argc, argv, &oshfs_oper, NULL);
}

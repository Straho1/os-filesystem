#include "../include/newfs.h"

extern struct newfs_super newfs_super;
extern struct custom_options newfs_options;

/**
 * @brief 获取文件名
 *
 * @param path
 * @return char*
 */
char *newfs_get_fname(const char *path)
{
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}
/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path
 * @return int
 */
int newfs_calc_lvl(const char *path)
{
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char *str = path;
    int lvl = 0;
    if (strcmp(path, "/") == 0)
    {
        return lvl;
    }
    while (*str != NULL)
    {
        if (*str == '/')
        {
            lvl++;
        }
        str++;
    }
    return lvl;
}
/**
 * @brief 驱动读
 *
 * @param offset
 * @param out_content
 * @param size
 * @return int
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size)
{
    int offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLOCK_SZ());
    int bias = offset - offset_aligned;
    int size_aligned = NEWFS_ROUND_UP((size + bias), NEWFS_BLOCK_SZ());
    uint8_t *temp_content = (uint8_t *)malloc(size_aligned);
    uint8_t *cur = temp_content;
    // lseek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        ddriver_read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NEWFS_ERROR_NONE;
}
/**
 * @brief 驱动写
 *
 * @param offset
 * @param in_content
 * @param size
 * @return int
 */
int newfs_driver_write(int offset, uint8_t *in_content, int size)
{
    int offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLOCK_SZ());
    int bias = offset - offset_aligned;
    int size_aligned = NEWFS_ROUND_UP((size + bias), NEWFS_BLOCK_SZ());
    uint8_t *temp_content = (uint8_t *)malloc(size_aligned);
    uint8_t *cur = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);

    // lseek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        ddriver_write(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();
    }

    free(temp_content);
    return NEWFS_ERROR_NONE;
}
/**
 * @brief 为一个inode分配dentry，采用头插法
 *
 * @param inode
 * @param dentry
 * @return int
 */
int newfs_alloc_dentry(struct newfs_inode *inode, struct newfs_dentry *dentry)
{
    if (inode->dentrys == NULL)
    {
        inode->dentrys = dentry;
    }
    else
    {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    return inode->dir_cnt;
}
/**
 * @brief 分配一个inode，占用位图
 *
 * @param dentry 该dentry指向分配的inode
 * @return newfs_inode
 */
struct newfs_inode *newfs_alloc_inode(struct newfs_dentry *dentry)
{
    struct newfs_inode *inode;
    int byte_cursor = 0;
    int bit_cursor = 0;
    int ino_cursor = 0;
    boolean is_find_free_entry = FALSE;

    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_inode_blks);
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
        {
            if ((newfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0)
            {
                /* 当前ino_cursor位置空闲 */
                newfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry)
        {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == newfs_super.max_ino)
        return -NEWFS_ERROR_NOSPACE;

    inode = (struct newfs_inode *)malloc(sizeof(struct newfs_inode));
    inode->ino = ino_cursor;
    inode->size = 0;

    /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino = inode->ino;
    /* inode指回dentry */
    inode->dentry = dentry;

    inode->dir_cnt = 0;
    inode->dentrys = NULL;

    return inode;
}
/**
 * @brief 查找当前data_map中的空闲位置
 *
 * @param inode
 * @param bcnt
 */
void newfs_find_free_block(struct newfs_inode *inode, int bcnt)
{
    int byte_cursor, bit_cursor, bno_cursor = 0;
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_data_blks);
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
        {
            if ((newfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0)
            {
                /* 当前bno_cursor位置空闲 */
                newfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                inode->bno[bcnt] = bno_cursor;
                return;
            }
            bno_cursor++;
        }
    }
    free(inode);
    return -NEWFS_ERROR_NOSPACE;
}
/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 *
 * @param inode
 * @return int
 */
int newfs_sync_inode(struct newfs_inode *inode)
{
    struct newfs_inode_d inode_d;
    struct newfs_dentry *dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino = inode->ino;
    inode_d.ino = ino;
    inode_d.size = inode->size;
    memcpy(inode_d.target_path, inode->target_path, NEWFS_MAX_FILE_NAME);
    inode_d.ftype = inode->dentry->ftype;
    inode_d.dir_cnt = inode->dir_cnt;

    /* Cycle 1: 写 INODE */
    /* Cycle 2: 写 数据 */
    int bcnt, offset;
    if (NEWFS_IS_DIR(inode))
    {
        dentry_cursor = inode->dentrys;
        bcnt = 0;
        while (bcnt < NEWFS_DATA_PER_FILE && dentry_cursor != NULL)
        {
            newfs_find_free_block(inode, bcnt);
            offset = NEWFS_DATA_OFS(inode->bno[bcnt]);
            while (dentry_cursor != NULL && offset < NEWFS_DATA_OFS((inode->bno[bcnt] + 1)))
            {
                memcpy(dentry_d.fname, dentry_cursor->fname, NEWFS_MAX_FILE_NAME);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                if (newfs_driver_write(offset, (uint8_t *)&dentry_d,
                                       sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE)
                {
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return -NEWFS_ERROR_IO;
                }

                if (dentry_cursor->inode != NULL)
                {
                    newfs_sync_inode(dentry_cursor->inode);
                }

                dentry_cursor = dentry_cursor->brother;
                offset += sizeof(struct newfs_dentry_d);
            }
            bcnt++;
        }
    }
    else if (NEWFS_IS_REG(inode))
    {
        for (bcnt = 0; bcnt < NEWFS_DATA_PER_FILE; bcnt++)
        {
            newfs_find_free_block(inode, bcnt);
            inode->data_block_pointer[bcnt] = (uint8_t *)malloc(NEWFS_BLOCK_SZ());
            if (newfs_driver_write(NEWFS_DATA_OFS(inode->bno[bcnt]), inode->data_block_pointer[bcnt],
                                   NEWFS_BLOCK_SZ()) != NEWFS_ERROR_NONE)
            {
                NEWFS_DBG("[%s] io error\n", __func__);
                return -NEWFS_ERROR_IO;
            }
            if (inode->size < (bcnt + 1) * NEWFS_BLOCK_SZ())
                break;
        }
    }

    for (bcnt = 0; bcnt < NEWFS_DATA_PER_FILE; bcnt++)
    {
        inode_d.bno[bcnt] = inode->bno[bcnt];
    }
    if (newfs_driver_write(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d,
                           sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE)
    {
        NEWFS_DBG("[%s] io error\n", __func__);
        return -NEWFS_ERROR_IO;
    }

    return NEWFS_ERROR_NONE;
}
/**
 * @brief
 *
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct newfs_inode*
 */
struct newfs_inode *newfs_read_inode(struct newfs_dentry *dentry, int ino)
{
    struct newfs_inode *inode = (struct newfs_inode *)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry *sub_dentry;
    struct newfs_dentry_d dentry_d;
    int dir_cnt = 0, bcnt = 0, offset = 0;
    if (newfs_driver_read(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d,
                          sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE)
    {
        NEWFS_DBG("[%s] io error\n", __func__);
        return NULL;
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    for (bcnt = 0; bcnt < NEWFS_DATA_PER_FILE; bcnt++)
        inode->bno[bcnt] = inode_d.bno[bcnt];
    inode->size = inode_d.size;
    memcpy(inode->target_path, inode_d.target_path, NEWFS_MAX_FILE_NAME);
    inode->dentry = dentry;
    inode->dentrys = NULL;

    if (NEWFS_IS_DIR(inode))
    {
        dir_cnt = inode_d.dir_cnt;
        bcnt = 0;
        while (dir_cnt > 0 && bcnt < NEWFS_DATA_PER_FILE)
        {
            offset = NEWFS_DATA_OFS(inode->bno[bcnt]);
            while (dir_cnt > 0 && offset < NEWFS_DATA_OFS((inode->bno[bcnt] + 1)))
            {
                if (newfs_driver_read(offset,
                                      (uint8_t *)&dentry_d,
                                      sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE)
                {
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return NULL;
                }
                sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
                sub_dentry->parent = inode->dentry;
                sub_dentry->ino = dentry_d.ino;
                newfs_alloc_dentry(inode, sub_dentry);
                offset += sizeof(struct newfs_dentry_d);
                dir_cnt--;
            }
            bcnt++;
        }
    }
    else if (NEWFS_IS_REG(inode))
    {
        for (bcnt = 0; bcnt < NEWFS_DATA_PER_FILE; bcnt++)
        {
            inode->data_block_pointer[bcnt] = (uint8_t *)malloc(NEWFS_BLOCK_SZ());
            if (newfs_driver_read(NEWFS_DATA_OFS(inode->bno[bcnt]), (uint8_t *)inode->data_block_pointer[bcnt],
                                  NEWFS_BLOCK_SZ()) != NEWFS_ERROR_NONE)
            {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;
            }
        }
    }
    return inode;
}
/**
 * @brief
 *
 * @param inode
 * @param dir [0...]
 * @return struct newfs_dentry*
 */
struct newfs_dentry *newfs_get_dentry(struct newfs_inode *inode, int dir)
{
    struct newfs_dentry *dentry_cursor = inode->dentrys;
    int cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt)
        {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}
/**
 * @brief
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *
 * @param path
 * @return struct newfs_inode*
 */
struct newfs_dentry *newfs_lookup(const char *path, boolean *is_find, boolean *is_root)
{
    struct newfs_dentry *dentry_cursor = newfs_super.root_dentry;
    struct newfs_dentry *dentry_ret = NULL;
    struct newfs_inode *inode;
    int total_lvl = newfs_calc_lvl(path);
    int lvl = 0;
    boolean is_hit;
    char *fname = NULL;
    char *path_cpy = (char *)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0)
    { /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = newfs_super.root_dentry;
    }
    fname = strtok(path_cpy, "/");
    while (fname)
    {
        lvl++;
        if (dentry_cursor->inode == NULL)
        { /* Cache机制 */
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (NEWFS_IS_REG(inode) && lvl < total_lvl)
        {
            NEWFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (NEWFS_IS_DIR(inode))
        {
            dentry_cursor = inode->dentrys;
            is_hit = FALSE;

            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0)
                {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }

            if (!is_hit)
            {
                *is_find = FALSE;
                NEWFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl)
            {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/");
    }

    if (dentry_ret->inode == NULL)
    {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }

    return dentry_ret;
}
/**
 * @brief 挂载newfs, Layout 如下
 *
 * Layout
 * | Super | Inode Map | DATA Map | Data |
 *
 * IO_SZ = BLK_SZ / 2
 *
 * 每个Inode占用1个Blk
 * @param options
 * @return int
 */
int newfs_mount(struct custom_options options)
{
    int ret = NEWFS_ERROR_NONE;
    int driver_fd;
    struct newfs_super_d newfs_super_d;
    struct newfs_dentry *root_dentry;
    struct newfs_inode *root_inode;

    int inode_num;
    int data_num;
    int map_inode_blks;
    int map_data_blks;

    int super_blks;
    boolean is_init = FALSE;

    newfs_super.is_mounted = FALSE;

    // driver_fd = open(options.device, O_RDWR);
    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0)
    {
        return driver_fd;
    }

    newfs_super.driver_fd = driver_fd;
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_SIZE, &newfs_super.sz_disk);
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &newfs_super.sz_io);

    root_dentry = new_dentry("/", NEWFS_DIR);

    if (newfs_driver_read(NEWFS_SUPER_OFS, (uint8_t *)(&newfs_super_d),
                          sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    /* 读取super */
    if (newfs_super_d.magic_num != NEWFS_MAGIC_NUM)
    { /* 幻数无 */
        /* 估算各部分大小 */
        super_blks = 1;

        inode_num = 512;
        data_num = 2048;

        map_inode_blks = 1;
        map_data_blks = 1;

        /* 布局layout */
        newfs_super.max_ino = inode_num;
        newfs_super.max_data = data_num;

        newfs_super_d.map_inode_offset = NEWFS_SUPER_OFS + NEWFS_BLKS_SZ(super_blks);
        newfs_super_d.map_data_offset = newfs_super_d.map_inode_offset + NEWFS_BLKS_SZ(map_inode_blks);

        newfs_super_d.inode_offset = newfs_super_d.map_data_offset + NEWFS_BLKS_SZ(map_data_blks);
        newfs_super_d.data_offset = newfs_super_d.inode_offset + NEWFS_BLKS_SZ(inode_num);

        newfs_super_d.map_inode_blks = map_inode_blks;
        newfs_super_d.map_data_blks = map_data_blks;

        newfs_super_d.sz_usage = 0;
        newfs_super_d.max_ino = inode_num;
        newfs_super_d.max_data = data_num;

        NEWFS_DBG("inode map blocks: %d\n", map_inode_blks);
        is_init = TRUE;
    }
    newfs_super.sz_usage = newfs_super_d.sz_usage; /* 建立 in-memory 结构 */

    newfs_super.map_inode = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks));
    newfs_super.map_data = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_data_blks));

    newfs_super.map_inode_blks = newfs_super_d.map_inode_blks;
    newfs_super.map_inode_offset = newfs_super_d.map_inode_offset;
    newfs_super.inode_offset = newfs_super_d.inode_offset;

    newfs_super.map_data_blks = newfs_super_d.map_data_blks;
    newfs_super.map_data_offset = newfs_super_d.map_data_offset;
    newfs_super.data_offset = newfs_super_d.data_offset;

    if (newfs_driver_read(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode),
                          NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    if (newfs_driver_read(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data),
                          NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }

    if (is_init)
    { /* 分配根节点 */
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode);
    }

    root_inode = newfs_read_inode(root_dentry, NEWFS_ROOT_INO);
    root_dentry->inode = root_inode;
    newfs_super.root_dentry = root_dentry;
    newfs_super.is_mounted = TRUE;

    // newfs_dump_map();
    return ret;
}
/**
 * @brief
 *
 * @return int
 */
int newfs_umount()
{
    struct newfs_super_d newfs_super_d;

    if (!newfs_super.is_mounted)
    {
        return NEWFS_ERROR_NONE;
    }

    newfs_sync_inode(newfs_super.root_dentry->inode); /* 从根节点向下刷写节点 */

    newfs_super_d.magic_num = NEWFS_MAGIC_NUM;

    newfs_super_d.map_inode_blks = newfs_super.map_inode_blks;
    newfs_super_d.map_inode_offset = newfs_super.map_inode_offset;
    newfs_super_d.inode_offset = newfs_super.inode_offset;

    newfs_super_d.map_data_blks = newfs_super.map_data_blks;
    newfs_super_d.map_data_offset = newfs_super.map_data_offset;
    newfs_super_d.data_offset = newfs_super.data_offset;

    newfs_super_d.sz_usage = newfs_super.sz_usage;

    if (newfs_driver_write(NEWFS_SUPER_OFS, (uint8_t *)&newfs_super_d,
                           sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }

    if (newfs_driver_write(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode),
                           NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    if (newfs_driver_write(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data),
                           NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    // newfs_dump_map();

    free(newfs_super.map_inode);
    free(newfs_super.map_data);
    ddriver_close(NEWFS_DRIVER());

    return NEWFS_ERROR_NONE;
}

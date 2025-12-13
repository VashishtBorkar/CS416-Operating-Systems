/*
 *  Copyright (C) 2025 CS416 Rutgers CS
 *	Rutgers Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "rufs.h"

#define DEBUG 1
#define debugf(fmt, ...) \
    do { if (DEBUG) fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); } while (0)

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
struct superblock sb;

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	char buffer[BLOCK_SIZE];
	if (bio_read(sb.i_bitmap_blk, buffer) < 0) {
		return -EIO;
	}

	bitmap_t b = (bitmap_t)buffer;

	// Step 2: Traverse inode bitmap to find an available slot
	for (int i = 0; i < sb.max_inum; i++) {
		if (get_bitmap(b, i) == 0) {
			// Step 3: Update inode bitmap and write to disk 
			set_bitmap(b, i);
			bio_write(sb.i_bitmap_blk, buffer);
			return i;
		}
	}
	return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	char buffer[BLOCK_SIZE];
	if (bio_read(sb.d_bitmap_blk, buffer) < 0) {
		return -EIO;
	}
	bitmap_t b = (bitmap_t)buffer;
	
	// Step 2: Traverse data block bitmap to find an available slot
	for (int i = 0; i < sb.max_dnum; i++) {
		if (get_bitmap(b, i) == 0) {
			// Step 3: Update data block bitmap and write to disk
			set_bitmap(b, i);
			bio_write(sb.d_bitmap_blk, buffer);
			return sb.d_start_blk + i;
		}
	}
	return -1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

	if (ino >= sb.max_inum) {
        return -ENOENT;
    }

	// Step 1: Get the inode's on-disk block number
	int inodes_per_block = BLOCK_SIZE / sizeof(struct inode);
	int block_num = sb.i_start_blk + (ino / inodes_per_block);

	// Step 2: Get offset of the inode in the inode on-disk block
	int offset = (ino % inodes_per_block) * sizeof(struct inode);

	// Step 3: Read the block from disk and then copy into inode structure
	char blockbuffer[BLOCK_SIZE];
	if (bio_read(block_num, blockbuffer) < 0) {
		return -EIO;
	}
	memcpy(inode, blockbuffer + offset, sizeof(struct inode));

	if (!inode->valid) {
		return -ENOENT;
	}

	return 0;
}

int writei(uint16_t ino, struct inode *inode) {
	if (ino >= sb.max_inum) {
		return -1;
	}
	
	// Step 1: Get the block number where this inode resides on disk
	int inodes_per_block = BLOCK_SIZE / sizeof(struct inode);
	int block_num = sb.i_start_blk + (ino / inodes_per_block);
	
	// Step 2: Get the offset in the block where this inode resides on disk
	int offset = (ino % inodes_per_block) * sizeof(struct inode);

	// Step 3: Write inode to disk 
	char blockbuffer[BLOCK_SIZE];
	if (bio_read(block_num, blockbuffer) < 0) {
		return -EIO;
	}
	memcpy(blockbuffer + offset, inode, sizeof(struct inode));

	if (bio_write(block_num, blockbuffer) < 0) {
		return -EIO;
	}

	return 0;
}

/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

	if (name_len > 208) {
        return -1;
    }

	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode dir_inode;
	if (readi(ino, &dir_inode) < 0) {
		return -1;
	}

	if (!dir_inode.valid) {
		return -1;
	}

	size_t ents_per_blk = BLOCK_SIZE / sizeof(struct dirent);

	for (int i = 0; i < 16; i++) {
		// Step 2: Get data block of current directory from inode
		int blk = dir_inode.direct_ptr[i];
		if (blk == 0) continue;

		uint8_t buffer[BLOCK_SIZE];
		if (bio_read(blk, buffer) < 0) return -EIO;

		// Step 3: Read directory's data block and check each directory entry.
		struct dirent *tab = (struct dirent *)buffer;
		for (size_t j = 0; j < ents_per_blk; j++) {
			if (!tab[j].valid) continue;

			// copy name to dirent
			if (tab[j].len == (uint16_t)name_len &&
				strncmp(tab[j].name, fname, name_len) == 0) {
				if (dirent) {
					*dirent = tab[j];
				}
				return 0; // found
			}
		}
	}

	return -ENONET; // not found in any block
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
	
	if (name_len > 208) {
        return -1;
    }

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	// Step 2: Check if fname (directory name) is already used in other entries
	if (dir_find(dir_inode.ino, fname, name_len, NULL) == 0) {
        return -1; // already exists
    }
	size_t ents_per_blk = BLOCK_SIZE / sizeof(struct dirent);
    uint8_t buffer[BLOCK_SIZE];
	
	for (int i = 0; i < 16; i++) {
    	int blk = dir_inode.direct_ptr[i];
    	if (blk == 0) continue; // unused slot

		uint8_t buffer[BLOCK_SIZE];
		if (bio_read(blk, buffer) < 0) return -1;

		struct dirent *tab = (struct dirent *)buffer;
		for (size_t j = 0; j < ents_per_blk; j++) {
			// Step 3: Add directory entry in dir_inode's data block and write to disk
			if (!tab[j].valid) {
				tab[j].valid = 1;
				tab[j].ino = f_ino;
				tab[j].len = (uint16_t)name_len;
				memcpy(tab[j].name, fname, name_len);

				if (bio_write(blk, buffer) < 0) return -EIO;

				dir_inode.size += sizeof(struct dirent);
				dir_inode.vstat.st_mtime = time(NULL);

				writei(dir_inode.ino, &dir_inode);
				return 0;
			}
		}
	}

	// Allocate a new data block for this directory if it does not exist
	int new_blk = get_avail_blkno();
    if (new_blk < 0)
        return -1;

	// Update directory inode
    for (int i = 0; i < 16; i++) {
        if (dir_inode.direct_ptr[i] == 0) {
            dir_inode.direct_ptr[i] = new_blk;
            break;
        }
        if (i == 15)
            return -1; // directory full
    }

	// Write directory entry
	memset(buffer, 0, BLOCK_SIZE);
    struct dirent *tab = (struct dirent *)buffer;

    tab[0].ino = f_ino;
    tab[0].valid = 1;
    tab[0].len = name_len;
    memcpy(tab[0].name, fname, name_len);

    if (bio_write(new_blk, buffer) < 0)
        return -EIO;

    // Update directory inode
    dir_inode.size += sizeof(struct dirent);
    dir_inode.vstat.st_mtime = time(NULL);

    writei(dir_inode.ino, &dir_inode);

	return 0;
}


/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	if (strcmp(path, "/") == 0) {
        if (readi(ino, inode) < 0) {
            return -1;
        }
        return 0;
    }

	struct inode cur;
    if (readi(ino, &cur) < 0) {
        return -1;
    }

	char path_copy[PATH_MAX];
    strncpy(path_copy, path, PATH_MAX - 1);
    path_copy[PATH_MAX - 1] = '\0';

	char *p = path_copy;
    if (*p == '/') {
        p++;
    }

	char *token = strtok(p, "/");
    struct dirent dent;

	while (token != NULL) {
        if (!S_ISDIR(cur.vstat.st_mode)) {
            return -1;
        }
        size_t name_len = strlen(token);
        if (dir_find(cur.ino, token, name_len, &dent) < 0) {
            return -1;
        }
        if (readi(dent.ino, &cur) < 0) {
            return -1;
        }
        token = strtok(NULL, "/");
    }
	if (inode != NULL) {
        *inode = cur;
    }
	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {
	debugf("Formatting new filesystem at %s", diskfile_path);
	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);
	debugf("Disk created");

	// write superblock information
	struct superblock new_sb;
	memset(&new_sb, 0, sizeof(struct superblock));
	new_sb.magic_num = MAGIC_NUM;
	new_sb.max_inum = MAX_INUM;
	new_sb.max_dnum = MAX_DNUM;

	int inodes_per_block = BLOCK_SIZE / sizeof(struct inode);
	int inode_blocks = (MAX_INUM + inodes_per_block - 1) / inodes_per_block;

	new_sb.i_bitmap_blk = 1;
	new_sb.d_bitmap_blk = 2;
	new_sb.i_start_blk = 3;
	new_sb.d_start_blk = new_sb.i_start_blk + inode_blocks;

	char blockbuffer[BLOCK_SIZE];
	memset(blockbuffer, 0, BLOCK_SIZE);
	memcpy(blockbuffer, &new_sb, sizeof(struct superblock));
	if(bio_write(0, blockbuffer) < 0) {
		return -1;
	}

	// initialize inode bitmap and set bitmap
	memset(blockbuffer, 0, BLOCK_SIZE);
	bitmap_t i_bitmap = (bitmap_t)blockbuffer;
	set_bitmap(i_bitmap, 0); 
	bio_write(new_sb.i_bitmap_blk, blockbuffer);
	
	// initialize data block bitmap and set bitmap
	memset(blockbuffer, 0, BLOCK_SIZE);
	bitmap_t d_bitmap = (bitmap_t)blockbuffer;
	set_bitmap(d_bitmap, 0);
	bio_write(new_sb.d_bitmap_blk, blockbuffer);

	// initialize root directory data block
	memset(blockbuffer, 0, BLOCK_SIZE);
	bio_write(new_sb.d_start_blk, blockbuffer);

	sb = new_sb;
	
	// update inode for root directory
	struct inode root;
	memset(&root, 0, sizeof(struct inode));

	root.ino = 0;
	root.valid = 1;
	root.type = __S_IFDIR;
	root.size = 0;
	root.link = 2;
	for (int i = 0; i < 16; i++) root.direct_ptr[i] = 0;
	for (int i = 0; i < 8; i++) root.indirect_ptr[i] = 0;
	root.direct_ptr[0] = new_sb.d_start_blk; 

	time_t curr_time = time(NULL);
	root.vstat.st_atime = curr_time;
	root.vstat.st_mtime = curr_time;

	root.vstat.st_mode = __S_IFDIR | 0755;
	root.vstat.st_uid = getuid();
	root.vstat.st_gid = getgid();
	root.vstat.st_nlink = 2;

	writei(0, &root);
	debugf("Wrote root inode to disk");

	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {
	debugf("Initializing RUFS...");
	
	struct stat st;

	// Step 1a: If disk file is not found, call mkfs
	if (stat(diskfile_path, &st) < 0) {
        if (rufs_mkfs() < 0) {
            return -1;
        }
    } else {
        // Step 1b: If disk file is found, just initialize in-memory data structures
        if (dev_open(diskfile_path) < 0) {
            return -1;
        }
    }

	// and read superblock from disk
	char blockbuffer[BLOCK_SIZE];
	if (bio_read(0, blockbuffer) < 0) {
		fprintf(stderr, "Failed to read superblock\n");
		return -EIO;
	}

	// Copy superblock into global in-memory superblock
    memcpy(&sb, blockbuffer, sizeof(struct superblock));
	
	return 0;
}

int count_used_dblocks() {
    char buffer[BLOCK_SIZE];
    if (bio_read(sb.d_bitmap_blk, buffer) < 0) {
        return -1;
    }

    bitmap_t b = (bitmap_t)buffer;
    int used = 0;

    for (int i = 0; i < sb.max_dnum; i++) {
        if (get_bitmap(b, i)) {
            used++;
        }
    }

    return used;
}

int count_used_inodes() {
    char buffer[BLOCK_SIZE];
    if (bio_read(sb.i_bitmap_blk, buffer) < 0) {
        return -1;
    }

    bitmap_t b = (bitmap_t)buffer;
    int used = 0;

    for (int i = 0; i < sb.max_inum; i++) {
        if (get_bitmap(b, i)) {
            used++;
        }
    }

    return used;
}



static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures

	// Step 2: Close diskfile
	printf("Used data blocks: %d\n", count_used_dblocks());
	printf("Used inodes: %d\n", count_used_inodes());
	
	dev_close();
	
}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path
	struct inode ino;
	if (get_node_by_path(path, 0, &ino) < 0) {
		return -ENOENT;
	}

	// Step 2: fill attribute of file into stbuf from inode
	*stbuf = ino.vstat;

	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: If not find, return -1
	struct inode dir_inode;
	if (get_node_by_path(path, 0, &dir_inode) < 0) {
		return -ENOENT;
	}

    if (!S_ISDIR(dir_inode.vstat.st_mode)) {
        return -1;
    }	

	return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode dir_inode;
	if (get_node_by_path(path, 0, &dir_inode) < 0) {
		return -ENOENT;
	}

	if (!S_ISDIR(dir_inode.vstat.st_mode)) {
		return -1;
	}

	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);

	size_t ents_per_blk = BLOCK_SIZE / sizeof(struct dirent);
    char blockbuffer[BLOCK_SIZE];

    
    for (int i = 0; i < 16; i++) {
        int blk = dir_inode.direct_ptr[i];
        if (blk == 0)
            continue;

        if (bio_read(blk, blockbuffer) < 0)
            return -EIO;

        struct dirent *tab = (struct dirent *)blockbuffer;
		
	// Step 2: Read directory entries from its data blocks, and copy them to filler
        for (size_t j = 0; j < ents_per_blk; j++) {
            if (!tab[j].valid)
                continue;

            // add null terminator
            char name[209];
            memcpy(name, tab[j].name, tab[j].len);
            name[tab[j].len] = '\0';

            filler(buffer, name, NULL, 0);
        }
    }


	return 0;
}

static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char path_copy1[PATH_MAX];
    char path_copy2[PATH_MAX];

    strcpy(path_copy1, path);
    strcpy(path_copy2, path);

	const char *parent_path = dirname(path_copy1);
	const char *dir_name = basename(path_copy2);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode parent_inode;
	if (get_node_by_path(parent_path, 0, &parent_inode) < 0) {
		return -ENOENT;
	}

	if (!S_ISDIR(parent_inode.vstat.st_mode)) {
		return -1;
	}

	// Step 3: Call get_avail_ino() to get an available inode number
	int new_ino = get_avail_ino();
	if (new_ino < 0) {
		return -1;
	}

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	if (dir_add(parent_inode, new_ino, dir_name, strlen(dir_name)) < 0) {
		return -1;
	}

	// Step 5: Update inode for target directory
	struct inode new_inode;
	memset(&new_inode, 0, sizeof(struct inode));

	new_inode.ino = new_ino;
	new_inode.valid = 1;
	new_inode.type = __S_IFDIR;
	new_inode.size = 0;
	new_inode.link = 2; 

    int blk = get_avail_blkno();
    if (blk < 0) {
        return -ENOSPC;
    }

    new_inode.direct_ptr[0] = blk;

	time_t now = time(NULL);
	new_inode.vstat.st_mode = __S_IFDIR | mode;
	new_inode.vstat.st_uid = getuid();
	new_inode.vstat.st_gid = getgid();
	new_inode.vstat.st_nlink = 2;
	new_inode.vstat.st_atime = now;
	new_inode.vstat.st_mtime = now;

	// Step 6: Call writei() to write inode to disk
	writei(new_ino, &new_inode);
	
	return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	debugf("Entered rufs_create...");

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char path_copy1[PATH_MAX];
    char path_copy2[PATH_MAX];

    strcpy(path_copy1, path);
    strcpy(path_copy2, path);

	const char *parent_path = dirname(path_copy1);
	const char *file_name = basename(path_copy2);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode parent_inode;
	if (get_node_by_path(parent_path, 0, &parent_inode) < 0) {
		return -ENOENT;
	}

	if (!S_ISDIR(parent_inode.vstat.st_mode)) {
		return -ENOTDIR;
	}

	// Step 3: Call get_avail_ino() to get an available inode number
	int new_ino = get_avail_ino();
	if (new_ino < 0) {
		return -ENOSPC;
	}

	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	if (dir_add(parent_inode, new_ino, file_name, strlen(file_name)) < 0) {
        return -EIO;
    }

	// Step 5: Update inode for target file
	struct inode new_inode;
	memset(&new_inode, 0, sizeof(struct inode));

	new_inode.ino = new_ino;
	new_inode.valid = 1;
	new_inode.type = __S_IFREG;
	new_inode.size = 0;
	new_inode.link = 1;

	time_t now = time(NULL);
	new_inode.vstat.st_mode = __S_IFREG | mode;
	new_inode.vstat.st_uid = getuid();
	new_inode.vstat.st_gid = getgid();
	new_inode.vstat.st_nlink = 1;
	new_inode.vstat.st_atime = now;
	new_inode.vstat.st_mtime = now;

	// Step 6: Call writei() to write inode to disk
	writei(new_ino, &new_inode);

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {
	printf(" TESTING OUTPUT\n");
	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: If not find, return -1
	struct inode file_inode;

	if (get_node_by_path(path, 0, &file_inode) < 0) {
		return -ENOENT;
	}

    if (!S_ISREG(file_inode.vstat.st_mode)) {
        return -EISDIR;
    }	

	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode file_inode;
    if (get_node_by_path(path, 0, &file_inode) < 0) {
        return -ENOENT;
    }

    if (!S_ISREG(file_inode.vstat.st_mode)) {
        return -EISDIR;
    }

	// Step 2: Based on size and offset, read its data blocks from disk
	if (offset >= file_inode.size) {
        return 0;
    }
    // Clamp size to file size
    if (offset + size > file_inode.size) {
        size = file_inode.size - offset;
    }

	// Step 3: copy the correct amount of data from offset to buffer
	size_t bytes_read = 0;
    int start_blk = offset / BLOCK_SIZE;
    int blk_offset = offset % BLOCK_SIZE;

    for (int i = start_blk; i < 16 && bytes_read < size; i++) {

        int blkno = file_inode.direct_ptr[i];
        if (blkno == 0) break;

        char blkbuf[BLOCK_SIZE];
        bio_read(blkno, blkbuf);

        size_t to_copy = BLOCK_SIZE - blk_offset;
        if (to_copy > size - bytes_read)
            to_copy = size - bytes_read;

        memcpy(buffer + bytes_read,
               blkbuf + blk_offset,
               to_copy);

        bytes_read += to_copy;
        blk_offset = 0; // only applies to first block
    }


	// Note: this function should return the amount of bytes you copied to buffer
	return bytes_read;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	debugf("rufs_write: path=%s size=%zu offset=%lld", path, size, (long long)offset);

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode file_inode;
    if (get_node_by_path(path, 0, &file_inode) < 0) {
        return -ENOENT;
    }

    if (!S_ISREG(file_inode.vstat.st_mode)) {
        return -EISDIR;
    }
	// Step 2: Based on size and offset, read its data blocks from disk
	size_t bytes_written = 0;
    int start_blk = offset / BLOCK_SIZE;
    int blk_offset = offset % BLOCK_SIZE;
	debugf("start_blk=%d blk_offset=%d", start_blk, blk_offset);

	for (int i = start_blk; i < 16 && bytes_written < size; i++) {
		debugf("processing block index i=%d direct_ptr[i]=%d", i, file_inode.direct_ptr[i]);

        // Allocate block if missing
        if (file_inode.direct_ptr[i] == 0) {
            int blk = get_avail_blkno();
			debugf("allocated block = %d (sb.d_start_blk=%u)", blk, sb.d_start_blk);
            if (blk < 0) {
				return -ENOSPC;
                // break;
            }
            file_inode.direct_ptr[i] = blk;
        }

        char blockbuffer[BLOCK_SIZE];
        bio_read(file_inode.direct_ptr[i], blockbuffer);
	
	// Step 3: Write the correct amount of data from offset to disk
        size_t to_copy = BLOCK_SIZE - blk_offset;
        if (to_copy > size - bytes_written)
            to_copy = size - bytes_written;

        memcpy(blockbuffer + blk_offset, buffer + bytes_written, to_copy);
		debugf("writing %zu bytes into block %d (file offset %lld)",
       	to_copy, file_inode.direct_ptr[i], (long long)(offset + bytes_written));
		

        bio_write(file_inode.direct_ptr[i], blockbuffer);

        bytes_written += to_copy;
		debugf("rufs_write: bytes_written=%zu, block=%d", bytes_written, i);
        blk_offset = 0;
    }


	// Step 4: Update the inode info and write it to disk
	if (offset + bytes_written > file_inode.size) {
        file_inode.size = offset + bytes_written;
		file_inode.vstat.st_size = file_inode.size;
    }
	debugf("updated inode size = %u, vstat.size = %u", file_inode.size, file_inode.vstat.st_size);


    file_inode.vstat.st_mtime = time(NULL);

    writei(file_inode.ino, &file_inode);

	// Note: this function should return the amount of bytes you write to disk
	return bytes_written;
}


/* 
 * Functions you DO NOT need to implement for this project
 * (stubs provided for completeness)
 */

static int rufs_rmdir(const char *path) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_unlink(const char *path) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.mkdir		= rufs_mkdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,

	//Operations that you don't have to implement.
	.rmdir		= rufs_rmdir,
	.releasedir	= rufs_releasedir,
	.unlink		= rufs_unlink,
	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	printf(" TESTING OUTPUT\n");
	fprintf(stderr, "RUFS starting...\n");
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}


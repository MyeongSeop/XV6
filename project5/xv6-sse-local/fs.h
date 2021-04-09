// On-disk file system fortat.
// Both the kernel and user programs use this header file.


#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  //uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  //uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block in Block Group
  uint nswap;
  uint swapstart;
  uint bgstart;
  uint bgnum;
  //uint BGSIZE;
};

#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint)) // 128
#define NININDIRECT (BSIZE/ sizeof(uint))*(BSIZE/sizeof(uint)) // 128*128
#define MAXFILE (NDIRECT + NINDIRECT + NININDIRECT)



#define REALSIZE     (FSSIZE-LOGSIZE-2)
#define max_get(a, b) ((a) > (b) ? (a) : (b) )
#define BGSIZE       max_get((REALSIZE/32), 4096)
#define BGNUM        (REALSIZE/BGSIZE)
#define SWAPSIZE     (REALSIZE)%BGSIZE
#define BGINODE      (BGSIZE/32)
#define BGBIT        ((BGSIZE%4096) != (0) ? (BGSIZE/4096 + 1) : (BGSIZE/4096))
#define BGDATA       (BGSIZE - BGINODE - BGBIT)



// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+2];   // Data block addresses
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, bg_num, sb) ( (i) + sb.bgstart + bg_num*BGSIZE) 
//#define BG_get(i) 				((i)/BGSIZE)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, bg_num, sb) (b + sb.bmapstart + sb.bgstart + bg_num*BGSIZE) 

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};


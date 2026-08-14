struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uint64 lastuse; // logical time of the most recent final release
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

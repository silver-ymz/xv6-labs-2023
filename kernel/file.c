//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "fcntl.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

struct {
  struct spinlock lock;
  struct mmap mmap[NFILE];
} mmaptable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
  initlock(&mmaptable.lock, "mmaptable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

struct mmap* mmapalloc(struct file *f)
{
  struct mmap *m;
  acquire(&mmaptable.lock);
  for(m = mmaptable.mmap; m < mmaptable.mmap + NFILE; m++){
    if (m->file == 0) {
      m->file = f;
      release(&mmaptable.lock);
      filedup(f);
      return m;
    }
  }
  release(&mmaptable.lock);
  return 0;
}

struct mmap *mmapdup(struct mmap *m2, pagetable_t pagetable) {
  struct mmap *m;
  acquire(&mmaptable.lock);
  for(m = mmaptable.mmap; m < mmaptable.mmap + NFILE; m++){
    if (m->file == 0) {
      memmove(m, m2, sizeof(struct mmap));
      if ((m->addr = uvmmmap(pagetable, m->addr, m->len, m->prot << 1)) != m2->addr) {
        m->file = 0;
        return 0;
      }
      release(&mmaptable.lock);
      filedup(m->file);
      return m;
    }
  }
  release(&mmaptable.lock);
  return 0;
}

void mmapclose(struct mmap *m) {
  if(m->file == 0)
    panic("mmapclose");
  fileclose(m->file);
  acquire(&mmaptable.lock);
  m->file = 0;
  release(&mmaptable.lock);
}

int munmap(uint64 addr, uint64 len) {
  if (addr % PGSIZE != 0)
    return -1;

  if (len % PGSIZE != 0)
    return -1;

  for (int i = 0; i < NOFILE; i++) {
    struct mmap *m;
    if ((m = myproc()->mmap[i])) {
      int mlen = m->len;
      if (m->addr == addr) {
        m->addr = addr + len;
        m->len -= len;
      } else if (m->addr + m->len == addr + len) {
        m->len -= len;
      } else {
        continue;
      }

      if (m->flag & MAP_SHARED) {
        begin_op();
        ilock(m->file->ip);
        for (int va = addr; va < addr + len; va += PGSIZE) {
          int off = va - addr;
          int len = (mlen - off) < PGSIZE ? (mlen - off) : PGSIZE;
          pte_t *pte;
          if (!(pte = walk(myproc()->pagetable, va, 0)))
            panic("munmap: walk");
          if (*pte & PTE_V) {
            if (*pte & PTE_D) {
              if (writei(m->file->ip, 1, va, off, len) != len)
                panic("munmap: writei");
            }
            kfree((void *)PTE2PA(*pte));
            *pte = 0;
          }
        }
        iunlock(m->file->ip);
        end_op();
      } else {
        for (int va = addr; va < addr + len; va += PGSIZE) {
          pte_t *pte;
          if (!(pte = walk(myproc()->pagetable, va, 0)))
            panic("munmap: walk");
          if (*pte & PTE_V) {
            kfree((void *)PTE2PA(*pte));
            *pte = 0;
          }
        }
      }

      if (m->len == 0) {
        myproc()->mmap[i] = 0;
        mmapclose(m);
      }

      return 0;
    }
  }
  
  return -1;
}

int mmap_fault_handler(struct proc *p, uint64 va) {
  pte_t *pte;
  char *mem;
  uint flags;

  va = PGROUNDDOWN(va);
  if (va >= MAXVA)
    return -1;
  if ((pte = walk(p->pagetable, va, 0)) == 0)
    return -1;
  if ((*pte & PTE_V) || !(*pte & PTE_M))
    return -1;
  if (!(mem = kalloc()))
    return -1;

  for (int i = 0; i < NOFILE; i++) {
    struct mmap *m;
    if ((m = p->mmap[i]) && m->addr <= va && va < m->addr + m->len) {
      int off = va - m->addr;
      int len = (m->len - off) < PGSIZE ? (m->len - off) : PGSIZE;
      ilock(m->file->ip);
      int read_len = readi(m->file->ip, 0, (uint64)mem, off, len);
      iunlock(m->file->ip);
      if (read_len == -1 || read_len > len)
        panic("mmap_fault_handler: readi failed");
      while (read_len < PGSIZE) {
        mem[read_len] = 0;
        read_len++;
      }
      flags = PTE_FLAGS(*pte);
      flags ^= PTE_V;
      *pte = PA2PTE(mem) | flags;
      return 0;
    }
  }

  kfree(mem);
  return -1;
}
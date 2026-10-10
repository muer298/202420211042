#include "lab2.h"
pagetable_t kernel_pagetable;
static uint64 next_page;
static void *free_pages;
void *kalloc(void) {
  void *p;
  if(free_pages) { p = free_pages; free_pages = *(void **)p; }
  else {
    if(!next_page) next_page = PGROUNDUP((uint64)kernel_end);
    if(next_page + PGSIZE > PHYSTOP) return 0;
    p = (void *)next_page;
    next_page += PGSIZE;
  }
  memset(p, 0, PGSIZE);
  return p;
}
void kfree(void *p) {
  if(p) { *(void **)p = free_pages; free_pages = p; }
}
int map_page(pagetable_t root, uint64 va, uint64 pa, uint64 flags) {
  pagetable_t pt = root;
  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pt[PX(level, va)];
    if(*pte & PTE_V) {
      if(*pte & (PTE_R | PTE_W | PTE_X)) return -1;
      pt = (pagetable_t)PTE2PA(*pte);
    } else {
      pagetable_t next = kalloc();
      if(!next) return -1;
      *pte = PA2PTE(next) | PTE_V;
      pt = next;
    }
  }
  pt[PX(0, va)] = PA2PTE(pa) | flags | PTE_V | (1L << 6) | (1L << 7);
  return 0;
}
void free_pagetable(pagetable_t pt) {
  if(!pt) return;
  for(int i=0; i<512; i++)
    if((pt[i] & PTE_V) && !(pt[i] & (PTE_R | PTE_W | PTE_X)))
      free_pagetable((pagetable_t)PTE2PA(pt[i]));
  kfree(pt);
}
void vm_init(void) {
  kernel_pagetable = kalloc();
  if(!kernel_pagetable) panic("kernel page table");
  kernel_pagetable[0] = PTE_V | PTE_R | PTE_W | 0xc0;
  kernel_pagetable[2] = PA2PTE(0x80000000UL) | PTE_V | PTE_R | PTE_W | PTE_X | 0xc0;
  if(map_page(kernel_pagetable, TRAMPOLINE, (uint64)trampoline, PTE_R | PTE_X))
    panic("trampoline mapping");
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}
int user_range(uint64 va, uint64 n) { return va < USER_SIZE && n <= USER_SIZE - va; }
static int transfer(struct proc *p, void *kernel, uint64 user, uint64 n, int out) {
  if(!user_range(user,n)) return -1;
  uchar *k = kernel;
  while(n) {
    uint64 off = user % PGSIZE, count = PGSIZE - off;
    if(count > n) count = n;
    uchar *physical = (uchar *)p->pages[user / PGSIZE] + off;
    if(out) memcpy(physical,k,count); else memcpy(k,physical,count);
    k += count; user += count; n -= count;
  }
  return 0;
}
int copyin(struct proc *p, void *dst, uint64 src, uint64 n) { return transfer(p,dst,src,n,0); }
int copyout(struct proc *p, uint64 dst, const void *src, uint64 n) { return transfer(p,(void *)src,dst,n,1); }
int copyinstr(struct proc *p, char *dst, uint64 src, uint64 max) {
  for(uint64 i=0;i<max;i++) {
    if(copyin(p,dst+i,src+i,1)) return -1;
    if(!dst[i]) return 0;
  }
  return -1;
}

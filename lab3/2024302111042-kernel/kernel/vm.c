#include "lab3.h"

#define RAM_PAGES ((PHYSTOP - KERNBASE) / PGSIZE)
#define BITBYTE(i) ((i) >> 3)
#define BITMASK(i) (1U << ((i) & 7))

pagetable_t kernel_pagetable;
static uchar freebits[(RAM_PAGES + 7) / 8];
static uint first_page, limit_page;
static uint64 free_count;
static int alloc_hint;

static int bit_get(uint i) { return (freebits[BITBYTE(i)] & BITMASK(i)) != 0; }
static void bit_set(uint i) { freebits[BITBYTE(i)] |= BITMASK(i); }
static void bit_clear(uint i) { freebits[BITBYTE(i)] &= (uchar)~BITMASK(i); }

void
kinit(void)
{
  uint64 first = PGROUNDUP((uint64)kernel_end);
  first_page = (uint)((first - KERNBASE) / PGSIZE);
  limit_page = (uint)RAM_PAGES;
  if (LAB3_MEMCAP_DELTA >= limit_page - first_page)
    panic("LAB3 memory cap");
  limit_page -= LAB3_MEMCAP_DELTA;
  memset(freebits, 0, sizeof(freebits));
  for (uint i = first_page; i < limit_page; i++) bit_set(i);
  free_count = limit_page - first_page;
#if LAB3_ALLOC_ORDER == 0
  alloc_hint = (int)first_page;
#else
  alloc_hint = (int)limit_page - 1;
#endif
}

uint64 kfree_pages(void) { return free_count; }

void *
kalloc(void)
{
  int found = -1;
#if LAB3_ALLOC_ORDER == 0
  for (int i = alloc_hint; i < (int)limit_page; i++)
    if (bit_get((uint)i)) { found = i; break; }
#elif LAB3_ALLOC_ORDER == 1
  for (int i = alloc_hint; i >= (int)first_page; i--)
    if (bit_get((uint)i)) { found = i; break; }
#else
  uint mid = first_page + (limit_page - first_page) / 2;
  for (uint d = 0; d < limit_page - first_page; d++) {
    uint i = (d & 1) ? mid - (d + 1) / 2 : mid + d / 2;
    if (i >= first_page && i < limit_page && bit_get(i)) { found = (int)i; break; }
  }
#endif
  if (found < 0) return 0;
  bit_clear((uint)found);
  free_count--;
#if LAB3_ALLOC_ORDER == 0
  alloc_hint = found + 1;
#elif LAB3_ALLOC_ORDER == 1
  alloc_hint = found - 1;
#endif
  void *p = (void *)(KERNBASE + (uint64)(uint)found * PGSIZE);
  memset(p, 0, PGSIZE);
  return p;
}

void
kfree(void *p)
{
  uint64 pa = (uint64)p;
  if (!p || pa % PGSIZE || pa < KERNBASE + (uint64)first_page * PGSIZE ||
      pa >= KERNBASE + (uint64)limit_page * PGSIZE)
    panic("kfree address");
  uint i = (uint)((pa - KERNBASE) / PGSIZE);
  if (bit_get(i)) panic("double kfree");
  memset(p, 0x5a, PGSIZE);
  bit_set(i);
  free_count++;
#if LAB3_ALLOC_ORDER == 0
  if ((int)i < alloc_hint) alloc_hint = (int)i;
#elif LAB3_ALLOC_ORDER == 1
  if ((int)i > alloc_hint) alloc_hint = (int)i;
#endif
}

/* Return whether this table still contains mappings; free empty descendants. */
static int
prune(pagetable_t pt, int level)
{
  int used = 0;
  for (int i = 0; i < 512; i++) {
    pte_t pte = pt[i];
    if (!(pte & PTE_V)) continue;
    if (pte & (PTE_R | PTE_W | PTE_X)) {
      used = 1;
      continue;
    }
    pagetable_t child = (pagetable_t)PTE2PA(pte);
    if (level > 0 && !prune(child, level - 1)) {
      pt[i] = 0;
      kfree(child);
    } else {
      used = 1;
    }
  }
  return used;
}

pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if (va >= MAXVA) return 0;
  pagetable_t created[2];
  pte_t *parents[2];
  int ncreated = 0;
  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      if (*pte & (PTE_R | PTE_W | PTE_X)) return 0;
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc) return 0;
      pagetable_t next = kalloc();
      if (!next) {
        while (ncreated) {
          ncreated--;
          *parents[ncreated] = 0;
          kfree(created[ncreated]);
        }
        return 0;
      }
      parents[ncreated] = pte;
      created[ncreated++] = next;
      *pte = PA2PTE(next) | PTE_V;
      pagetable = next;
    }
  }
  return &pagetable[PX(0, va)];
}

int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, uint64 perm)
{
  if (!size || va % PGSIZE || pa % PGSIZE || size % PGSIZE ||
      va >= MAXVA || size > MAXVA - va)
    return -1;
  uint64 start = va;
  for (uint64 done = 0; done < size; done += PGSIZE) {
    pte_t *pte = walk(pagetable, va + done, 1);
    if (!pte || (*pte & PTE_V)) {
      for (uint64 undo = 0; undo < done; undo += PGSIZE) {
        pte_t *old = walk(pagetable, start + undo, 0);
        if (old) *old = 0;
      }
      prune(pagetable, 2);
      return -1;
    }
    uint64 ad = PTE_A | ((perm & PTE_W) ? PTE_D : 0);
    *pte = PA2PTE(pa + done) | perm | ad | PTE_V;
  }
  return 0;
}

void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  va = PGROUNDDOWN(va);
  for (uint64 i = 0; i < npages; i++, va += PGSIZE) {
    pte_t *pte = walk(pagetable, va, 0);
    if (!pte || !(*pte & PTE_V) || !(*pte & (PTE_R | PTE_W | PTE_X))) continue;
    uint64 pa = PTE2PA(*pte);
    *pte = 0; /* Invalidate before the physical page becomes reusable. */
    if (do_free) kfree((void *)pa);
  }
  prune(pagetable, 2);
}

pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pt = kalloc();
  if (!pt) return 0;
  if (mappages(pt, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) < 0 ||
      mappages(pt, TRAPFRAME, PGSIZE, (uint64)p->tf, PTE_R | PTE_W) < 0) {
    /* Non-user leaves are only detached; the owned trapframe is not freed. */
    free_user_pagetable(pt);
    return 0;
  }
  return pt;
}

static void
free_user_level(pagetable_t pt, int level)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = pt[i];
    if (!(pte & PTE_V)) continue;
    if (pte & (PTE_R | PTE_W | PTE_X)) {
      pt[i] = 0;
      if (pte & PTE_U) kfree((void *)PTE2PA(pte));
    } else {
      pagetable_t child = (pagetable_t)PTE2PA(pte);
      if (level > 0) free_user_level(child, level - 1);
      pt[i] = 0;
      kfree(child);
    }
  }
}

void
free_user_pagetable(pagetable_t pt)
{
  if (!pt) return;
  free_user_level(pt, 2);
  kfree(pt);
}

static int
copy_user_level(pagetable_t src, pagetable_t dst, int level, uint64 base)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = src[i];
    if (!(pte & PTE_V)) continue;
    uint64 va = base | ((uint64)i << PXSHIFT(level));
    if (pte & (PTE_R | PTE_W | PTE_X)) {
      if (!(pte & PTE_U)) continue;
      void *page = kalloc();
      if (!page) return -1;
      memcpy(page, (void *)PTE2PA(pte), PGSIZE);
      uint64 flags = PTE_FLAGS(pte) & ~(PTE_V | PTE_A | PTE_D);
      if (mappages(dst, va, PGSIZE, (uint64)page, flags) < 0) {
        kfree(page);
        return -1;
      }
    } else if (level > 0 &&
               copy_user_level((pagetable_t)PTE2PA(pte), dst, level - 1, va) < 0) {
      return -1;
    }
  }
  return 0;
}

int uvmcopy(pagetable_t src, pagetable_t dst) { return copy_user_level(src, dst, 2, 0); }

static int
map_fresh_range(pagetable_t pt, uint64 start, uint64 end, uint64 perm)
{
  uint64 mapped = 0;
  for (uint64 va = start; va < end; va += PGSIZE) {
    void *page = kalloc();
    if (!page || mappages(pt, va, PGSIZE, (uint64)page, perm) < 0) {
      if (page) kfree(page);
      uvmunmap(pt, start, mapped / PGSIZE, 1);
      return -1;
    }
    mapped += PGSIZE;
  }
  return 0;
}

int
uvmalloc(struct proc *p, uint64 oldsz, uint64 newsz)
{
  if (newsz < oldsz || newsz >= TRAPFRAME) return -1;
  uint64 first = PGROUNDUP(oldsz), end = PGROUNDUP(newsz);
  if (end < first) return -1;
  return map_fresh_range(p->pagetable, first, end, PTE_R | PTE_W | PTE_U);
}

int
uvmdealloc(struct proc *p, uint64 oldsz, uint64 newsz)
{
  if (newsz > oldsz || newsz < p->heap_base) return -1;
  uint64 first = PGROUNDUP(newsz), end = PGROUNDUP(oldsz);
  if (end > first) uvmunmap(p->pagetable, first, (end - first) / PGSIZE, 1);
  return 0;
}

int user_range(uint64 va, uint64 n) { return va < MAXVA && n <= MAXVA - va; }

/* Check every page before a syscall starts doing externally visible work. */
int
user_access(struct proc *p, uint64 va, uint64 n, int write)
{
  if (!user_range(va, n)) return 0;
  if (!n) return 1;
  uint64 last = PGROUNDDOWN(va + n - 1);
  for (uint64 page = PGROUNDDOWN(va); ; page += PGSIZE) {
    pte_t *pte = walk(p->pagetable, page, 0);
    uint64 need = PTE_V | PTE_U | (write ? PTE_W : PTE_R);
    if (!pte || (*pte & need) != need || !(*pte & (PTE_R | PTE_W | PTE_X)))
      return 0;
    if (page == last) break;
  }
  return 1;
}

static int
transfer(struct proc *p, void *kernel, uint64 user, uint64 n, int out, int require_user)
{
  if (!user_range(user, n)) return -1;
  uchar *k = kernel;
  while (n) {
    uint64 pageva = PGROUNDDOWN(user);
    pte_t *pte = walk(p->pagetable, pageva, 0);
    if (!pte || !(*pte & PTE_V) || !(*pte & (PTE_R | PTE_W | PTE_X)) ||
        (require_user && !(*pte & PTE_U)) ||
        (out && require_user && !(*pte & PTE_W)) ||
        (!out && require_user && !(*pte & PTE_R)))
      return -1;
    uint64 count = PGSIZE - (user - pageva);
    if (count > n) count = n;
    uchar *physical = (uchar *)PTE2PA(*pte) + (user - pageva);
    if (out) memcpy(physical, k, count); else memcpy(k, physical, count);
    k += count; user += count; n -= count;
  }
  return 0;
}

int copyin(struct proc *p, void *dst, uint64 src, uint64 n) { return transfer(p, dst, src, n, 0, 1); }
int copyout(struct proc *p, uint64 dst, const void *src, uint64 n) { return transfer(p, (void *)src, dst, n, 1, 1); }
int uvm_load_bytes(struct proc *p, uint64 dst, const void *src, uint64 n) { return transfer(p, (void *)src, dst, n, 1, 0); }

int
copyinstr(struct proc *p, char *dst, uint64 src, uint64 max)
{
  for (uint64 i = 0; i < max; i++) {
    if (copyin(p, dst + i, src + i, 1)) return -1;
    if (!dst[i]) return 0;
  }
  return -1;
}

static void
dump_level(pagetable_t pt, int level, uint64 base)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = pt[i];
    if (!(pte & PTE_V)) continue;
    uint64 va = base | ((uint64)i << PXSHIFT(level));
    uint64 pa = PTE2PA(pte);
    if (pte & (PTE_R | PTE_W | PTE_X)) {
      char perm[5] = {
        (pte & PTE_R) ? 'r' : '-', (pte & PTE_W) ? 'w' : '-',
        (pte & PTE_X) ? 'x' : '-', (pte & PTE_U) ? 'u' : '-', 0
      };
      printf("LEAF va=%lx pa=%lx perm=%s\n", va, pa, perm);
    } else {
      printf("L%d va=%lx pa=%lx perm=----\n", level, va, pa);
      if (level > 0) dump_level((pagetable_t)pa, level - 1, va);
    }
  }
}

void
dump_pagetable(pagetable_t pt)
{
  printf("DUMP-PAGETABLE begin\n");
  dump_level(pt, 2, 0);
  printf("DUMP-PAGETABLE end\n");
}

void
vm_selftest(void)
{
  uint64 before = kfree_pages();
  void *a = kalloc(), *b = kalloc(), *c = kalloc();
  if (!a || !b || !c || (uint64)a % PGSIZE ||
#if LAB3_ALLOC_ORDER == 1
      !((uint64)a > (uint64)b && (uint64)b > (uint64)c) ||
#else
      0 ||
#endif
      *(volatile uint64 *)a != 0)
    panic("allocator selftest");
  kfree(a); kfree(b); kfree(c);
  if (kfree_pages() != before) panic("allocator conservation");

  pagetable_t pt = kalloc();
  void *held = 0;
  while (kfree_pages() > 2) {
    void *page = kalloc();
    if (!page) panic("rollback setup");
    *(void **)page = held;
    held = page;
  }
  uint64 low = kfree_pages();
  if (mappages(pt, (1UL << 21) - PGSIZE, 2 * PGSIZE,
               KERNBASE, PTE_R | PTE_W) != -1 || kfree_pages() != low)
    panic("mappages rollback");
  kfree(pt);
  while (held) {
    void *next = *(void **)held;
    kfree(held);
    held = next;
  }
  if (kfree_pages() != before) panic("rollback conservation");
  printf("LAB3-SELFTEST PASS allocator order=%d rollback\n", LAB3_ALLOC_ORDER);
}

void
vm_init(void)
{
  kernel_pagetable = kalloc();
  if (!kernel_pagetable) panic("kernel page table");
  if (mappages(kernel_pagetable, UART0, PGSIZE, UART0, PTE_R | PTE_W) < 0 ||
      mappages(kernel_pagetable, PLIC, 0x400000, PLIC, PTE_R | PTE_W) < 0 ||
      mappages(kernel_pagetable, KERNBASE, PHYSTOP - KERNBASE,
               KERNBASE, PTE_R | PTE_W | PTE_X) < 0 ||
      mappages(kernel_pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
    panic("kernel mappings");
  sfence_vma();
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

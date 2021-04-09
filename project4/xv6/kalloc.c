// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "x86.h"
#include "proc.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

struct page pages[PHYSTOP/PGSIZE];
struct page *page_lru_head;
int num_free_pages=0;
int num_lru_pages=0;
struct spinlock p_lock;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
  initlock(&p_lock, "p_lock");
}

char* bitmap_addr;

int get_blkno(char *a)
{
        int i;
        char *addr = a;
        for(i=1;i<PGSIZE;i++){
                if(addr[i] == 0 ){
			addr[i] = 1;
			break;
		}
	}
	if(i==PGSIZE){panic("blkno exceed\n"); return -1;}
        return i;
}


void
bitmap(void)
{
	bitmap_addr = kalloc();
	memset(bitmap_addr, 0, PGSIZE);
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;
  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP){
  
  panic("kfree");}

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
  num_free_pages++;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

int reclaim(void)
{
	if(num_lru_pages == 0){
		panic("Out of Memory!\n");
		return 0;
	}
	
	int i;
	pde_t *pgdir;
	char* va;
	pte_t *pte=0;
	
	for(i=0;i<num_lru_pages;i++){
		pgdir = page_lru_head->pgdir;
   	    	va = page_lru_head->vaddr;
       		pte = walkpgdir(pgdir, va, 0);
                page_lru_head = page_lru_head->next;
		if((*pte & PTE_A)==0) break;
		else *pte = *pte & ~PTE_A;
		if(i == num_lru_pages - 1){
			pgdir = page_lru_head->pgdir;
			va = page_lru_head->vaddr;
			pte = walkpgdir(pgdir, va, 0);
			page_lru_head = page_lru_head->next;
			break;
		}
        }
        uint pa = PTE_ADDR(*pte);
        char* ph_addr = (char* )P2V(pa);
        
        int blkno = get_blkno(bitmap_addr);
	if(blkno == -1){panic("blkno exceed\n"); return 0;}
	swapwrite(ph_addr,blkno);
	unlink_page(ph_addr);
	kfree(ph_addr);
	//clear PTE_P
	*pte = *pte & 0xFFF;
	blkno = (blkno + SWAPBASE)*4096;
	*pte = *pte | blkno | PTE_S;
        *pte = *pte & ~PTE_P;

	return 1;
}


char*
kalloc(void)
{
  struct run *r;

try_again:
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(!r){
	release(&kmem.lock);
	int get_reclaim = reclaim();
  	if(get_reclaim == 1) goto try_again;
	else return 0;
  }
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  num_free_pages--;
  return (char*)r;
}

void link_page(char* addr, char* v_addr, pde_t *pgdir)
{
	acquire(&p_lock);
	uint p_addr = V2P(addr);
	int page_num = p_addr/PGSIZE;

	pages[page_num].vaddr = v_addr;
	pages[page_num].pgdir = pgdir;
	
	if(num_lru_pages == 0){
		page_lru_head = &pages[page_num];
		pages[page_num].next = &pages[page_num];
		pages[page_num].prev = &pages[page_num];
	}
	struct page *tmp = page_lru_head->prev;
	if(num_lru_pages != 0){
		pages[page_num].next = page_lru_head;
		pages[page_num].prev = tmp;
		tmp->next = &pages[page_num];
		page_lru_head->prev = &pages[page_num];
		
	}
	num_lru_pages++;
	release(&p_lock);
}

void unlink_page(char* addr)
{
	uint p_addr = V2P(addr);
	if(num_lru_pages <= 0) return;
	int page_num = p_addr/PGSIZE;
	acquire(&p_lock);
	if(pages[page_num].next == 0 || pages[page_num].prev == 0) {
		release(&p_lock);
		return;
	}
	if((pages[page_num].pgdir) == (page_lru_head->pgdir) && pages[page_num].vaddr == page_lru_head->vaddr) page_lru_head = page_lru_head->next;
	
	struct page *left;
	struct page *right;
	left = pages[page_num].prev;
	right = pages[page_num].next;

	//delete
	pages[page_num].vaddr = 0;
	pages[page_num].pgdir = 0;
	left->next = right;
	right->prev = left;
	pages[page_num].next = 0;
	pages[page_num].prev = 0;
	num_lru_pages--;

	release(&p_lock);
}

void get_pages(void)
{	
	//char *ph_addr = kalloc();
	uint va = rcr2();
	va = (va/4096)*4096;
	pde_t *pgdir = myproc()->pgdir;
	pte_t *pte = walkpgdir(pgdir, (char *)va, 0);
	char *ph_addr = kalloc();
	uint blkno = *pte & ~0xFFF;
	blkno = (blkno/4096) - SWAPBASE;
	swapread(ph_addr, blkno);
	bitmap_addr[blkno] = 0;
	*pte = *pte & ~PTE_S;
	*pte = *pte & 0xFFF;
	*pte = *pte | PTE_P | V2P(ph_addr) | PTE_U | PTE_W;

	link_page(ph_addr, (char*)va, pgdir);

}

#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
int fds[64]={0,};
int mmap_proc[NPROC]={0,}; //0 unused more than 1: used
int pf_count =0;
int mmap_freed[64]={0,}; // 0 usable 1 already_exists
int mmap_area_proc[64]={0,};
struct mmap_area m[64];
uint mmap(uint addr, int length, int prot, int flags, int fd, int offset)
{
	
	struct proc *p=myproc();
	int i,j;
	int m_count=0;
	//length and offset check;
	if(length%4096 != 0 || offset%4096 != 0) {
		return 0;
	}
	//map_private
	if((flags & 0x02) != 0x02) return 0; 
	//mmap_area
	for(i=0;i<64;i++){
		if(mmap_freed[i] == 0){
			m_count=i;
			mmap_freed[m_count]=1;
			break;
		}
	}
	if(m_count == 64){
		return 0;
	}
	
	/*
	int f_wflag = file_wflag(p->ofile[fd]);
        if(f_wflag == 0 && (prot & 0x2 )== 0x02){
              	return 0;
        }*/

	int size = length/4096;
	//map_fixed
	if(addr != 0){
		if((flags & 0x10) != 0x10) return 0;
	}
	if((flags & 0x10) == 0x10){
		if(addr==0) return 0;
		int pid = p->pid;
		if(mmap_proc[pid] != 0){
			for(i=0;i<64;i++){
				int comp_pid = mmap_area_proc[i];
				
				if(comp_pid !=0 && comp_pid == pid){
                              		if(m[i].addr <= (addr+MMAPBASE) && (m[i].addr + m[i].length) > (addr+MMAPBASE)) {
						return 0;
					}
				}
			}
                        
                }
		m[m_count].addr = addr + MMAPBASE;
		for(i=0;i<size;i++){
			p->vm_table[(addr/4096) + i]=1;
		}
        }
	//map_anonymous
	int anony=0;
	if((flags & 0x04) != 0x04 && fd==-1) {
		return 0; 
	}
	if((flags & 0x04) == 0x04){
		anony=1;
	}
	if(anony==0 && fd==-1) return 0;
	//addr=0
	if(addr==0){
		int check=1;
		int base=0;
		for(i=0;i<4096-size;i++){
			check=1;
			for(j=0;j<size;j++){
				if(p->vm_table[i+j]==1) check=0;
			}
			if(check==1) {
				base=i;
				break;
			}
			base = i;
		}
		if(base == (4096-size) ) return 0;
		m[m_count].addr = MMAPBASE + (base*4096);
		for(j=base;j<base+size;j++) p->vm_table[j] = 1;
	}
	//check prot
	int perm;
	perm = PTE_W|PTE_U;
	int offset_size = offset/4096;
	uint start_addr = m[m_count].addr;
	char *ph_addr;

	//not populate
	if((flags & 0x08) != 0x08){ 
		m[m_count].length = length;
		m[m_count].offset = offset;
		m[m_count].prot = prot;
		m[m_count].flags = flags;
		m[m_count].p = p;
		if(anony==0) m[m_count].f = p->ofile[fd];  
		pf[pf_count] = m_count;
		fd_get[pf_count] = fd;
		pf_count ++;
	}
	
	//populate
	uint cal=start_addr;
	int map_flag=0;
	if((flags & 0x08) == 0x08){
		m[m_count].length = length;
		m[m_count].offset = offset;
		m[m_count].prot = prot;
		m[m_count].flags = flags;
		m[m_count].p = p;
		if(anony==0){
			for(i=0;i<offset_size;i++){
				ph_addr = kalloc();
				fileread(p->ofile[fd], ph_addr, 4096);
				kfree(ph_addr);
			}
			
			for(i=0;i<size;i++){
				ph_addr = kalloc();
				memset(ph_addr, 0, PGSIZE);
				fileread(p->ofile[fd], ph_addr, 4096);
				map_flag=mappages(p->pgdir, (char *)cal, PGSIZE, V2P(ph_addr), perm);
				
				cal += 4096;
				if(map_flag <0){
					kfree(ph_addr);
					return 0;
				}
			}
			m[m_count].f = p->ofile[fd];
		}
		if(anony==1){
			for(i=0;i<size;i++){
				ph_addr = kalloc();
				memset(ph_addr, 0, PGSIZE);
				map_flag=mappages(p->pgdir, (char *)cal, PGSIZE, V2P(ph_addr), perm);
				cal += 4096;
				if(map_flag < 0){
					kfree(ph_addr);
					return 0;
				}
			}
		}
	}
	fds[m_count]=fd;
	mmap_proc[p->pid]++; 
	mmap_freed[m_count]=1;
	m[m_count].p = p;
	
	p->mmaped += 1;
	mmap_area_proc[m_count] = p->pid;
	/*cprintf("FINISH\n");
	cprintf("m_count:%d, p->sz:%d\n",m_count, p->sz);
	cprintf("addr:%x, length:%d flag:%x, pid:%d\n",start_addr, m[m_count].length,flags, mmap_area_proc[m_count]);
	cprintf("sibal\naddr:%x, length:%d flag:%x, pid:%d\n",m[0].addr,m[0].length, m[0].flags, mmap_area_proc[0]);*/
	return start_addr;
}


int munmap(uint addr){
	
	struct proc *p = myproc();
	int m_count=0;
	if(mmap_proc[p->pid] == 0){
		return -1;
	}
	int i;
	for(i=0;i<64;i++){
		if(m[i].addr == addr){
			m_count=i;
			break;
		}
	}
	int fd = fds[m_count];
	if(m_count>=64){
		return -1;
	}
	int size = (m[m_count].length)/4096;
	uint cal=addr;
	pte_t *pte;
	
	cal = addr;

	for(i=0;i<size;i++){
		pte = walkpgdir(p->pgdir, (char *)cal, 1);
		
		char *ph_addr_free = P2V(PTE_ADDR(*pte));
		if((uint)ph_addr_free == 0x80000000) break;
		
		kfree(ph_addr_free);
		
		*pte = 0;
		cal=cal+4096;
	}
	int vm_addr_size = (addr-MMAPBASE)/4096;
	for(i=0;i<size;i++) p->vm_table[vm_addr_size + i] = 0;
	//close file
	if(fd>0){
		p->ofile[fd]=0;
		fds[m_count] = 0;
	}
	p->mmaped --;
	mmap_area_proc[m_count]=0;
	mmap_proc[p->pid]--;
	m[m_count].addr = 0;
	m[m_count].length=0;
	m[m_count].offset=0;
	m[m_count].prot=0;
	m[m_count].flags=0;
	mmap_freed[m_count]=0;
	return 1;
}

void
fork_mmap(struct proc *parent, struct proc *child)
{
	int m_count=0;
	int i,j;
	for(i=0;i<64;i++){
                if(mmap_freed[i] == 0){
                        m_count=i;
                        mmap_freed[m_count]=1;
                        break;
                }
        }
	int p_pid = parent->pid;
	int c_pid = child->pid;
	int cnt = mmap_proc[p_pid];
	if(cnt==0) return;
	i=0;
	while( i<cnt){
		for(j=0;j<64;j++){
			int comp_pid = mmap_area_proc[j];
			if((comp_pid != 0) && (comp_pid == p_pid)){
				m[m_count+i].addr = m[j].addr;
				m[m_count+i].length = m[j].length;
				m[m_count+i].offset = m[j].offset;
				m[m_count+i].prot = m[j].prot;
				m[m_count+i].flags = m[j].flags;
				m[m_count+i].p = child;
				mmap_area_proc[m_count+i] = c_pid;
				mmap_freed[m_count+i] = 1;
				//int k;
				//int num = m[j].addr - MMAPBASE;
				//for(k=(num / 4096);k<(num / 4096)+(m[j].length / 4096);k++) child->vm_table[k+(m[j].addr/4096)] = 1;
				int fd=fds[j];
				fds[m_count+i] = fd;
				cprintf("addr:%x,m_count:%d\n",m[m_count+i].addr, m_count+i);
				if(fd != -1) file_offset(parent->ofile[fd],0 );
				
				//mapping 
				
				i++;
			}
		}
		
	}
	for(i=0;i<4096;i++) {
		child->vm_table[i] = parent->vm_table[i];
	}
	int mmaped = parent->mmaped;
	child->mmaped = mmaped;
	mmap_proc[child->pid] = cnt;
	
}

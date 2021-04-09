#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;
int mmap_area_proc[64];
int asd;
int pf_count_trap=0;
struct mmap_area m[64];
int fds[64];
int
page_fault(struct trapframe *tf)
{

        struct proc *p = myproc();
        uint fault_addr = rcr2();
	int m_count=0;
        int i;
	int pid = p->pid;
	for(i=0;i<64;i++){
		int comp_pid = mmap_area_proc[i];
		if(m[i].addr == fault_addr && comp_pid == pid){
			m_count = i;
			break;
		}
	}
        if(m[m_count].addr != fault_addr) {
                return -1;
        }
        //int prot_flag = (m[m_count].prot) & 0x2;
        /*if(tf->err == 1 && prot_flag!=0x2) {
                cprintf("NO authority for write\n");
                return -1;
        }*/
        int length = m[m_count].length;
        int flags = m[m_count].flags;
        int size = length/4096;
        int offset = m[m_count].offset;
	int offset_size = offset/4096;
        int anony=0;
        uint cal=fault_addr;
        char *ph_addr;
        int map_flag=0;
        int perm = PTE_W|PTE_U;
        if((flags & 0x04) == 0x04) anony=1;
        if(anony==0){
                int fd = fds[m_count];
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
                        if(map_flag < 0){
                                kfree(ph_addr);
                                return -1;
                        }
                }
		
        }
        if(anony==1){
                for(i=0;i<size;i++){
                        ph_addr = kalloc();
                        memset(ph_addr, 0, PGSIZE);
                        map_flag = mappages(p->pgdir, (char *)cal, PGSIZE, V2P(ph_addr), perm);
                        cal += 4096;
                        if(map_flag < 0){
                                kfree(ph_addr);
                                return -1;
                        }
                }
        }
	pf_count_trap++;
        return 0;
}




void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
//  case T_IRQ0 + IRQ_IDE2:
//	ide2intr();
//	lpaiceoi();
//	break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  case T_PGFLT:
    asd=page_fault(myproc()->tf);
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}

/*
int
page_fault(struct trapframe *tf)
{

	struct proc *p = myproc();
	uint fault_addr = rcr2();
	int m_count = m_count_get;
	int i;

	if(m[m_count]->addr != fault_addr) {
		cprintf("no corresponding addr\n");
		return -1;
	}
	int prot_flag = (m[m_count]->prot) & 0x2;
	if(tf->err == 1 && prot_flag!=0x2) {
		cprintf("NO authority for write\n");
		return -1;
	}
	cprintf("page fault happened!\n");
	int length = m[m_count]->length;
	int flags = m[m_count]->flags;
	int size = length/4096;
	//int offset = m[m_count]->offset;
	int anony=0;
	uint cal=fault_addr;
	char *ph_addr;
	int map_flag=0;
	int perm = PTE_W|PTE_U;
	if((flags & 0x04) == 0x04) anony=1;
	if(anony==0){
		int fd = fd_get;
		for(i=0;i<size;i++){
			ph_addr = kalloc();
			memset(ph_addr, 0, PGSIZE);
			fileread(p->ofile[fd], ph_addr, 4096);
			map_flag=mappages(p->pgdir, (char *)cal, PGSIZE, V2P(ph_addr), perm);
			cal += 4096;
			if(map_flag < 0){
				kfree(ph_addr);
				return -1;
			}
		}
	}	
	if(anony==1){
		for(i=0;i<size;i++){
			ph_addr = kalloc();
			memset(ph_addr, 0, PGSIZE);
			map_flag = mappages(p->pgdir, (char *)cal, PGSIZE, V2P(ph_addr), perm);
			cal += 4096;
			if(map_flag < 0){
				kfree(ph_addr);
				return -1;
			}
		}
	}

	return 0;	
}*/

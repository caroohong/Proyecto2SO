#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

// #include "shm.h"
// #include "ipc.h"
// #include "spinlock.h"

// #define NUM_KEYS (8)
// #define NUM_PAGES (4)
// int is_key_used[NUM_KEYS];
// void *key_page_addrs[NUM_KEYS][NUM_PAGES];
// int num_key_pages[NUM_KEYS];
// int key_ref_count[NUM_KEYS];
// uint top;

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
static int
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

int shmmanip(uint token, char *addr, uint size) {
 
   uint a = (uint) addr;
   char *mem;
 
   // Asserts 
   if((uint)addr+size  >= KERNBASE)
     return -1;
 
   a = PGROUNDUP(a);
   if (size == 0) {
 
     if (myproc()->shmem_token != token) {
       cprintf("Differetn token than %d", myproc()->shmem_token);
       return -1;
     }
 
     // Loop through parent directory structure through proc->parent->pgdir
     // and get the page where va == proc->startaddr
     // and then copy the next n pages on to this addr.
 
     pde_t *ppgdir = myproc()->parent->pgdir;
     pte_t *pte;
     uint pa;
     uint i = (uint) myproc()->parent->startaddr;
     uint end = ((uint) myproc()->parent->startaddr + myproc()->parent->shmem_size);
     for(; i < end; i += PGSIZE, a+=PGSIZE){
       if((pte = walkpgdir(ppgdir, (void *) i, 0)) == 0)
         panic("shmget: pte should exist");
       if(!(*pte & PTE_P))
         panic("shmget: page not present");
       pa = PTE_ADDR(*pte);
       mappages(myproc()->pgdir, (char*) a, PGSIZE, pa, PTE_W|PTE_U);
     }
     return 0;
   }
 
   myproc()->startaddr = (void*) 0;
   for (; a < (uint)addr + size; a += PGSIZE) {
     mem = kalloc();
     if (mem == 0) {
       cprintf("shmget out of memory\n");
       return -1;
     }
     memset(mem, 0, PGSIZE);
     //mappages(proc->pgdir, (char*) a, PGSIZE, v2p(mem), PTE_W|PTE_U);
     if (myproc()->startaddr == 0) {
       myproc()->startaddr = (char*)a;
     }
   }
 
   myproc()->shmem_token = token;
   myproc()->shmem_size = size;
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
  // deallocuvm(pgdir, HEAPLIMIT, 0);
  for(i = 0; i < NPDENTRIES; i++){
    /*
    if(pgdir[i] & PTE_P) {
      for (j = 0; j < NUM_KEYS; j++) {
        if (key_ref_count[i] != 0) {  // Shared page is being used. Not to be freed for sure.
          break;
        }
        for(k = 0; k < NUM_PAGES; k++) {
          if((char*)PTE_ADDR(pgdir[i]) == key_page_addrs[i][k]) {
            break;
          }
        }
      }
      // Dont free the shared pages that are being used used
      if (j == NUM_KEYS && k == NUM_PAGES)
        kfree((char*)PTE_ADDR(pgdir[i]));
    }
    */
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
    /*
    int j, k;
    for(i = myproc()->top; i < USERTOP; i += PGSIZE){
      if((pte = walkpgdir(pgdir, (void*)i, 0)) == 0)
        panic("copyuvm: pte should exist");
      if(!(*pte & PTE_P))
        panic("copyuvm: page not present");
      pa = PTE_ADDR(*pte);

      for(j = 0; j < NUM_KEYS; j++) {
        for(k = 0; k < NUM_PAGES; k++) {
          if(myproc()->page_addrs[i][j] == (void*)i){
            break;
          }
        }
      }
      if(mappages(d, (void*)i, PGSIZE, PADDR(key_page_addrs[j][k]), PTE_W|PTE_U) < 0)
        goto bad;
    }
    
  }
  // Increase ref counts for use by child process
  for(i = 0; i < NUM_KEYS; i++) {
    if(myproc()->keys[i] == 1) {
      key_ref_count[i]++;
    }
    */
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

// shared memory
void* 
create_shared_memory_region(struct proc* p, uint size) {
  if (size == 0) {
    return 0; // Tamaño inválido, no se puede crear una región de memoria compartida vacía
  }

  void* addr = kalloc(); // Asigna una página de memoria
  if (addr == 0) {
    return 0; // Error: no hay suficiente memoria disponible
  }

  uint aligned_size = PGROUNDUP(size); // Redondea el tamaño al múltiplo de tamaño de página
  for (uint offset = 0; offset < aligned_size; offset += PGSIZE) {
    if (mappages(p->pgdir, (char*)p->sz, PGSIZE, V2P(addr + offset), PTE_W | PTE_U) < 0) {
      // Error al mapear la página
      kfree(addr); // Libera la página de memoria previamente asignada
      return 0;
    }
    p->sz += PGSIZE; // Incrementa el tamaño del proceso
  }

  return addr; // Devuelve la dirección de inicio de la región de memoria compartida
}

// shared memory
int 
copy_pte_range(pde_t *dst_pgdir, pde_t *src_pgdir, uint dst_va, uint src_va, uint size) {
  uint page_offset;
  pte_t *src_pte, *dst_pte;

  // Asegurarse de que los punteros a los directorios de página sean válidos.
  if (src_pgdir == 0 || dst_pgdir == 0) {
    return -1; // Error: punteros nulos
  }

  // Recorre la región y copia las entradas de la tabla de página.
  for (uint offset = 0; offset < size; offset += PGSIZE) {
    src_pte = walkpgdir(src_pgdir, (void *)(src_va + offset), 0);
    dst_pte = walkpgdir(dst_pgdir, (void *)(dst_va + offset), 1);

    if (src_pte == 0 || dst_pte == 0) {
      return -1; // Error: no se pueden encontrar entradas de tabla de página
    }

    // Copia la entrada de la tabla de página del proceso fuente al proceso destino.
    *dst_pte = *src_pte;
  }

  return 0; // Copia exitosa
}
// void
// shmeminit(void) {
//   int i, j;
//   for (i = 0; i < NUM_KEYS; i++) {
//     is_key_used[i] = 0;
//     num_key_pages[i] = 0;
//     key_ref_count[i] = 0;
//     for(j = 0; j < NUM_PAGES; j++) {
//       key_page_addrs[i][j] = NULL;
//     }
//   }
//   top = USERTOP;
// }

// void*
// shmgetat(int key, int num_pages)
// {
//   int i;
//   // Check for valid params
//   if (key < 0 || key > 7) {
//     return (void*)-1;
//   }
//   if (num_pages < 1 || num_pages > 4) {
//     return (void*)-1;
//   }

//   // Update ref count if process isnt already using this key
//   if (myproc()->keys[key] == 0) {
//     key_ref_count[key]++;
//     myproc()->keys[key] = 1;
//   }

//   // Return existing page mapping if key has already been used
//   if (is_key_used[key] == 1) {
//     for(i = 0; i < num_pages; i++) {
//       // map to phy add
//     }
//     return key_page_addrs[key][num_key_pages[key]-1];

//   } else { // Create a new mapping
//     // Check if trying to access already allocated memory
//     if ((top - num_pages*PGSIZE) < myproc()->sz) {
//       return (void*)-1;
//     }
//     // Allocate memory and make the mappings
//     char* mem;
//     for(i = 0; i < num_pages; i++) {
//       mem = kalloc();  // Physical memory
//       if (mem == 0) {
//         cprintf("allocuvm out of memory\n");
//         return (void*)-1;
//       }
//       memset(mem, 0, PGSIZE);

//       // VP
//       void* addr = (void*)(top - PGSIZE);  // address of available page.
//       // change address of next available page.
//       top -= PGSIZE;

//       // Map vp to pp
//       mappages(myproc()->pgdir, addr, PGSIZE, PADDR(mem), PTE_W|PTE_U);

//       key_page_addrs[key][i] = addr;
//     }
//     is_key_used[key] = 1;
//     num_key_pages[key] = num_pages;

//     return (void*)top;
//   }
// }

// // This call returns, for a particular key, how many processes currently are
// // sharing the associated pages.
// int
// shm_refcount(int key)
// {
//   // Check for valid params
//   if (key < 0 || key > 7) {
//     return -1;
//   }
//   return key_ref_count[key];
// }


#include <defs.h>
#include <stdio.h>
#include <string.h>
#include <console.h>
#include <kdebug.h>
#include <picirq.h>
#include <trap.h>
#include <clock.h>
#include <intr.h>
#include <pmm.h>
#include <vmm.h>
#include <ide.h>
#include <swap.h>
#include <proc.h>
#include <fs.h>
#include <mp.h>
#include <lapic.h>


int kern_init(void) __attribute__((noreturn));

static void lab1_switch_test(void);

static void startothers(void);

static void mpmain(void)  __attribute__((noreturn));

void seginit(void);

int
kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);

    cons_init();                // init the console

    const char *message = "(THU.CST) os is loading ...";
    cprintf("%s\n\n", message);

    print_kerninfo();

    grade_backtrace();

    pmm_init();                 // init physical memory management

    mpinit();    // collect info about this machine
    lapicinit();

    //seginit();
    
    idt_init();                 // init interrupt descriptor table
    cprintf("\ncpu%d: starting ucore\n\n", cpu->id);

    //ioapicinit();    // another interrupt controller  xv6

    pic_init();                 // init interrupt controller
    ioapicinit();    // another interrupt controller  xv6
    //startothers();

    vmm_init();                 // init virtual memory management
    sched_init();               // init scheduler
    proc_init();                // init process table
    
    ide_init();                 // init ide devices
    swap_init();                // init swap
    fs_init();                  // init fs 

    //startothers();   // start other processorsi
    //mpmain();
    clock_init();               // init clock interrupt
    intr_enable();              // enable irq interrupt

    //LAB1: CAHLLENGE 1 If you try to do it, uncomment lab1_switch_test()
    // user/kernel mode switch test
    //lab1_switch_test();
    cprintf("start other cups\n");    
    startothers();   // start other processorsi
    cprintf("after startothers");
    cpu_idle();                 // run idle process
    //startothers();   // start other processorsi
}

void
switchkvm(void)
{

//	boot_pgdir[0] = boot_pgdir[PDX(KERNBASE)];
	lcr3(v2p(boot_pgdir));   // switch to the kernel page table
//	uint32_t cr0 = rcr0();
//	cr0 |= CR0_PE | CR0_PG | CR0_AM | CR0_WP | CR0_NE | CR0_TS | CR0_EM | CR0_MP;
//	cr0 &= ~(CR0_TS | CR0_EM);
	// turn on paging
//	lcr0(cr0);    
	cprintf("page ok \n");
	seginit();
//	boot_pgdir[0]=0;

}



// Other CPUs jump here from entryother.S.
static void
mpenter(void)
{
	cprintf("123dfs\n");
	idt_init();
	//pmm_init2();
	switchkvm();
	cprintf("cpu id is  %lx\n",cpu->id);
	lapicinit();
	cprintf("after lapicinit()\n");
	mpmain();
}

// Common CPU setup code.
static void
mpmain(void)
{
	cprintf("cpu%d: starting\n", cpu->id);
	idt_init();       // load idt register
	xchg(&cpu->started, 1); // tell startothers() we're up
	//scheduler();     // start running processes
	//cpu_idle();
	while(1);
}



static inline void
lgdt_xv6(struct segdesc *p, int size)
{
	volatile ushort pd[3];

	pd[0] = size-1;
	pd[1] = (uint)p;
	pd[2] = (uint)p >> 16; 

	asm volatile("lgdt (%0)" : : "r" (pd));
}


static inline void
loadgs_xv6(ushort v)
{
	asm volatile("movw %0, %%gs" : : "r" (v));
}



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
	c = &cpus[cpunum()];
	//c = &cpus[1];
	cprintf("cpu addr 123%lx",c);
	c->gdt[SEG_KTEXT] = SEG(STA_X|STA_R, 0, 0xffffffff, 0); 
	c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0); 
	c->gdt[SEG_UTEXT] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
	c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);

	// Map cpu, and curproc
	c->gdt[SEG_KCPU] = SEG(STA_W, (&c->cpu), 8, 0); 
        cprintf("cpu addr %lx\n",&c->cpu);
	lgdt_xv6(c->gdt, sizeof(c->gdt));
	cprintf("dfsd fd\n");
	loadgs_xv6(SEG_KCPU << 3); 

	// Initialize cpu-local storage.
	cpu = c;
	proc = 0;
}
__attribute__((__aligned__(PGSIZE)))
pde_t entrypgdir[1024] = {
	  // Map VA's [0, 4MB) to PA's [0, 4MB)
	     [0] = (0) | PTE_P | PTE_W | PTE_PS,
	  //     // Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB)
	     [KERNBASE>>PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS,
	         };
	  


// Start the non-boot (AP) processors.
static void
startothers(void)
{
	extern uchar _binary_entryother_start[], _binary_entryother_size[];
	uchar *code;
	struct cpu *c; 
	char *stack;

	// Write entry code to unused memory at 0x7000.
	// The linker has placed the image of entryother.S in
	// _binary_entryother_start.
	code = p2v(0x7000);
	memmove(code, _binary_entryother_start, (uint)_binary_entryother_size);

	for(c = cpus; c < cpus+ncpu; c++){
		if(c == cpus+cpunum())  // We've started already.
			continue;
       
		// Tell entryother.S what stack to use, where to enter, and what 
		// pgdir to use. We cannot use kpgdir yet, because the AP processor
		// is running in low  memory, so we use entrypgdir for the APs too.
		stack = alloc_page();
		
		*(void**)(code-4) = stack + KSTACKSIZE;
		*(void**)(code-8) = mpenter;
		cprintf("mpenter addr %lx\n",mpenter);
		*(int**)(code-12) =  (void *)v2p(entrypgdir);
		//cprintf("boot_pgdir = %p\n",code-12);
		lapicstartap(c->id, v2p(code));
		// wait for cpu to finish mpmain()
		while(c->started == 0)
			;   
	}
}



void __attribute__((noinline))
grade_backtrace2(int arg0, int arg1, int arg2, int arg3) {
    mon_backtrace(0, NULL, NULL);
}

void __attribute__((noinline))
grade_backtrace1(int arg0, int arg1) {
    grade_backtrace2(arg0, (int)&arg0, arg1, (int)&arg1);
}

void __attribute__((noinline))
grade_backtrace0(int arg0, int arg1, int arg2) {
    grade_backtrace1(arg0, arg2);
}

void
grade_backtrace(void) {
    grade_backtrace0(0, (int)kern_init, 0xffff0000);
}

static void
lab1_print_cur_status(void) {
    static int round = 0;
    uint16_t reg1, reg2, reg3, reg4;
    asm volatile (
            "mov %%cs, %0;"
            "mov %%ds, %1;"
            "mov %%es, %2;"
            "mov %%ss, %3;"
            : "=m"(reg1), "=m"(reg2), "=m"(reg3), "=m"(reg4));
    cprintf("%d: @ring %d\n", round, reg1 & 3);
    cprintf("%d:  cs = %x\n", round, reg1);
    cprintf("%d:  ds = %x\n", round, reg2);
    cprintf("%d:  es = %x\n", round, reg3);
    cprintf("%d:  ss = %x\n", round, reg4);
    round ++;
}

static void
lab1_switch_to_user(void) {
    //LAB1 CHALLENGE 1 : TODO
}

static void
lab1_switch_to_kernel(void) {
    //LAB1 CHALLENGE 1 :  TODO
}

static void
lab1_switch_test(void) {
    lab1_print_cur_status();
    cprintf("+++ switch to  user  mode +++\n");
    lab1_switch_to_user();
    lab1_print_cur_status();
    cprintf("+++ switch to kernel mode +++\n");
    lab1_switch_to_kernel();
    lab1_print_cur_status();
}


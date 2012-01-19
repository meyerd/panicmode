/*
 * 01/2011,
 * wipe ram and shut down system
 * usage: # echo 1 > /proc/panicmode
 * needs: sysrq for sync & remount
 *
 * TODO:
 * What about unmaskable interrupts?
 * What address ranges need to be protected?
 * How to power-off system after wiping ram?
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>        /* for msleep */
//#include <linux/fs.h>           /* for emergency_sync,_remount */
#include <linux/irqflags.h>     /* for local_irq_disable */
#include <linux/proc_fs.h>      /* proc fs */
#include <linux/reboot.h>       /* for machine_shutdown */
#include <linux/sched.h>        /* for task_struct, for_each_process... */
#include <linux/sysrq.h>        /* sysrq functionality */
#include <linux/mm.h>			/* __pa __va high_memory */
#include <asm/e820.h>			/* warning x86 specific! */
#include <asm/uaccess.h>        /* for copy_from_user */

MODULE_LICENSE("GPL");

#define PROCFS_MAX_SIZE         2
#define PROCFS_NAME             "panicmode"

extern struct e820map e820;

/*
 * This structure holds information about the /proc file
 */
static struct proc_dir_entry *proc_file;

/*
 * The buffer used to store character for this module
 */
static char procfs_buffer[PROCFS_MAX_SIZE];

/*
 * The size of the buffer
 */
static unsigned long procfs_buffer_size = 0;


/*
 * Signal sysrq helper function.  Sends a signal to all user processes.
 * Copied from drivers/char/sysrq.c
 */
static void send_sig_all(int sig)
{
	struct task_struct *p;

	for_each_process(p) {
		if (p->mm && !is_global_init(p))
			/* Not swapper, init nor kernel thread */
			force_sig(sig, p);
	}
}

/*
 * Enter panic mode: terminate all tasks and wipe ram
 */
void panicmode_execute(void)
{
	/* $%"§-ing stupid ISO C90 mode ... */
	loff_t physical_start = 0;
	loff_t physical_end = 0;
	loff_t ignore_begin = 0;
	loff_t ignore_end = 0;
	int i = 0;

	printk(KERN_ALERT "PANICMODE: entering panic mode. PANIC!!1\n");

	/* shut down clean and fast */
	printk(KERN_ALERT "DEBUG sigterm\n");
	send_sig_all(SIGTERM);
	msleep(30);

	printk(KERN_ALERT "DEBUG sigkill\n");
	send_sig_all(SIGKILL);
	msleep(10);

	printk(KERN_ALERT "DEBUG sync\n");
	handle_sysrq('s'); // sync
	//emergency_sync();
	msleep(50);

	printk(KERN_ALERT "DEBUG remount\n");
	handle_sysrq('u'); // mount r/o
	//emergency_remount();

	/* switch off IRQs to make us uninterruptible */
	//TODO TASK_UNINTERRUPTIBLE?
	printk(KERN_ALERT "PANICMODE: disabling interrupts\n");
	local_irq_disable();

	/* shut down other CPUs */
	//reboot.c machine_ops.shutdown / machine_shutdown
	printk(KERN_ALERT "PANICMODE: shutting down cores\n");
	smp_send_stop();
	//machine_shutdown();

	// get two chunks of pysical continuous memory
	// then copy the tables of memory to bewiped to the
	// fist chunk, this chunk of course has to be in the tables
	// then copy the bytecode to the second chunk and 
	// signal the pysical position to it in a register
	// then jump into the wipe-bytecode

	/* some functions to remember:
	 *  - page_is_ram
	 *  - xlate_dev_mem_ptr 
	 *  - macros: __pa(x) and __va(x)
	 */

	// do we also have to add the pagetables of the kernel
	// virtual address space to the ignore list?

	physical_start = 0;
	physical_end = __pa(high_memory);

	// avoid all device mapped areas
	// stolen from page_is_ram

	// first page (4k) is bios, not ram
	physical_start += PAGE_SIZE;

	// add another bios segment
	// BIOS_BEGIN <= x < BIOS_END


	/*		for(i = 0; i < e820.nr_map; i++) {
				if (e820.map[i].type != E820_RAM) {
	// no ram add to ignore list
	ignore_begin = e820.map[i].addr;
	ignore_end = e820.map[i].addr + e820.map[i].size;
	}
	} */


	//TODO protected address space

	//TODO overwrite ram; virt_to_bus()

	//TODO
	//handle_sysrq('o'); // shut off system (how??)
	//
	printk(KERN_ALERT "PANICMODE: all done\n");
}

void calculate_required_table_space(void) {
	int i;
	unsigned long long required = 0;

	required += 2 * sizeof(loff_t);

	/*	for(i = 0; i < e820.nr_map; i++) {
			if (e820.map[i].type != E820_RAM) {
	// no ram add to ignore list
	required += 2 * sizeof(loff_t);
	}
	} */

	printk(KERN_INFO "total physical memory: %Li bytes (%Li Mb)\n", (loff_t)__pa(high_memory), 
			((loff_t)__pa(high_memory) / 1024 / 1024));

	printk(KERN_INFO "required space for ignore tables: %Li bytes\n", required);
}

/*
 * Check user input from proc filesystem
 */
int panicmode_procwrite(struct file *file, const char *buffer,
		unsigned long count, void *data)
{
	/* get buffer size */
	procfs_buffer_size = count;
	if (procfs_buffer_size > PROCFS_MAX_SIZE ) {
		procfs_buffer_size = PROCFS_MAX_SIZE;
	}

	/* write data to the buffer */
	if ( copy_from_user(procfs_buffer, buffer, procfs_buffer_size) ) {
		return -EFAULT;
	}

	/* unleash the panic: # echo 1 > /proc/panicmode */
	if (procfs_buffer[0] == '1') {
		panicmode_execute();
	} else if(procfs_buffer[0] == '2') {
		calculate_required_table_space();
		return 0;
	}

	return -EINVAL;
}

/*
 * Called on module load
 */
static int panicmode_init(void)
{
	printk(KERN_ALERT "DEBUG loading panic mode\n");

	/* create the /proc file */
	proc_file = create_proc_entry(PROCFS_NAME, 0600, NULL);

	if (proc_file == NULL) {
		remove_proc_entry(PROCFS_NAME, NULL);
		printk(KERN_ALERT "Error: Could not initialize /proc/%s\n",
				PROCFS_NAME);
		return -ENOMEM;
	}

	proc_file->write_proc = panicmode_procwrite;
	proc_file->mode       = S_IFREG | S_IRUGO;
	proc_file->uid        = 0;
	proc_file->gid        = 0;
	proc_file->size       = 0;

	printk(KERN_ALERT "DEBUG panic mode loaded\n");
	return 0;
}

/*
 * Called on module unload
 */
static void panicmode_exit(void)
{
	printk(KERN_ALERT "DEBUG unloading panic mode\n");
	remove_proc_entry(PROCFS_NAME, NULL);
	printk(KERN_ALERT "DEBUG panic mode unloaded\n");
}


module_init(panicmode_init);
module_exit(panicmode_exit);

/*
 * 01/2011 wipe ram and shut down amd64 system against cold boot attack
 *
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
#include <linux/mm.h>           /* __pa __va high_memory */
#include <asm/e820.h>           /* warning x86 specific! */
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
 * Contains assembler to be stored at a safe place.
 * Switches to long mode and overwrites all RAM.
 */
static void wiper(unsigned long memsize)
{
        printk(KERN_ALERT "PANICMODE: jump complete.");
        printk(KERN_ALERT "Everybody head back to base for debriefing and cocktails.\n");
        //TODO:
        /* create GDT */
        /*
        // Selector 0x00 cannot be used
        GDT[0] = {.base=0, .limit=0, .type=0};
        // Selector 0x08 will be our code
        GDT[1] = {.base=0, .limit=memsize, .type=0x9A};
        // Selector 0x10 will be our data
        GDT[2] = {.base=0, .limit=memsize, .type=0x92};
        // You can use LTR(0x18)
        GDT[3] = {.base=&myTss, .limit=sizeof(myTss), .type=0x89};
        */

        /* switch off paging */
        /*
        mov eax, cr0 ; Read CR0.
        and eax,7FFFFFFFh; Set PE=0
        mov cr0, eax ; Write CR0.
        ...
        */

        /* switch to long mode */
        /* overwrite ram */
        //use memsize
        /* power off system */
}

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
static void panicmode_execute(void)
{
        unsigned long safemem;
        struct sysinfo info;
        unsigned long memsize;

        printk(KERN_ALERT "PANICMODE: entering panic mode. PANIC!!1\n");
        //TODO check cpuid if we actually are amd64 else we're fucked

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
        printk(KERN_ALERT "PANICMODE: shutting down cores\n");
        smp_send_stop();
        //machine_shutdown();

        //TODO
        /* find address space to store asm */
        //safemem = __pa(high_memory) + 5*1024*1024;
        safemem = 0xffffffff816d41bd;

        /* copy asm */
        memcpy(&safemem, &wiper, 1024);

        /* jump into asm */
        si_meminfo(&info);
        memsize = info.totalram;
        printk(KERN_ALERT "PANICMODE: %8lu kB total memory\n", memsize);
        printk(KERN_ALERT "PANICMODE: jumping into assembly\n");
        ((void (*)(unsigned long)) safemem)(memsize);
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

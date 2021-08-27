#ifndef _NF_INTERNALS_H
#define _NF_INTERNALS_H

#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#ifdef CONFIG_NETFILTER_DEBUG
#define NFDEBUG(format, args...)  printk(KERN_DEBUG format , ## args)
#else
#define NFDEBUG(format, args...)
#endif


/* core.c */
unsigned int nf_iterate(struct list_head *head, struct sk_buff *skb,
			struct nf_hook_state *state, struct nf_hook_ops **elemp);

/* nf_queue.c */
#ifndef VENDOR_EDIT
//Junyuan.Huang@PSW.CN.WiFi.Network.1471780, 2018/06/26,
//Modify for limit speed function
int nf_queue(struct sk_buff *skb, struct nf_hook_ops *elem,
	     struct nf_hook_state *state, unsigned int queuenum);
#else /* VENDOR_EDIT */
#if defined(CONFIG_IMQ) || defined(CONFIG_IMQ_MODULE)
int nf_queue(struct sk_buff *skb, struct nf_hook_ops *elem,
	     struct nf_hook_state *state, unsigned int queuenum, unsigned int queuetype);
#else
int nf_queue(struct sk_buff *skb, struct nf_hook_ops *elem,
	     struct nf_hook_state *state, unsigned int queuenum);
#endif
#endif /* VENDOR_EDIT */

void nf_queue_nf_hook_drop(struct net *net, struct nf_hook_ops *ops);
int __init netfilter_queue_init(void);

/* nf_log.c */
int __init netfilter_log_init(void);

#endif

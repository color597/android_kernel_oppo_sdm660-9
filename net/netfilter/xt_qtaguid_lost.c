/***********************************************************
** File: - xt_qtaguid_lost.c
** VENDOR_EDIT
** Copyright (C), 2016, OPPO Mobile Comm Corp., Ltd
**
** Abstract: - count lost package
**
**
** Version: 1
** Date created: 2016/09/28
** Author: Jiemin.Zhu@Swdp.Android.OppoFeature.TrafficMonitor
** ------------------------------- Revision History: ---------------------------------------
** 		<author>	    <data>			<desc>
**      Jiemin.Zhu    2016/09/28    create this file
****************************************************************/

#define NOTAG_PACKAGE_SADDR 0xfefc
#define NOTAG_PACKAGE_SPORT	65535
#define	IP_HASHBITS	8
#define IPPROTO_UNUSED	222
#define LOST_STAT_TIMEOUT		1000 * 30 /* remove rb_node when tigger */
#define LOST_STAT_REPORT_THRESHOLD	3 * 1024 * 1024 /* 3MB */

enum lost_stat_enum {
	/* lost traffic enable */
	LS_ENABLE = 0,
	/* close lost socket enable */
	LS_CLOSE_ENABLE,
	/* timeout for remove entry from lost stat rbtree and uid list */
	LS_TIMEOUT,
	/* thresh for report lost stat to userspace by uevent */
	LS_REPORT_THRESHOLD,
	/* upload lost stat log enable */
	LS_UPLOAD_LOG,
	/* upload time thresh */
	LS_UPLOAD_LOG_THRESHOLD,
	/* enable wifi for statistics, just for test, must be 0 in release version */
	LS_WIFI_ENABLE,
	LS_MAX,
};

static long lost_stat_params[LS_MAX];

static struct kobject *qtaguid_module_kobj = NULL;

#define IPV4_WORK	1
#define IPV6_WORK	2

struct lost_stat_work_struct {
	struct work_struct work;
	void *data;
	struct iface_stat *iface_entry;
	int type;
};
static struct lost_stat_work_struct lost_stat_work;
static struct lost_stat_work_struct lost_stat_uid_work;

struct ipv4_head {
	__be32 daddr;
	__be32 saddr;
	__be16 dport;
	__be16 sport;
	u8 protocol;
};
struct ipv6_head {
	struct in6_addr daddr;
	struct in6_addr saddr;
	__be16 dport;
	__be16 sport;
	int protocol;
};
struct lost_node {
	struct rb_node node;
	int hash;
};

struct ipv4_lost_stat {
	struct lost_node ln;
	struct byte_packet_counters counters[IFS_MAX_DIRECTIONS];
	bool notag_package;
	unsigned char state;
	u_int8_t family;
	struct ipv4_head ipv4hdr;
	int index;
	unsigned long jiffies;
	unsigned long last_jiffies;
};

struct ipv6_lost_stat {
	struct lost_node ln;
	struct byte_packet_counters counters[IFS_MAX_DIRECTIONS];
	bool notag_package;
	unsigned char state;
	u_int8_t family;
	struct ipv6_head ipv6hdr;
	int index;
	unsigned long jiffies;
	unsigned long last_jiffies;
};

struct sock_work_info {
	uid_t uid;
	struct byte_packet_counters counters[IFS_MAX_DIRECTIONS];
	__be32 addr;
	__be16 port;
	u8 protocol;
};

struct sock_hash_info {
	struct rb_node node;
	int hash;
};

struct sock_list_info {
	struct list_head list;
	uid_t uid;
	struct byte_packet_counters counters[IFS_MAX_DIRECTIONS]; //count for lostowner or lostfile
	unsigned long jiffies;	//current jiffies
	unsigned long lost_jiffies;	//lost jiffies
};

struct ip4_sock_info {
	struct sock_hash_info si; /* rbtree for same ip,port,protocol */
	struct sock_list_info ls;	/* list for different uids in the same rbtree */
	struct list_head head;
	__be32 addr;
	__be16 port;
	u8 protocol;
	unsigned long jiffies;
};

#define MAX_EVENT_PARAM	9

static struct in6_addr notag_ip6;

static inline int hash_ip6(const struct in6_addr *ip, __be16 sport, int tproto)
{
	u32 hash;

	hash = (__force u32)(ip->s6_addr32[0] ^ ip->s6_addr32[1] ^
			     ip->s6_addr32[2] ^ ip->s6_addr32[3] ^ sport ^ tproto);

	return hash * GOLDEN_RATIO_PRIME_32;
}

static inline int hash_ip4(const __be32 s_addr) {
	u32 hash;

	hash = (__force unsigned int)(s_addr);

	return hash * GOLDEN_RATIO_PRIME_32;
}

static int lost_node_tree_insert(struct lost_node *data, struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	while (*new) {
		struct lost_node *this = rb_entry(*new, struct lost_node, node);
		parent = *new;
		if (data->hash > this->hash)
			new = &((*new)->rb_left);
		else if (data->hash < this->hash)
			new = &((*new)->rb_right);
		else {
			pr_err("cannot find lost node hash");
			return -1;
		}
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);

	return 0;
}

static int ipv4_stat_tree_insert(struct ipv4_lost_stat *stat, struct rb_root *root)
{
	return lost_node_tree_insert(&stat->ln, root);
}

static int ipv6_stat_tree_insert(struct ipv6_lost_stat *stat, struct rb_root *root)
{
	return lost_node_tree_insert(&stat->ln, root);
}

static struct lost_node* lost_node_tree_search(struct rb_root *root, int hash)
{
	struct rb_node *node = root->rb_node;

	while(node) {
		struct lost_node *data = rb_entry(node, struct lost_node, node);
		if (data->hash < hash)
			node = node->rb_left;
		else if(data->hash > hash)
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}

static int
extract_icmp4_fields(const struct sk_buff *skb,
		    u8 *protocol,
		    __be32 *raddr,
		    __be32 *laddr,
		    __be16 *rport,
		    __be16 *lport)
{
	unsigned int outside_hdrlen = ip_hdrlen(skb);
	struct iphdr *inside_iph, _inside_iph;
	struct icmphdr *icmph, _icmph;
	__be16 *ports, _ports[2];

	icmph = skb_header_pointer(skb, outside_hdrlen,
				   sizeof(_icmph), &_icmph);
	if (icmph == NULL)
		return 1;

	switch (icmph->type) {
	case ICMP_DEST_UNREACH:
	case ICMP_SOURCE_QUENCH:
	case ICMP_REDIRECT:
	case ICMP_TIME_EXCEEDED:
	case ICMP_PARAMETERPROB:
		break;
	default:
		return 1;
	}

	inside_iph = skb_header_pointer(skb, outside_hdrlen +
					sizeof(struct icmphdr),
					sizeof(_inside_iph), &_inside_iph);
	if (inside_iph == NULL)
		return 1;

	if (inside_iph->protocol != IPPROTO_TCP &&
	    inside_iph->protocol != IPPROTO_UDP)
		return 1;

	ports = skb_header_pointer(skb, outside_hdrlen +
				   sizeof(struct icmphdr) +
				   (inside_iph->ihl << 2),
				   sizeof(_ports), &_ports);
	if (ports == NULL)
		return 1;

	/* the inside IP packet is the one quoted from our side, thus
	 * its saddr is the local address */
	*protocol = inside_iph->protocol;
	*laddr = inside_iph->saddr;
	*lport = ports[0];
	*raddr = inside_iph->daddr;
	*rport = ports[1];

	return 0;
}

static int
extract_icmp6_fields(const struct sk_buff *skb,
		     unsigned int outside_hdrlen,
		     int *protocol,
		     struct in6_addr **raddr,
		     struct in6_addr **laddr,
		     __be16 *rport,
		     __be16 *lport)
{
	struct ipv6hdr *inside_iph, _inside_iph;
	struct icmp6hdr *icmph, _icmph;
	__be16 *ports, _ports[2];
	u8 inside_nexthdr;
	__be16 inside_fragoff;
	int inside_hdrlen;

	icmph = skb_header_pointer(skb, outside_hdrlen,
				   sizeof(_icmph), &_icmph);
	if (icmph == NULL)
		return 1;

	if (icmph->icmp6_type & ICMPV6_INFOMSG_MASK)
		return 1;

	inside_iph = skb_header_pointer(skb, outside_hdrlen + sizeof(_icmph), sizeof(_inside_iph), &_inside_iph);
	if (inside_iph == NULL)
		return 1;
	inside_nexthdr = inside_iph->nexthdr;

	inside_hdrlen = ipv6_skip_exthdr(skb, outside_hdrlen + sizeof(_icmph) + sizeof(_inside_iph),
					 &inside_nexthdr, &inside_fragoff);
	if (inside_hdrlen < 0)
		return 1; /* hjm: Packet has no/incomplete transport layer headers. */

	if (inside_nexthdr != IPPROTO_TCP &&
	    inside_nexthdr != IPPROTO_UDP)
		return 1;

	ports = skb_header_pointer(skb, inside_hdrlen,
				   sizeof(_ports), &_ports);
	if (ports == NULL)
		return 1;

	/* the inside IP packet is the one quoted from our side, thus
	 * its saddr is the local address */
	*protocol = inside_nexthdr;
	*laddr = &inside_iph->saddr;
	*lport = ports[0];
	*raddr = &inside_iph->daddr;
	*rport = ports[1];

	return 0;
}

static void reset_counter(struct byte_packet_counters *counter)
{
	counter->bytes = 0;
	counter->packets = 0;
}

static int analysis_ip6_package(const struct sk_buff *skb, struct xt_action_param *par,
			struct iface_stat *iface_entry, const enum ifs_tx_rx direction)
{
	struct ipv6_lost_stat *ipv6_ls;
	struct ipv6hdr *iph = ipv6_hdr(skb);
	struct udphdr _hdr, *hp = NULL;
	int thoff = 0, uninitialized_var(tproto);
	struct in6_addr *daddr = NULL, *saddr = NULL;
	__be16 uninitialized_var(dport), uninitialized_var(sport);
	bool notag_package = false;
	int hash;
	struct lost_node *node;

	tproto = ipv6_find_hdr(skb, &thoff, -1, NULL, NULL);
	if (tproto < 0) {
		pr_debug("unable to find transport header in IPv6 packet, dropping\n");
		return -EINVAL;
	}

	if (tproto == IPPROTO_UDP || tproto == IPPROTO_TCP) {
		hp = skb_header_pointer(skb, thoff,
					sizeof(_hdr), &_hdr);
		if (hp == NULL)
			return -EINVAL;

		saddr = &iph->saddr;
		sport = hp->source;
		daddr = &iph->daddr;
		dport = hp->dest;
	} else if (tproto == IPPROTO_ICMPV6) {
		if (extract_icmp6_fields(skb, thoff, &tproto, &saddr, &daddr,
				 		&sport, &dport)) {
			notag_package = true;
			saddr = &notag_ip6;
			daddr = &notag_ip6;
			sport = NOTAG_PACKAGE_SPORT;
		}
	} else {
		notag_package = true;
		saddr = &notag_ip6;
		daddr = &notag_ip6;
		sport = NOTAG_PACKAGE_SPORT;
	}

	if (direction == IFS_TX) {
		hash = hash_ip6(daddr, dport, tproto);
	} else
		hash = hash_ip6(saddr, sport, tproto);

	spin_lock(&iface_entry->lost_stat_tree_lock);
	node = lost_node_tree_search(&iface_entry->ipv6_lost_stat_tree, hash);
	if (!node) {
		ipv6_ls = kzalloc(sizeof(*ipv6_ls), GFP_ATOMIC);
		if (!ipv6_ls) {
			spin_unlock(&iface_entry->lost_stat_tree_lock);
			return -ENOMEM;
		}

		ipv6_ls->notag_package = notag_package;
		ipv6_ls->ipv6hdr.saddr = *saddr;
		ipv6_ls->ipv6hdr.dport = dport;
		ipv6_ls->family = par->family;
		ipv6_ls->ipv6hdr.daddr = *daddr;
		ipv6_ls->ipv6hdr.sport = sport;	
		ipv6_ls->ipv6hdr.protocol = tproto;
		ipv6_ls->ln.hash = hash;
		ipv6_ls->last_jiffies = jiffies;
		ipv6_ls->jiffies = jiffies;

		if (ipv6_stat_tree_insert(ipv6_ls, &iface_entry->ipv6_lost_stat_tree) != 0) {
			kfree(ipv6_ls);
			spin_unlock(&iface_entry->lost_stat_tree_lock);
			return 0;
		}
		node = &ipv6_ls->ln;
	} else {
		ipv6_ls = rb_entry(&node->node, struct ipv6_lost_stat, ln.node);
	}

	ipv6_ls->index++;	
	ipv6_ls->counters[direction].bytes += skb->len;
	ipv6_ls->counters[direction].packets++;
	ipv6_ls->jiffies = jiffies;

	if (jiffies_to_msecs(jiffies) - jiffies_to_msecs(ipv6_ls->last_jiffies)
			< lost_stat_params[LS_TIMEOUT]) {
		if (ipv6_ls->counters[direction].bytes > lost_stat_params[LS_REPORT_THRESHOLD] &&
			!lost_stat_work.type) {
			lost_stat_work.data = (void*)ipv6_ls;
			lost_stat_work.iface_entry = iface_entry;
			lost_stat_work.type = IPV4_WORK;
			rb_erase(&node->node, &iface_entry->ipv4_lost_stat_tree);
			LS_DEBUG("[lost_stat] report lost for %s\n", iface_entry->ifname);
			schedule_work(&lost_stat_work.work);
		}
	} else {
		ipv6_ls->index = 0;
		ipv6_ls->jiffies = jiffies;
		ipv6_ls->last_jiffies = jiffies;
		reset_counter(&ipv6_ls->counters[IFS_TX]);
		reset_counter(&ipv6_ls->counters[IFS_RX]);
		ipv6_ls->counters[direction].bytes += skb->len;
		ipv6_ls->counters[direction].packets++;
	}

	spin_unlock(&iface_entry->lost_stat_tree_lock);

	return 0;
}

static int analysis_ip4_package(const struct sk_buff *skb, struct xt_action_param *par,
			struct iface_stat *iface_entry, const enum ifs_tx_rx direction)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct udphdr _hdr, *hp = NULL;
	__be32 daddr, saddr;
	__be16 dport, sport;
	u8 protocol;
	bool notag_package = false;
	struct ipv4_lost_stat *ipv4_ls;
	struct lost_node *node;
	int hash;

	if (iph->protocol == IPPROTO_UDP || iph->protocol == IPPROTO_TCP) {
		hp = skb_header_pointer(skb, ip_hdrlen(skb),
					sizeof(_hdr), &_hdr);
		if (hp == NULL)
			return -EINVAL;

		protocol = iph->protocol;
		saddr = iph->saddr;
		sport = hp->source;
		daddr = iph->daddr;
		dport = hp->dest;

	} else if (iph->protocol == IPPROTO_ICMP) {
		if (extract_icmp4_fields(skb, &protocol, &saddr, &daddr,
					&sport, &dport)) {
			protocol = IPPROTO_UNUSED;
			notag_package = true;
			saddr = NOTAG_PACKAGE_SADDR;
			sport = NOTAG_PACKAGE_SPORT;
		}
	} else {
		notag_package = true;
		protocol = IPPROTO_UNUSED;
		saddr = NOTAG_PACKAGE_SADDR;
		daddr = NOTAG_PACKAGE_SADDR;
		sport = NOTAG_PACKAGE_SPORT;
		dport = NOTAG_PACKAGE_SPORT;
	}

	if (direction == IFS_TX) {
		hash = hash_ip4(daddr ^ dport ^ protocol);
	} else
		hash = hash_ip4(saddr ^ sport ^ protocol);

	spin_lock(&iface_entry->lost_stat_tree_lock);
	node = lost_node_tree_search(&iface_entry->ipv4_lost_stat_tree, hash);
	if (!node) {
		ipv4_ls = kzalloc(sizeof(*ipv4_ls), GFP_ATOMIC);
		if (!ipv4_ls) {
			spin_unlock(&iface_entry->lost_stat_tree_lock);
			return -ENOMEM;
		}

		ipv4_ls->ipv4hdr.daddr = daddr;
		ipv4_ls->ipv4hdr.saddr = saddr;
		ipv4_ls->ipv4hdr.protocol = protocol;
		ipv4_ls->ipv4hdr.dport = dport;
		ipv4_ls->ipv4hdr.sport = sport;
		ipv4_ls->family = par->family;
		ipv4_ls->notag_package = notag_package;	
		ipv4_ls->ln.hash = hash;
		ipv4_ls->last_jiffies = jiffies;
		ipv4_ls->jiffies = jiffies;

		if (ipv4_stat_tree_insert(ipv4_ls, &iface_entry->ipv4_lost_stat_tree) != 0) {
			kfree(ipv4_ls);
			spin_unlock(&iface_entry->lost_stat_tree_lock);
			return 0;
		}
		node = &ipv4_ls->ln;
	} else {
		ipv4_ls = rb_entry(&node->node, struct ipv4_lost_stat, ln.node);
	}

	ipv4_ls->index++;
	ipv4_ls->counters[direction].bytes += skb->len;
	ipv4_ls->counters[direction].packets++;
	ipv4_ls->jiffies = jiffies;

	//within "lost_stat_params[LS_TIMEOUT]", check threshold
	if (jiffies_to_msecs(jiffies) - jiffies_to_msecs(ipv4_ls->last_jiffies)
			< lost_stat_params[LS_TIMEOUT]) {
		if (ipv4_ls->counters[direction].bytes > lost_stat_params[LS_REPORT_THRESHOLD] &&
			!lost_stat_work.type) {
			lost_stat_work.data = (void*)ipv4_ls;
			lost_stat_work.iface_entry = iface_entry;
			lost_stat_work.type = IPV4_WORK;
			rb_erase(&node->node, &iface_entry->ipv4_lost_stat_tree);
			LS_DEBUG("[lost_stat] report lost for %s\n", iface_entry->ifname);
			LS_DEBUG("[lost_stat] report %p %08X:%04X, protocol %d in rbtree, hash: %d, search hash: %d\n",
						ipv4_ls,
						ntohl(ipv4_ls->ipv4hdr.saddr),
						ntohs(ipv4_ls->ipv4hdr.sport),
						ipv4_ls->ipv4hdr.protocol,
						ipv4_ls->ln.hash, hash);
			schedule_work(&lost_stat_work.work);
		}
	} else {
		ipv4_ls->index = 0;
		ipv4_ls->jiffies = jiffies;
		ipv4_ls->last_jiffies = jiffies;
		reset_counter(&ipv4_ls->counters[IFS_TX]);
		reset_counter(&ipv4_ls->counters[IFS_RX]);
		ipv4_ls->counters[direction].bytes += skb->len;
		ipv4_ls->counters[direction].packets++;
	}

	spin_unlock(&iface_entry->lost_stat_tree_lock);

	return 0;
}

static bool is_data_network(const struct iface_stat *iface_entry)
{
	bool ret = false;
	struct net_device *dev;

	if (iface_entry->active) {
		dev = iface_entry->net_dev;
		if (dev && strncmp(dev->name, "rmnet_data", 10) == 0)
			ret = true;
		if (lost_stat_params[LS_WIFI_ENABLE]) {
			if (dev && strncmp(dev->name, "wlan", 4) == 0)
				ret = true;
		}
	}

	return ret;
}

static int analysis_lostowner_package(const struct sk_buff *skb,
							struct xt_action_param *par, char *reason)
{
	const struct net_device *el_dev;
	enum ifs_tx_rx direction;
	struct iface_stat *iface_entry; 
	unsigned int hook_mask = (1 << par->hooknum);
	int ret;

	if (!lost_stat_params[LS_ENABLE])
		return 0;

	if (!skb->dev) {
		MT_DEBUG("qtaguid[%d]: no skb->dev\n", par->hooknum);
		el_dev = par->in ? : par->out;
	} else {
		const struct net_device *other_dev;
		el_dev = skb->dev;
		other_dev = par->in ? : par->out;
		if (el_dev != other_dev) {
			MT_DEBUG("qtaguid[%d]: skb->dev=%p %s vs "
				"par->(in/out)=%p %s\n",
				par->hooknum, el_dev, el_dev->name, other_dev,
				other_dev->name);
		}
	}
	if (unlikely(!el_dev)) {
		pr_info("qtaguid[%d]: no par->in/out?!!\n", par->hooknum);
		return 0;
	}
	direction = par->in ? IFS_RX : IFS_TX;

	spin_lock_bh(&iface_stat_list_lock);
	iface_entry = get_iface_entry(el_dev->name);
	if (!iface_entry) {
		ret = -EINVAL;
		goto out;
	}
	if (!is_data_network(iface_entry)) {
		ret = 0;
		goto out;
	}
	spin_unlock_bh(&iface_stat_list_lock);

	if (!(hook_mask & XT_SOCKET_SUPPORTED_HOOKS)) {
		return 0;
	}

	switch(par->family) {
	case NFPROTO_IPV6:
		analysis_ip6_package(skb, par, iface_entry, direction);
		break;
	case NFPROTO_IPV4:
		analysis_ip4_package(skb, par, iface_entry, direction);
		break;
	default:
		pr_err("what the fuck!!! The package neither is ipv4 nor ipv6, it's %d\n", par->family);
		break;
	}

	return 0;

out:
	spin_unlock_bh(&iface_stat_list_lock);
	return ret;
}

static void lost_stat_work_func(struct work_struct *work)
{
	char *lost_stat_param[MAX_EVENT_PARAM] = { "LOST_STAT=LOST", NULL };
	struct lost_stat_work_struct *work_data = container_of(work, struct lost_stat_work_struct, work);
	struct ipv4_lost_stat * ip4_lt;
	struct ipv6_lost_stat * ip6_lt;
	struct iface_stat *iface_entry;
	int i, j = 0;

	if (!work_data)
		return ;

	iface_entry = work_data->iface_entry;
	if (!iface_entry) {
		work_data->type = 0;
		return;
	}

	for(i = 1; i < MAX_EVENT_PARAM - 1; i++) {
		lost_stat_param[i] = kzalloc(100, GFP_KERNEL);
		if (!lost_stat_param[i]) {
			LS_DEBUG("[lost_stat] kzalloc uevent param failed\n");
			goto free_memory;
		}
	}

	switch(work_data->type) {
	case IPV4_WORK:
		ip4_lt = (struct ipv4_lost_stat*)work_data->data;
		if (ip4_lt && (ip4_lt->counters[IFS_TX].bytes > lost_stat_params[LS_REPORT_THRESHOLD] ||
					ip4_lt->counters[IFS_RX].bytes > lost_stat_params[LS_REPORT_THRESHOLD])) {
			sprintf(lost_stat_param[++j], "SIP=%X", ntohl(ip4_lt->ipv4hdr.saddr));
			sprintf(lost_stat_param[++j], "SPORT=%X", ntohs(ip4_lt->ipv4hdr.sport));
			sprintf(lost_stat_param[++j], "PROTOCOL=%d", ip4_lt->ipv4hdr.protocol);
			sprintf(lost_stat_param[++j], "TX=%llu", ip4_lt->counters[IFS_TX].bytes);
			sprintf(lost_stat_param[++j], "RX=%llu", ip4_lt->counters[IFS_RX].bytes);
			sprintf(lost_stat_param[++j], "UID=%d", 0);
			sprintf(lost_stat_param[++j], "CLASSIFY=%s", "lost");
			LS_DEBUG("[lost_stat] report in kthread %p %s %s %s %s %s\n", ip4_lt,
					lost_stat_param[1], lost_stat_param[2], lost_stat_param[3],
					lost_stat_param[4], lost_stat_param[5]);
			lost_stat_param[MAX_EVENT_PARAM - 1] = NULL;
			kobject_uevent_env(qtaguid_module_kobj, KOBJ_CHANGE, lost_stat_param);

			kfree(ip4_lt);
			ip4_lt = NULL;
		}
		break;
	case IPV6_WORK:
		ip6_lt = (struct ipv6_lost_stat*)work_data->data;
		if (ip6_lt && (ip6_lt->counters[IFS_TX].bytes > lost_stat_params[LS_REPORT_THRESHOLD] ||
					ip6_lt->counters[IFS_RX].bytes > lost_stat_params[LS_REPORT_THRESHOLD])) {
			sprintf(lost_stat_param[++j], "SIP=%X%X%X%X",
						ntohl(ip6_lt->ipv6hdr.saddr.s6_addr32[0]),
						ntohl(ip6_lt->ipv6hdr.saddr.s6_addr32[1]),
						ntohl(ip6_lt->ipv6hdr.saddr.s6_addr32[2]),
						ntohl(ip6_lt->ipv6hdr.saddr.s6_addr32[3]));
			sprintf(lost_stat_param[++j], "SPORT=%X", ntohs(ip6_lt->ipv6hdr.sport));
			sprintf(lost_stat_param[++j], "PROTOCOL=%d", ip6_lt->ipv6hdr.protocol);
			sprintf(lost_stat_param[++j], "TX=%llu", ip6_lt->counters[IFS_TX].bytes);
			sprintf(lost_stat_param[++j], "RX=%llu", ip6_lt->counters[IFS_RX].bytes);
			sprintf(lost_stat_param[++j], "UID=%d", 0);
			sprintf(lost_stat_param[++j], "CLASSIFY=%s", "lost");
			lost_stat_param[MAX_EVENT_PARAM - 1] = NULL;
			kobject_uevent_env(qtaguid_module_kobj, KOBJ_CHANGE, lost_stat_param);

			kfree(ip6_lt);
			ip6_lt = NULL;
		}
		break;
	default:
		break;
	}

free_memory:
	for(i--; i > 0; i--) {
		kfree(lost_stat_param[i]);
	}
	work_data->type = 0;
}

static void lost_stat_uid_work_func(struct work_struct *work)
{
	struct lost_stat_work_struct *work_data = container_of(work, struct lost_stat_work_struct, work);
	struct iface_stat *iface_entry;
	struct sock_work_info *info;
	char *lost_stat_param[MAX_EVENT_PARAM] = { "LOST_STAT=LOST", NULL };
	int i, j = 0;

	if (!work_data)
		return ;

	iface_entry = work_data->iface_entry;
	if (!iface_entry) {
		work_data->type = 0;
		return;
	}
	info = (struct sock_work_info*)work_data->data;
	if (!info) {
		work_data->type = 0;
		return;
	}

	for(i = 1; i < MAX_EVENT_PARAM - 1; i++) {
		lost_stat_param[i] = kzalloc(100, GFP_KERNEL);
		if (!lost_stat_param[i]) {
			LS_DEBUG("[lost_stat] kzalloc uevent param failed\n");
			goto free_memory;
		}
	}

	sprintf(lost_stat_param[++j], "SIP=%X", ntohl(info->addr));
	sprintf(lost_stat_param[++j], "SPORT=%X", ntohs(info->port));
	sprintf(lost_stat_param[++j], "PROTOCOL=%d", info->protocol);
	sprintf(lost_stat_param[++j], "TX=%llu", info->counters[IFS_TX].bytes);
	sprintf(lost_stat_param[++j], "RX=%llu", info->counters[IFS_RX].bytes);
	sprintf(lost_stat_param[++j], "UID=%d", info->uid);
	sprintf(lost_stat_param[++j], "CLASSIFY=%s", "app");
	LS_DEBUG("[lost_stat_uid] report in kthread %p %s %s %s %s %s %s\n", info,
			lost_stat_param[1], lost_stat_param[2], lost_stat_param[3],
			lost_stat_param[4], lost_stat_param[5], lost_stat_param[6]);
	lost_stat_param[MAX_EVENT_PARAM - 1] = NULL;
	kobject_uevent_env(qtaguid_module_kobj, KOBJ_CHANGE, lost_stat_param);

free_memory:
	for(i--; i > 0; i--) {
		kfree(lost_stat_param[i]);
	}
	kfree(info);
	work_data->type = 0;
}

static struct sock_hash_info *__sock_info_node_tree_search(struct rb_root *root, int hash)
{
	struct rb_node *node = root->rb_node;

	while(node) {
		struct sock_hash_info *data = rb_entry(node, struct sock_hash_info, node);
		if (data->hash < hash)
			node = node->rb_left;
		else if(data->hash > hash)
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}

static uid_t __sock_list_get_ip4(struct ip4_sock_info *sock_info,
						struct sock_list_info *find_list)
{
	struct sock_list_info *list;
	unsigned long cur_jiffies;
	uid_t uid = 0;
	bool is_first = true;

	list_for_each_entry(list, &sock_info->head, list) {
		if (is_first) {
			cur_jiffies = list->jiffies;
			find_list = list;
			uid = list->uid;
			is_first = false;
			continue;
		}
		if (time_before(cur_jiffies, list->jiffies)) {
			uid = list->uid;
			find_list = list;
			cur_jiffies = list->jiffies;
		}
	}
	return uid;
}

static uid_t __search_sock_info_ip4(const struct sk_buff *skb,
					struct iface_stat *iface_entry,
					__be32 addr, __be16 port, u8 protocol,
					enum ifs_tx_rx direction)
{
	int hash;
	struct sock_hash_info *info;
	struct sock_work_info *work_info;
	struct ip4_sock_info *sock_info;
	struct sock_list_info *find_list = NULL;
	uid_t uid = 0;

	hash = hash_ip4(addr ^ port ^ protocol);

	spin_lock(&iface_entry->sock_info_tree_lock);
	info = __sock_info_node_tree_search(&iface_entry->ip4_sock_info_tree, hash);
	if (info) {
		sock_info = rb_entry(&info->node, struct ip4_sock_info, si.node);
		uid = __sock_list_get_ip4(sock_info, find_list);
		if (find_list) {
			if (unlikely(find_list->lost_jiffies == 0))
				find_list->lost_jiffies = jiffies;
			find_list->counters[direction].bytes += skb->len;
			find_list->counters[direction].packets++;
			//within "lost_stat_params[LS_TIMEOUT]", check threshold
			if ((jiffies_to_msecs(jiffies) - jiffies_to_msecs(find_list->lost_jiffies))
					< lost_stat_params[LS_TIMEOUT]) {
				if (find_list->counters[direction].bytes > lost_stat_params[LS_REPORT_THRESHOLD]) {
					work_info = kzalloc(sizeof(*work_info), GFP_ATOMIC);
					if (!work_info) {
						goto unlock;
					}
					work_info->uid = uid;
					work_info->addr = addr;
					work_info->port = port;
					work_info->protocol = protocol;
					work_info->counters[direction].bytes = find_list->counters[direction].bytes;
					lost_stat_uid_work.data = (void*)work_info;
					lost_stat_uid_work.type = 0;
					schedule_work(&lost_stat_uid_work.work);
					list_del(&find_list->list);
					kfree(find_list);
					if (list_empty(&sock_info->head)) {
						rb_erase(&sock_info->si.node, &iface_entry->ip4_sock_info_tree);
						kfree(sock_info);
					}
				}
			} else {
				find_list->jiffies = jiffies;
				find_list->lost_jiffies = jiffies;
				reset_counter(&find_list->counters[IFS_TX]);
				reset_counter(&find_list->counters[IFS_RX]);
				find_list->counters[direction].bytes += skb->len;
				find_list->counters[direction].packets++;
			}
		}
	}
unlock:
	spin_unlock(&iface_entry->sock_info_tree_lock);

	return uid;
}

static int __get_ip4_header(const struct sk_buff *skb, __be32 *daddr, __be32 *saddr,
					__be16 *dport, __be16 *sport, u8 *protocol)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct udphdr _hdr, *hp = NULL;

	if (iph->protocol == IPPROTO_UDP || iph->protocol == IPPROTO_TCP) {
		hp = skb_header_pointer(skb, ip_hdrlen(skb),
					sizeof(_hdr), &_hdr);
		if (hp == NULL)
			return -EINVAL;

		*protocol = iph->protocol;
		*saddr = iph->saddr;
		*sport = hp->source;
		*daddr = iph->daddr;
		*dport = hp->dest;

	} else if (iph->protocol == IPPROTO_ICMP) {
		if (extract_icmp4_fields(skb, protocol, saddr, daddr,
					sport, dport))
				return -EINVAL;
	} else {
		return -EINVAL;
	}

	return 0;
}

static uid_t search_sock_info_ip4(const struct sk_buff *skb,
						struct iface_stat *iface_entry, enum ifs_tx_rx direction)
{
	__be32 uninitialized_var(daddr), uninitialized_var(saddr);
	__be16 uninitialized_var(dport), uninitialized_var(sport);
	u8 uninitialized_var(protocol);
	uid_t uid = 0;

	if (__get_ip4_header(skb, &daddr, &saddr, &dport, &sport, &protocol)) {
		return 0;
	}
	if (direction == IFS_TX)
		uid = __search_sock_info_ip4(skb, iface_entry, daddr,
					dport, protocol, direction);
	else
		uid = __search_sock_info_ip4(skb,iface_entry, saddr,
					sport, protocol, direction);

	return uid;
}

static uid_t search_sock_info(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct net_device *el_dev;
	enum ifs_tx_rx direction;
	struct iface_stat *iface_entry;
	uid_t uid = 0;

	if (!lost_stat_params[LS_ENABLE])
		return 0;

	if (!skb->dev) {
		MT_DEBUG("qtaguid[%d]: no skb->dev\n", par->hooknum);
		el_dev = par->in ? : par->out;
	} else {
		const struct net_device *other_dev;
		el_dev = skb->dev;
		other_dev = par->in ? : par->out;
		if (el_dev != other_dev) {
			MT_DEBUG("qtaguid[%d]: skb->dev=%p %s vs "
				"par->(in/out)=%p %s\n",
				par->hooknum, el_dev, el_dev->name, other_dev,
				other_dev->name);
		}
	}
	if (unlikely(!el_dev)) {
		pr_info("qtaguid[%d]: no par->in/out?!!\n", par->hooknum);
		return 0;
	}
	direction = par->in ? IFS_RX : IFS_TX;

	spin_lock_bh(&iface_stat_list_lock);
	iface_entry = get_iface_entry(el_dev->name);
	if (!iface_entry) {
		spin_unlock_bh(&iface_stat_list_lock);
		return 0;
	}
	if (!is_data_network(iface_entry)) {
		spin_unlock_bh(&iface_stat_list_lock);
		return 0;
	}
	spin_unlock_bh(&iface_stat_list_lock);

	switch (par->family) {
	case NFPROTO_IPV6:
		break;
	case NFPROTO_IPV4:
		uid = search_sock_info_ip4(skb, iface_entry, direction);
		break;
	default:
		return 0;
	}

	return uid;
}

static void __sock_info_node_tree_insert(struct sock_hash_info *data,
						struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	while (*new) {
		struct sock_hash_info *this = rb_entry(*new, struct sock_hash_info, node);
		parent = *new;
		if (data->hash > this->hash)
			new = &((*new)->rb_left);
		else if (data->hash < this->hash)
			new = &((*new)->rb_right);
		else
			BUG();
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

static void __sock_info_insert_ip4(struct ip4_sock_info *sock_info,
						struct rb_root *root)
{
	__sock_info_node_tree_insert(&sock_info->si, root);
}

static struct sock_list_info *__init_sock_list(uid_t uid)
{
	struct sock_list_info *list;

	list = kzalloc(sizeof(*list), GFP_ATOMIC);
	if (!list)
		return NULL;

	list->uid = uid;
	list->jiffies = jiffies;
	list->lost_jiffies = 0;

	return list;
}

static int __sock_list_insert_ip4(struct ip4_sock_info *sock_info, uid_t uid)
{
	struct sock_list_info *list, *new_list;
	bool split_uid = true;

	if (list_empty(&sock_info->head)) {
		list = __init_sock_list(uid);
		if (!list)
			return -ENOMEM;
		list_add(&list->list, &sock_info->head);
		return 0;
	}

	list_for_each_entry(list, &sock_info->head, list) {
		if (list->uid == uid) {
			list->jiffies = jiffies;
			split_uid = false;
			break;
		}
	}
	if (split_uid) {
		new_list = __init_sock_list(uid);
		if (!new_list)
			return -ENOMEM;
		list_add(&new_list->list, &sock_info->head);
	}

	return 0;
}

static int __insert_sock_info(struct iface_stat *iface_entry, uid_t uid,
					__be32 addr, __be16 port, u8 protocol)
{
	int hash;
	struct sock_hash_info *info;
	struct ip4_sock_info *sock_info;

	hash = hash_ip4(addr ^ port ^ protocol);

	spin_lock(&iface_entry->sock_info_tree_lock);
	info = __sock_info_node_tree_search(&iface_entry->ip4_sock_info_tree, hash);
	if (!info) {
		sock_info = kzalloc(sizeof(*sock_info), GFP_ATOMIC);
		if (!sock_info) {
			spin_unlock(&iface_entry->sock_info_tree_lock);
			return -ENOMEM;
		}
		sock_info->addr = addr;
		sock_info->port = port;
		sock_info->protocol = protocol;
		sock_info->si.hash = hash;
		INIT_LIST_HEAD(&sock_info->head);

		__sock_info_insert_ip4(sock_info, &iface_entry->ip4_sock_info_tree);
	} else {
		sock_info = rb_entry(&info->node, struct ip4_sock_info, si.node);
	}

	__sock_list_insert_ip4(sock_info, uid);

	sock_info->jiffies = jiffies;

	spin_unlock(&iface_entry->sock_info_tree_lock);

	return 0;
}

static int insert_sock_info_ip4(const struct sk_buff *skb, uid_t uid,
						struct iface_stat *iface_entry, enum ifs_tx_rx direction, char *task_comm)
{
	__be32 uninitialized_var(daddr), uninitialized_var(saddr);
	__be16 uninitialized_var(dport), uninitialized_var(sport);
	u8 uninitialized_var(protocol);

	if (__get_ip4_header(skb, &daddr, &saddr, &dport, &sport, &protocol))
		return 0;

	if (direction == IFS_TX) {
		__insert_sock_info(iface_entry, uid, daddr, dport, protocol);
	}
	else {
		__insert_sock_info(iface_entry, uid, saddr, sport, protocol);
	}

	return 0;
}

static int insert_sock_info(const struct sk_buff *skb,
						struct xt_action_param *par, uid_t uid, char *task_comm)
{
	const struct net_device *el_dev;
	enum ifs_tx_rx direction;
	struct iface_stat *iface_entry;

	if (!lost_stat_params[LS_ENABLE])
		return 0;

	if (!skb->dev) {
		MT_DEBUG("qtaguid[%d]: no skb->dev\n", par->hooknum);
		el_dev = par->in ? : par->out;
	} else {
		const struct net_device *other_dev;
		el_dev = skb->dev;
		other_dev = par->in ? : par->out;
		if (el_dev != other_dev) {
			MT_DEBUG("qtaguid[%d]: skb->dev=%p %s vs "
				"par->(in/out)=%p %s\n",
				par->hooknum, el_dev, el_dev->name, other_dev,
				other_dev->name);
		}
	}
	if (unlikely(!el_dev)) {
		pr_info("qtaguid[%d]: no par->in/out?!!\n", par->hooknum);
		return 0;
	}
	direction = par->in ? IFS_RX : IFS_TX;

	spin_lock_bh(&iface_stat_list_lock);
	iface_entry = get_iface_entry(el_dev->name);
	if (!iface_entry) {
		spin_unlock_bh(&iface_stat_list_lock);
		return 0;
	}
	if (!is_data_network(iface_entry)) {
		spin_unlock_bh(&iface_stat_list_lock);
		return 0;
	}
	spin_unlock_bh(&iface_stat_list_lock);

	switch (par->family) {
	case NFPROTO_IPV6:
		break;
	case NFPROTO_IPV4:
		insert_sock_info_ip4(skb, uid, iface_entry, direction, task_comm);
		break;
	default:
		return 0;
	}

	return 0;
}

static int lost_stat_test_uevent(int uid)
{
	char *lost_stat_param[MAX_EVENT_PARAM] = { "LOST_STAT=LOST", NULL };
	int i, j = 0;

	for(i = 1; i < MAX_EVENT_PARAM - 1; i++) {
		lost_stat_param[i] = kzalloc(100, GFP_KERNEL);
		if (!lost_stat_param[i]) {
			LS_DEBUG("[lost_stat] kzalloc uevent param failed\n");
			goto free_memory;
		}
	}

	sprintf(lost_stat_param[++j], "SIP=%d", 0);
	sprintf(lost_stat_param[++j], "SPORT=%d", 0);
	sprintf(lost_stat_param[++j], "PROTOCOL=%d", 0);
	sprintf(lost_stat_param[++j], "TX=%d", 0);
	sprintf(lost_stat_param[++j], "RX=%d", 0);
	sprintf(lost_stat_param[++j], "UID=%d", uid);
	sprintf(lost_stat_param[++j], "CLASSIFY=%s", "app");
	//lost_stat_param[MAX_EVENT_PARAM - 1] = NULL;
	kobject_uevent_env(qtaguid_module_kobj, KOBJ_CHANGE, lost_stat_param);

free_memory:
	for(i--; i > 0; i--) {
		kfree(lost_stat_param[i]);
	}

	return 0;
}

static void lost_stat_rbtree_clear_all(void)
{
	struct iface_stat *iface_entry;
	struct sock_list_info *list, *tmp;
	struct rb_node *node, *next;
	size_t size = 0;

	printk("zjm: clear up all rbtree func\n");
	spin_lock_bh(&iface_stat_list_lock);
	list_for_each_entry(iface_entry, &iface_stat_list, list) {
		spin_lock(&iface_entry->sock_info_tree_lock);
		//erase all uid rbtree
		for (node = rb_first(&iface_entry->ip4_sock_info_tree); node; node = next) {
			struct sock_hash_info *data = rb_entry(node, struct sock_hash_info, node);
			struct ip4_sock_info *sock_info;
			sock_info = rb_entry(&data->node, struct ip4_sock_info, si.node);

			next = rb_next(node);

			rb_erase(node, &iface_entry->ip4_sock_info_tree);
			list_for_each_entry_safe(list, tmp, &sock_info->head, list) {
				size += sizeof(*list);
				kfree(list);
			}
			size += sizeof(*sock_info);
			kfree(sock_info);
			sock_info = NULL;
		}
		spin_unlock(&iface_entry->sock_info_tree_lock);

		spin_lock(&iface_entry->lost_stat_tree_lock);
		//erase all ip4 lost statistics tree
		for (node = rb_first(&iface_entry->ipv4_lost_stat_tree); node; node = next) {
			struct lost_node *data = rb_entry(node, struct lost_node, node);
			struct ipv4_lost_stat * ip4_lt;
			ip4_lt = rb_entry(&data->node, struct ipv4_lost_stat, ln.node);

			next = rb_next(node);

			LS_DEBUG("[lost_stat] index: %d source: %08X:%04X, des: %08X:%04X, " \
					"tx: %llu, rx: %llu, notag: %d, hash: %d\n",
				ip4_lt->index,
				ntohl(ip4_lt->ipv4hdr.saddr), ntohs(ip4_lt->ipv4hdr.sport),
				ntohl(ip4_lt->ipv4hdr.daddr), ntohs(ip4_lt->ipv4hdr.dport),
				ip4_lt->counters[IFS_TX].bytes, ip4_lt->counters[IFS_RX].bytes,
				ip4_lt->notag_package, data->hash);

			rb_erase(node, &iface_entry->ipv4_lost_stat_tree);
			size += sizeof(*ip4_lt);
			kfree(ip4_lt);
			ip4_lt = NULL;
		}
		//erase all ip6 lost statistics tree
		for (node = rb_first(&iface_entry->ipv6_lost_stat_tree); node; node = next) {
			struct lost_node *data = rb_entry(node, struct lost_node, node);
			struct ipv6_lost_stat * ip6_lt;
			ip6_lt = rb_entry(&data->node, struct ipv6_lost_stat, ln.node);

			next = rb_next(node);

			LS_DEBUG("[lost_stat] index: %d source: %08X%08X%08X%08X:%04X, des: %08X%08X%08X%08X:%04X, " \
				"tx: %llu, rx: %llu, notag: %d, hash: %d\n",
				ip6_lt->index,
				ntohl(ip6_lt->ipv6hdr.saddr.s6_addr32[0]),
				ntohl(ip6_lt->ipv6hdr.saddr.s6_addr32[1]),
				ntohl(ip6_lt->ipv6hdr.saddr.s6_addr32[2]),
				ntohl(ip6_lt->ipv6hdr.saddr.s6_addr32[3]),
				ntohs(ip6_lt->ipv6hdr.sport),
				ntohl(ip6_lt->ipv6hdr.daddr.s6_addr32[0]),
				ntohl(ip6_lt->ipv6hdr.daddr.s6_addr32[1]),
				ntohl(ip6_lt->ipv6hdr.daddr.s6_addr32[2]),
				ntohl(ip6_lt->ipv6hdr.daddr.s6_addr32[3]),
				ntohs(ip6_lt->ipv6hdr.dport),
				ip6_lt->counters[IFS_TX].bytes, ip6_lt->counters[IFS_RX].bytes,
				ip6_lt->notag_package, data->hash);

			rb_erase(node, &iface_entry->ipv6_lost_stat_tree);
			size += sizeof(*ip6_lt);
			kfree(ip6_lt);
			ip6_lt = NULL;
		}
		spin_unlock(&iface_entry->lost_stat_tree_lock);
	}
	spin_unlock_bh(&iface_stat_list_lock);
	printk("zjm: clear rbtree free %zd bytes memory\n", size);
}

static int lost_stat_ctrl_parse(const char *input)
{
	char cmd;
	int argc, index;
	long value;

	cmd = input[0];
	switch (cmd) {
	case 'e':
		// input is "l e %d"
		index = 0;
		argc = sscanf(input, "%c %d", &cmd, &index);
		if (argc != 2)
			return -EINVAL;
		lost_stat_test_uevent(index);
		break;
	case 'p':
		// input is "l p %d %ld"
		argc = sscanf(input, "%c %d %ld", &cmd, &index, &value);
		if (argc != 3)
			return -EINVAL;
		if (index >= LS_MAX)
			return -EINVAL;
		printk("zjm: lost_stat params %d, %ld\n", index, value);
		lost_stat_params[index] = value;
		break;
	case 'c':
		printk("zjm: clear up all rbtree\n");
		lost_stat_rbtree_clear_all(); // input is "l c"
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void __init init_lost_stat_params(void)
{
	lost_stat_params[LS_ENABLE] = 1;
	lost_stat_params[LS_CLOSE_ENABLE] = 1;
	lost_stat_params[LS_TIMEOUT] = LOST_STAT_TIMEOUT;
	lost_stat_params[LS_REPORT_THRESHOLD] = LOST_STAT_REPORT_THRESHOLD;
	lost_stat_params[LS_UPLOAD_LOG] = 0;
	lost_stat_params[LS_UPLOAD_LOG_THRESHOLD] = LOST_STAT_TIMEOUT;
	lost_stat_params[LS_WIFI_ENABLE] = 0;
}

static int __init init_lost_stat(void)
{
	init_lost_stat_params();

	notag_ip6.s6_addr32[0] = NOTAG_PACKAGE_SADDR;
	notag_ip6.s6_addr32[1] = NOTAG_PACKAGE_SADDR;
	notag_ip6.s6_addr32[2] = NOTAG_PACKAGE_SADDR;
	notag_ip6.s6_addr32[3] = NOTAG_PACKAGE_SADDR;

	qtaguid_module_kobj = kset_find_obj(module_kset, KBUILD_MODNAME);
	if (qtaguid_module_kobj == NULL) {
		return -1;
	}
	pr_info("[lost_stat] kernel obj name %s\n", qtaguid_module_kobj->name);
	lost_stat_work.type = 0;
	INIT_WORK(&lost_stat_work.work, lost_stat_work_func);
	lost_stat_uid_work.type = 0;
	INIT_WORK(&lost_stat_uid_work.work, lost_stat_uid_work_func);

	return 0;
}

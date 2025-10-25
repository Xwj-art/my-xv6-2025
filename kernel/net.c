#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

#define MAX_QUEUE_SIZE 16
#define MAX_BOUND_PORTS 1 << 16

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x02};

static struct spinlock netlock;

static struct {
  uint8 ports[MAX_BOUND_PORTS];
  struct spinlock portlock;
} portmanager;

struct retinfo {
  int src;
  short sport;
  char* data;
  uint len;
};

struct port_queue {
  // 数据报队列
  struct retinfo retinfos[MAX_QUEUE_SIZE];
  // 记录第一个数据
  int head;
  // 记录数据后第一个空位
  int tail;
  // 记录总数据报数目
  int count;
  struct spinlock queuelock;
};

static struct {
  struct port_queue port_manager[MAX_BOUND_PORTS];
  struct spinlock managerlock;
} queuemanager;

void netinit(void) {
  initlock(&portmanager.portlock, "portlock");
  initlock(&queuemanager.managerlock, "managerlock");
  initlock(&netlock, "netlock");
  // 初始化端口号
  for (int i = 0; i < MAX_BOUND_PORTS; i++) {
    portmanager.ports[i] = 0;
  }
}

//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64 sys_bind(void) {
  int port;
  argint(0, &port);

  acquire(&portmanager.portlock);

  if (portmanager.ports[port]) {
    // 已被绑定了
    release(&portmanager.portlock);
    return -1;
  }
  // 若数目大于15，装满了，装不下
  if (queuemanager.port_manager[port].count > 15) {
    // 满了
    release(&portmanager.portlock);
    return -1;
  }

  // 绑定端口
  portmanager.ports[port] = 1;
  release(&portmanager.portlock);

  // 初始化对应的队列
  acquire(&queuemanager.managerlock);
  struct port_queue* queue = &queuemanager.port_manager[port];
  queue->head = 0;
  queue->tail = 0;
  queue->count = 0;
  initlock(&queue->queuelock, "queuelock");
  release(&queuemanager.managerlock);

  return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64 sys_unbind(void) {
  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64 sys_recv(void) {
  // recv(int dport, int *src, short *sport, char *buf, int maxlen)
  struct proc* p = myproc();

  int dport;
  uint64 src;
  uint64 sport;
  uint64 buf;
  int maxlen;

  argint(0, &dport);
  argaddr(1, &src);
  argaddr(2, &sport);
  argaddr(3, &buf);
  argint(4, &maxlen);

  // 若端口未绑定，会一直无数据
  acquire(&portmanager.portlock);
  if (!portmanager.ports[dport]) {
    release(&portmanager.portlock);
    return -1;
  }
  release(&portmanager.portlock);

  // 等待指定端口传来数据
  struct port_queue* queue = &queuemanager.port_manager[dport];

  acquire(&queue->queuelock);
  while (queue->count == 0) {
    sleep(queue, &queue->queuelock);
  }

  uint head = queue->head;
  int sz = queue->retinfos[head].len;

  // 数据拷贝到用户空间
  copyout(p->pagetable, src, (char*)&queue->retinfos[head].src,
          sizeof((queue->retinfos[head]).src));
  copyout(p->pagetable, sport, (char*)&queue->retinfos[head].sport,
          sizeof((queue->retinfos[head]).sport));
  copyout(p->pagetable, buf, queue->retinfos[head].data, sz);

  // 设置queue
  // 释放内存
  kfree((void*)queue->retinfos[head].data);
  queue->head = (head + 1) % MAX_QUEUE_SIZE;
  queue->count--;

  release(&(queue->queuelock));

  return sz;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short in_cksum(const unsigned char* addr, int len) {
  int nleft = len;
  const unsigned short* w = (const unsigned short*)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1) {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char*)(&answer) = *(const unsigned char*)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64 sys_send(void) {
  struct proc* p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if (total > PGSIZE) return -1;

  pt("sys_send: send buf kalloc");
  char* buf = kalloc();
  if (buf == 0) {
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth* eth = (struct eth*)buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip* ip = (struct ip*)(eth + 1);
  ip->ip_vhl = 0x45;  // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char*)ip, sizeof(*ip));

  struct udp* udp = (struct udp*)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char* payload = (char*)(udp + 1);
  if (copyin(p->pagetable, payload, bufaddr, len) < 0) {
    pt("sys_send: send buf free");
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

int save_queue(struct retinfo info, uint16 port) {
  // 先判断能不能放
  struct port_queue* queue = &queuemanager.port_manager[port];
  acquire(&queue->queuelock);
  if (queue->count < 16) {
    // 如果能放
    queue->retinfos[queue->tail].src = info.src;
    queue->retinfos[queue->tail].sport = info.sport;
    queue->retinfos[queue->tail].data = kalloc();
    memset(queue->retinfos[queue->tail].data, 0, PGSIZE);
    memmove((void*)queue->retinfos[queue->tail].data, (void*)info.data, info.len);
    queue->retinfos[queue->tail].len = info.len;
    queue->tail = (queue->tail + 1) % MAX_QUEUE_SIZE;
    (queue->count)++;
    wakeup(queue);
    release(&queue->queuelock);
  } else {
    // 如果不能放，丢弃分组，返回-1
    // 怎么丢弃分组，这个分组的数据空间是否需要我来释放？需要我来释放buf
    release(&queue->queuelock);
    return -1;
  }
  return 0;
}

void ip_rx(char* buf, int len) {
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if (seen_ip == 0) printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  struct retinfo info;
  struct eth* eth = (struct eth*)buf;
  struct ip* ip = (struct ip*)(eth + 1);
  info.src = ntohl(ip->ip_src);
  if (ip->ip_p == IPPROTO_UDP) {
    // udp
    struct udp* udp = (struct udp*)(ip + 1);
    info.sport = ntohs(udp->sport);
    uint16 dport = ntohs(udp->dport);
    if (portmanager.ports[dport]) {
      // 已经bind端口
      // 负载
      info.data = (char*)(udp + 1);
      info.len = ntohs(udp->ulen) - sizeof(struct udp);
      // 保存到相应的队列
      if (save_queue(info, dport) < 0) {
        pt("fail");
      }
      pt("ip_rx: recv buf free");
      kfree(buf);
    } else {
      // 未绑定端口号
      pt("ip_rx: recv buf free");
      kfree(buf);
    }
  }
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void arp_rx(char* inbuf) {
  static int seen_arp = 0;

  if (seen_arp) {
    pt("arp_rx: free inbuf");
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth* ineth = (struct eth*)inbuf;
  struct arp* inarp = (struct arp*)(ineth + 1);

  pt("arp_rx: alloc");
  char* buf = kalloc();
  if (buf == 0) panic("send_arp_reply");

  struct eth* eth = (struct eth*)buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN);  // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN);     // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp* arp = (struct arp*)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  pt("arp_rx: free inbuf");
  kfree(inbuf);
}

void net_rx(char* buf, int len) {
  struct eth* eth = (struct eth*)buf;

  if (len >= sizeof(struct eth) + sizeof(struct arp) && ntohs(eth->type) == ETHTYPE_ARP) {
    arp_rx(buf);
  } else if (len >= sizeof(struct eth) + sizeof(struct ip) && ntohs(eth->type) == ETHTYPE_IP) {
    ip_rx(buf, len);
  } else {
    pt("net_rx: free buf");
    kfree(buf);
  }
}

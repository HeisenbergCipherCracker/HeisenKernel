#ifndef __ALINIX_KERNEL_NET_UDP_H
#define __ALINIX_KERNEL_NET_UDP_H

#include <alinix/types.h>
#include <alinix/compiler.h>
#include <net/ip_addr.h>
#include <net/ip.h>
#include <net/err.h>


#ifdef __cplusplus
extern "C"{
#endif


#define UPD_HELEN 8
struct udp_hdr {
  PACK_STRUCT_FIELD(uint16_t src);
  PACK_STRUCT_FIELD(uint16_t dest);  /* src/dest UDP ports */
  PACK_STRUCT_FIELD(uint16_t len);
  PACK_STRUCT_FIELD(uint16_t chksum);
} PACK_STRUCT_STRUCT;


#define UDP_FLAGS_NOCHKSUM       0x01U
#define UDP_FLAGS_UDPLITE        0x02U
#define UDP_FLAGS_CONNECTED      0x04U
#define UDP_FLAGS_MULTICAST_LOOP 0x08U

struct udp_pcb;


typedef void (*udp_recv_fn)(void *arg, struct udp_pcb *pcb, struct pbuf *p,
    ip_addr_t *addr, uint16_t port);


struct udp_pcb {
/* Common members of all PCB types */
  IP_PCB;

/* Protocol specific PCB members */

  struct udp_pcb *next;

  uint8_t flags;
  /** ports are in host byte order */
  uint16_t local_port, remote_port;

#if LWIP_IGMP
  /** outgoing network interface for multicast packets */
  ip_addr_t multicast_ip;
#endif /* LWIP_IGMP */

#if LWIP_UDPLITE
  /** used for UDP_LITE only */
  uint16_t chksum_len_rx, chksum_len_tx;
#endif /* LWIP_UDPLITE */

  /** receive callback function */
  udp_recv_fn recv;
  /** user-supplied argument for the recv callback */
  void *recv_arg;  
};

extern struct udp_pcb *udp_pcbs;


void             udp_remove     (struct udp_pcb *pcb);
err_t            udp_bind       (struct udp_pcb *pcb, ip_addr_t *ipaddr,
                                 uint16_t port);
err_t            udp_connect    (struct udp_pcb *pcb, ip_addr_t *ipaddr,
                                 uint16_t port);
void             udp_disconnect (struct udp_pcb *pcb);
void             udp_recv       (struct udp_pcb *pcb, udp_recv_fn recv,
                                 void *recv_arg);
err_t            udp_sendto_if  (struct udp_pcb *pcb, struct pbuf *p,
                                 ip_addr_t *dst_ip, uint16_t dst_port,
                                 struct netif *netif);
err_t            udp_sendto     (struct udp_pcb *pcb, struct pbuf *p,
                                 ip_addr_t *dst_ip, uint16_t dst_port);
err_t            udp_send       (struct udp_pcb *pcb, struct pbuf *p);




#if LWIP_CHECKSUM_ON_COPY
err_t            udp_sendto_if_chksum(struct udp_pcb *pcb, struct pbuf *p,
                                 ip_addr_t *dst_ip, u16_t dst_port,
                                 struct netif *netif, uint8_t have_chksum,
                                 u16_t chksum);
err_t            udp_sendto_chksum(struct udp_pcb *pcb, struct pbuf *p,
                                 ip_addr_t *dst_ip, u16_t dst_port,
                                 uint8_t have_chksum, u16_t chksum);
err_t            udp_send_chksum(struct udp_pcb *pcb, struct pbuf *p,
                                 uint8_t have_chksum, u16_t chksum);
#endif /* LWIP_CHECKSUM_ON_COPY */

#define          udp_flags(pcb) ((pcb)->flags)
#define          udp_setflags(pcb, f)  ((pcb)->flags = (f))

/* The following functions are the lower layer interface to UDP. */
void             udp_input      (struct pbuf *p, struct netif *inp);

void             udp_init       (void);

#if UDP_DEBUG
void udp_debug_print(struct udp_hdr *udphdr);
#else
#define udp_debug_print(udphdr)
#endif




#ifdef __cplusplus
}
#endif

#endif /*__ALINIX_KERNEL_NET_UDP_H*/
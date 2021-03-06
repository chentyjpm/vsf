#define VSFIP_CFG_ARPCACHE_SIZE				8
#define VSFIP_CFG_ARP_TIMEOUT_MS			1000
#define VSFIP_CFG_TTL_INPUT					10
#define VSFIP_CFG_UDP_PORT					40000
#define VSFIP_CFG_TCP_PORT					40000

#define VSFIP_CFG_TCP_RX_WINDOW				4500
#define VSFIP_CFG_TCP_TX_WINDOW				3000

#define VSFIP_CFG_NETIF_HEADLEN				64
#define VSFIP_CFG_HOSTNAME					"vsfip"

#define VSFIP_CFG_MTU						1500
// NETIF_HEAD + 1500(MTU)
#define VSFIP_BUFFER_SIZE					(VSFIP_CFG_MTU + VSFIP_CFG_NETIF_HEADLEN)
// 1500(MTU) - 20(TCP_HEAD) - 20(IP_HEAD)
#define VSFIP_CFG_TCP_MSS					(VSFIP_CFG_MTU - 40)

// dhcpd
#define VSFIP_CFG_DHCPD_ASSOCNUM			2
#define VSFIP_CFG_DOMAIN					"vsfip.net"
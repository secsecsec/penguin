#ifndef __NET_IP_H__
#define __NET_IP_H__

#define IP_LEN			20

#define IP_PROTOCOL_ICMP	0x01
#define IP_PROTOCOL_UDP		0x11
#define IP_PROTOCOL_TCP		0x06

typedef struct {
	uint8_t		ihl: 4;
	uint8_t		version: 4;
	uint8_t		ecn: 2;
	uint8_t		dscp: 6;
	uint16_t	length;
	uint16_t	id;
	uint16_t	flags_offset;
	uint8_t		ttl;
	uint8_t		protocol;
	uint16_t	checksum;
	uint32_t	source;
	uint32_t	destination;
	
	uint8_t		body[0];
} __attribute__ ((packed)) IP;

#endif /* __NET_IP_H__ */

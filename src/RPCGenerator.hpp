#ifndef __RPC_GENERATOR
#define __RPC_GENERATOR
#include <cstddef>
#include <stdint.h>
#include "city.h"
#include <random>

#define MICA_OP_GET 111
#define MICA_OP_PUT 112
#define HERD_MICA_OFFSET 10
#define HERD_OP_PUT (MICA_OP_PUT + HERD_MICA_OFFSET)
#define HERD_OP_GET (MICA_OP_GET + HERD_MICA_OFFSET)
#define HERD_VALUE_SIZE 32
#define MICA_MAX_VALUE \
	(512 - (sizeof(struct mica_key) + sizeof(uint16_t) + sizeof(uint16_t)))


#define UNIFORM_DIST 0
#define ZIPF_DIST 1



#define XMM_NUM_IN_IPV6_5TUPLE 3
#define IPV6_ADDR_LEN 16
struct ipv6_5tuple {
	uint8_t  ip_dst[IPV6_ADDR_LEN];
	uint8_t  ip_src[IPV6_ADDR_LEN];
	uint16_t port_dst;
	uint16_t port_src;
	uint8_t  proto;
//} __rte_packed;
}__attribute__((__packed__));

typedef __m128i xmm_t;
#define	XMM_SIZE	(sizeof(xmm_t))
#define	XMM_MASK	(XMM_SIZE - 1)

typedef union rte_xmm {
	xmm_t    x;
	uint8_t  u8[XMM_SIZE / sizeof(uint8_t)];
	uint16_t u16[XMM_SIZE / sizeof(uint16_t)];
	uint32_t u32[XMM_SIZE / sizeof(uint32_t)];
	uint64_t u64[XMM_SIZE / sizeof(uint64_t)];
	double   pd[XMM_SIZE / sizeof(double)];
} rte_xmm_t;

union ipv6_5tuple_host {
	struct {
		uint16_t pad0;
		uint8_t  proto;
		uint8_t  pad1;
		uint8_t  ip_src[IPV6_ADDR_LEN];
		uint8_t  ip_dst[IPV6_ADDR_LEN];
		uint16_t port_src;
		uint16_t port_dst;
		uint64_t reserve;
	};
	xmm_t xmm[XMM_NUM_IN_IPV6_5TUPLE];
};


struct ipv6_l3fwd_em_route {
	struct ipv6_5tuple key;
	uint8_t if_out;
};

#define IPPROTO_UDP 17

// static const struct ipv6_l3fwd_em_route ipv6_l3fwd_em_route_array[] = {
// 	{{{32, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 0},
// 	{{{32, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 1},
// 	{{{32, 1, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 2},
// 	{{{32, 1, 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 3},
// 	{{{32, 1, 2, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 4},
// 	{{{32, 1, 2, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 5},
// 	{{{32, 1, 2, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 6},
// 	{{{32, 1, 2, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 7},
// 	{{{32, 1, 2, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 8},
// 	{{{32, 1, 2, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 9},
// 	{{{32, 1, 2, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 10},
// 	{{{32, 1, 2, 0, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 11},
// 	{{{32, 1, 2, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 12},
// 	{{{32, 1, 2, 0, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 13},
// 	{{{32, 1, 2, 0, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 14},
// 	{{{32, 1, 2, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0, 0, 0, 0},
// 		 {32, 1, 2, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 15},
// };


static const struct ipv6_l3fwd_em_route ipv6_l3fwd_em_route_array[] = {
	{{{32, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 0},
	{{{32, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 1},
	{{{32, 1, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 2},
	{{{32, 1, 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 3},
	{{{32, 1, 2, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 4},
	{{{32, 1, 2, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 5},
	{{{32, 1, 2, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 6},
	{{{32, 1, 2, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 7},
	{{{32, 1, 2, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 8},
	{{{32, 1, 2, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 9},
	{{{32, 1, 2, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 10},
	{{{32, 1, 2, 0, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 11},
	{{{32, 1, 2, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 12},
	{{{32, 1, 2, 0, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 13},
	{{{32, 1, 2, 0, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 14},
	{{{32, 1, 2, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 15},
	
	{{{32, 1, 2, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 16},
	{{{32, 1, 2, 0, 0, 0, 0, 17, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 17, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 17},
	{{{32, 1, 2, 0, 0, 0, 0, 18, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 18, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 18},
	{{{32, 1, 2, 0, 0, 0, 0, 19, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 19, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 19},
	{{{32, 1, 2, 0, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 20},
	{{{32, 1, 2, 0, 0, 0, 0, 21, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 21, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 21},
	{{{32, 1, 2, 0, 0, 0, 0, 22, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 22, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 22},
	{{{32, 1, 2, 0, 0, 0, 0, 23, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 23, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 23},
	{{{32, 1, 2, 0, 0, 0, 0, 24, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 24, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 24},
	{{{32, 1, 2, 0, 0, 0, 0, 25, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 25, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 25},
	{{{32, 1, 2, 0, 0, 0, 0, 26, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 26, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 26},
	{{{32, 1, 2, 0, 0, 0, 0, 27, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 27, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 27},
	{{{32, 1, 2, 0, 0, 0, 0, 28, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 28, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 28},
	{{{32, 1, 2, 0, 0, 0, 0, 29, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 29, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 29},
	
	{{{32, 1, 2, 0, 0, 0, 0, 30, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 30, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 30},
	{{{32, 1, 2, 0, 0, 0, 0, 31, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 31, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 31},
	{{{32, 1, 2, 0, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 32},
	{{{32, 1, 2, 0, 0, 0, 0, 33, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 33, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 33},
	{{{32, 1, 2, 0, 0, 0, 0, 34, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 34, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 34},
	{{{32, 1, 2, 0, 0, 0, 0, 35, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 35, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 35},
	{{{32, 1, 2, 0, 0, 0, 0, 36, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 36, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 36},
	{{{32, 1, 2, 0, 0, 0, 0, 37, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 37, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 37},
	{{{32, 1, 2, 0, 0, 0, 0, 38, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 38, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 38},
	{{{32, 1, 2, 0, 0, 0, 0, 39, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 39, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 39},
	
	{{{32, 1, 2, 0, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 40},
	{{{32, 1, 2, 0, 0, 0, 0, 41, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 41, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 41},
	{{{32, 1, 2, 0, 0, 0, 0, 42, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 42, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 42},
	{{{32, 1, 2, 0, 0, 0, 0, 43, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 43, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 43},
	{{{32, 1, 2, 0, 0, 0, 0, 44, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 44, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 44},
	{{{32, 1, 2, 0, 0, 0, 0, 45, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 45, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 45},
	{{{32, 1, 2, 0, 0, 0, 0, 46, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 46, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 46},
	{{{32, 1, 2, 0, 0, 0, 0, 47, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 47, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 47},
	{{{32, 1, 2, 0, 0, 0, 0, 48, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 48, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 48},
	{{{32, 1, 2, 0, 0, 0, 0, 49, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 49, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 49},
	
	{{{32, 1, 2, 0, 0, 0, 0, 50, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 50, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 50},
	{{{32, 1, 2, 0, 0, 0, 0, 51, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 51, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 51},
	{{{32, 1, 2, 0, 0, 0, 0, 52, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 52, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 52},
	{{{32, 1, 2, 0, 0, 0, 0, 53, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 53, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 53},
	{{{32, 1, 2, 0, 0, 0, 0, 54, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 54, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 54},
	{{{32, 1, 2, 0, 0, 0, 0, 55, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 55, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 55},
	{{{32, 1, 2, 0, 0, 0, 0, 56, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 56, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 56},
	{{{32, 1, 2, 0, 0, 0, 0, 57, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 57, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 57},
	{{{32, 1, 2, 0, 0, 0, 0, 58, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 58, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 58},
	{{{32, 1, 2, 0, 0, 0, 0, 59, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 59, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 59},
	{{{32, 1, 2, 0, 0, 0, 0, 60, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 60, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 60},
	{{{32, 1, 2, 0, 0, 0, 0, 61, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 61, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 61},
	{{{32, 1, 2, 0, 0, 0, 0, 62, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 62, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 62},
	{{{32, 1, 2, 0, 0, 0, 0, 63, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 63, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 63},

};


class RPCGenerator {
	private:
		uint64_t srand_seed;
		size_t num_keys, update_fraction;
		int* key_arr;
		long double zipf_s=0.99; 
		long double* zcf; //cumulative frequency table for zipf
		uint32_t load_dist_type=0;
		uint32_t lg_type = 0;


	public:
		RPCGenerator(size_t num_keys, size_t update_fraction);
		virtual int generatePackedRPC_batch(char* userBuffer, uint32_t numreqs) const;
		virtual int generatePackedRPC(char* userBuffer, uint32_t packet_size) const ;
		virtual uint32_t getRPCPayloadSize() const ;
		void set_num_keys(size_t i_num_keys);// { num_keys = i_num_keys; }
void set_load_dist(uint32_t dist_type) { load_dist_type = dist_type;}
void set_update_fraction(size_t i_update_fraction) { update_fraction = i_update_fraction; }

//debug functions
size_t get_num_keys() { return num_keys; }
int* get_key_arr() { return key_arr; }
void set_lg_type(uint32_t lgt){lg_type = lgt;}
};


/* Fixed-size 16 byte keys */
struct mica_key {
	unsigned long long __unused : 64;
	unsigned int bkt : 32;
	unsigned int server : 16;
	unsigned int tag : 16;
};

struct mica_op {
	struct mica_key key; /* This must be the 1st field and 16B aligned */
	uint16_t opcode;
	uint16_t val_len;
	uint8_t value[MICA_MAX_VALUE];
};

#endif //#ifndef __RPC_GENERATOR

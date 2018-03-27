/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004, Andreas T�nnesen(andreto@olsr.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 *
 * * Redistributions of source code must retain the above copyright 
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright 
 *   notice, this list of conditions and the following disclaimer in 
 *   the documentation and/or other materials provided with the 
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its 
 *   contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 * $Id: routing_table.h,v 1.17 2005/11/16 23:55:54 tlopatic Exp $
 */

#ifndef _OLSR_ROUTING_TABLE
#define _OLSR_ROUTING_TABLE

#include <net/route.h>
#include "hna_set.h"

#define NETMASK_HOST 0xffffffff
#define NETMASK_DEFAULT 0x0

struct rt_entry
{
  union olsr_ip_addr    rt_dst;
  union olsr_ip_addr    rt_router;
  union hna_netmask     rt_mask;
  olsr_u8_t  	        rt_flags; 
  olsr_u16_t 	        rt_metric;
  float                 rt_etx;
  struct interface      *rt_if;
  struct rt_entry       *prev;
  struct rt_entry       *next;
};


struct destination_n
{
  struct rt_entry      *destination;
  struct destination_n *next;
};


/**
 * IPv4 <-> IPv6 wrapper
 */
union olsr_kernel_route
{
  struct
  {
    struct sockaddr rt_dst;
    struct sockaddr rt_gateway;
    olsr_u32_t rt_metric;
  } v4;

  struct
  {
    struct in6_addr rtmsg_dst;
    struct in6_addr rtmsg_gateway;
    olsr_u32_t rtmsg_metric;
  } v6;
};


extern struct rt_entry routingtable[HASHSIZE];
extern struct rt_entry hna_routes[HASHSIZE];


int
olsr_init_routing_table(void);

void 
olsr_calculate_routing_table(void);

void
olsr_calculate_hna_routes(void);

void
olsr_print_routing_table(struct rt_entry *);

struct rt_entry *
olsr_insert_routing_table(union olsr_ip_addr *, union olsr_ip_addr *, struct interface *, int, float);

struct rt_entry *
olsr_lookup_routing_table(union olsr_ip_addr *);

struct rt_entry *
olsr_lookup_hna_routing_table(union olsr_ip_addr *dst);

void
olsr_free_routing_table(struct rt_entry *);

#endif

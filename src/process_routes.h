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
 * $Id: process_routes.h,v 1.10 2007/01/31 12:36:50 bernd67 Exp $
 */

#include "routing_table.h"
 

#ifndef _OLSR_PROCESS_RT
#define _OLSR_PROCESS_RT

#include <sys/ioctl.h>

extern struct rt_entry old_routes[HASHSIZE];
extern struct rt_entry old_hna[HASHSIZE];

void
olsr_init_export_route(void);

void
olsr_addroute_add_function(int (*)(struct rt_entry*), olsr_u8_t);

int
olsr_addroute_remove_function(int (*)(struct rt_entry*), olsr_u8_t);

void
olsr_delroute_add_function(int (*)(struct rt_entry*), olsr_u8_t);

int
olsr_delroute_remove_function(int (*)(struct rt_entry*), olsr_u8_t);

int
olsr_export_add_route (struct rt_entry*); 

int
olsr_export_del_route (struct rt_entry*); 

int
olsr_export_add_route6 (struct rt_entry*); 

int
olsr_export_del_route6 (struct rt_entry*); 


int
olsr_init_old_table(void);

int
olsr_find_up_route(struct rt_entry *dst,struct rt_entry *table);

struct destination_n *
olsr_build_update_list(struct rt_entry *from_table, struct rt_entry *in_table);

void
olsr_update_kernel_routes(void);

void
olsr_update_kernel_hna_routes(void);

void
olsr_move_route_table(struct rt_entry *, struct rt_entry *);

void
olsr_delete_routes_from_kernel(struct destination_n *delete_kernel_list);

void
olsr_add_routes_in_kernel(struct destination_n *add_kernel_list);

int
olsr_delete_all_kernel_routes(void);

#endif

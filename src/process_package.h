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
 * $Id: process_package.h,v 1.12 2005/02/20 18:52:18 kattemat Exp $
 */


#ifndef _OLSR_PROCESS_PACKAGE
#define _OLSR_PROCESS_PACKAGE

#include "olsr_protocol.h"
#include "packet.h"
#include "neighbor_table.h"

void
olsr_init_package_process(void);

void
olsr_hello_tap(struct hello_message *, struct interface *, union olsr_ip_addr *);

void
olsr_process_received_hello(union olsr_message *, struct interface *, union olsr_ip_addr *);

void
olsr_tc_tap(struct tc_message *, struct interface *, union olsr_ip_addr *, union olsr_message *);

void
olsr_process_received_tc(union olsr_message *, struct interface *, union olsr_ip_addr *);

void
olsr_process_received_mid(union olsr_message *, struct interface *, union olsr_ip_addr *);

void
olsr_process_received_hna(union olsr_message *, struct interface *, union olsr_ip_addr *);

void
olsr_process_message_neighbors(struct neighbor_entry *,struct hello_message *);

void
olsr_linking_this_2_entries(struct neighbor_entry *,struct neighbor_2_entry *, float);

int
olsr_lookup_mpr_status(struct hello_message *, struct interface *);

#endif

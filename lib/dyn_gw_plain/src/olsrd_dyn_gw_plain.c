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
 */

#include "olsr_types.h"
#include "olsrd_dyn_gw_plain.h"
#include "scheduler.h"
#include "olsr.h"
#include "local_hna_set.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <net/route.h>
#include <unistd.h>
#include <errno.h>

#define DEBUGLEV 1

#define PLUGIN_INTERFACE_VERSION 4

static int has_inet_gateway;

/**
 * Plugin interface version
 * Used by main olsrd to check plugin interface version
 */
int 
olsrd_plugin_interface_version(void)
{
  return PLUGIN_INTERFACE_VERSION;
}

/**
 * Register parameters from config file
 * Called for all plugin parameters
 */
int
olsrd_plugin_register_param(char *key __attribute__((unused)), char *value __attribute__((unused)))
{
  return 1;
}

/**
 * Initialize plugin
 * Called after all parameters are passed
 */
int
olsrd_plugin_init(void)
{
  gw_net.v4 = INET_NET;
  gw_netmask.v4 = INET_PREFIX;

  has_inet_gateway = 0;
  
  /* Remove all local Inet HNA entries */
  while(remove_local_hna4_entry(&gw_net, &gw_netmask)) {
    olsr_printf(DEBUGLEV, "HNA Internet gateway deleted\n");
  }

  /* Register the GW check */
  olsr_register_scheduler_event(&olsr_event, NULL, 3, 4, NULL);

  return 1;
}

int
check_gw(union olsr_ip_addr *net, union olsr_ip_addr *mask)
{
    char buff[1024], iface[16];
    olsr_u32_t gate_addr, dest_addr, netmask;
    unsigned int iflags;
    int num, metric, refcnt, use;
    int retval = 0;

    FILE *fp = fopen(PROCENTRY_ROUTE, "r");

    if (!fp) 
      {
        perror(PROCENTRY_ROUTE);
        olsr_printf(DEBUGLEV, "INET (IPv4) not configured in this system.\n");
        return -1;
      }
    
    rewind(fp);

    /*
    olsr_printf(DEBUGLEV, "Genmask         Destination     Gateway         "
                "Flags Metric Ref    Use Iface\n");
    */
    while (fgets(buff, 1023, fp)) 
      {	
	num = sscanf(buff, "%16s %128X %128X %X %d %d %d %128X \n",
		     iface, &dest_addr, &gate_addr,
		     &iflags, &refcnt, &use, &metric, &netmask);

	if (num < 8)
	  {
	    continue;
	  }

	/*
	olsr_printf(DEBUGLEV, "%-15s ", olsr_ip_to_string((union olsr_ip_addr *)&netmask));

	olsr_printf(DEBUGLEV, "%-15s ", olsr_ip_to_string((union olsr_ip_addr *)&dest_addr));

	olsr_printf(DEBUGLEV, "%-15s %-6d %-2d %7d %s\n",
		    olsr_ip_to_string((union olsr_ip_addr *)&gate_addr),
		    metric, refcnt, use, iface);
	*/

	if(//(iflags & RTF_GATEWAY) &&
	   (iflags & RTF_UP) &&
	   (metric == 0) &&
	   (netmask == mask->v4) && 
	   (dest_addr == net->v4))
	  {
            olsr_printf(DEBUGLEV, "INTERNET GATEWAY VIA %s detected in routing table.\n", iface);
            retval=1;
	  }

    }

    fclose(fp);  
  
    if(retval == 0)
      {
	olsr_printf(DEBUGLEV, "No Internet GWs detected...\n");
      }
  
    return retval;
}

/**
 * Scheduled event to update the hna table,
 * called from olsrd main thread to keep the hna table thread-safe
 */
void olsr_event(void* foo __attribute__((unused)))
{
  int res = check_gw(&gw_net, &gw_netmask);
  if (1 == res && 0 == has_inet_gateway) {
    olsr_printf(DEBUGLEV, "Adding OLSR local HNA entry for Internet\n");
    add_local_hna4_entry(&gw_net, &gw_netmask);
    has_inet_gateway = 1;
  }
  else if (0 == res && 1 == has_inet_gateway) {
    /* Remove all local Inet HNA entries */
    while(remove_local_hna4_entry(&gw_net, &gw_netmask)) {
      olsr_printf(DEBUGLEV, "Removing OLSR local HNA entry for Internet\n");
    }
    has_inet_gateway = 0;
  }
}

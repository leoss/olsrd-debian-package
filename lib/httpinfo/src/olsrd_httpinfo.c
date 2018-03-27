/*
 * HTTP Info plugin for the olsr.org OLSR daemon
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
 * $Id: olsrd_httpinfo.c,v 1.80 2007/10/14 22:46:03 bernd67 Exp $
 */

/*
 * Dynamic linked library for the olsr.org olsr daemon
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#ifdef WIN32
#include <io.h>
#else
#include <netdb.h>
#endif

#include "olsr.h"
#include "olsr_cfg.h"
#include "interfaces.h"
#include "olsr_protocol.h"
#include "net_olsr.h"
#include "link_set.h"
#include "socket_parser.h"

#include "olsrd_httpinfo.h"
#include "admin_interface.h"
#include "html.h"
#include "gfx.h"

#ifdef OS
#undef OS
#endif

#ifdef WIN32
#define close(x) closesocket(x)
#define OS "Windows"
#endif
#ifdef linux
#define OS "GNU/Linux"
#endif
#ifdef __FreeBSD__
#define OS "FreeBSD"
#endif

#ifndef OS
#define OS "Undefined"
#endif

static char copyright_string[] __attribute__((unused)) = "olsr.org HTTPINFO plugin Copyright (c) 2004, Andreas T�nnesen(andreto@olsr.org) All rights reserved.";

#define MAX_CLIENTS 3

#define MAX_HTTPREQ_SIZE (1024 * 10)

#define DEFAULT_TCP_PORT 1978

#define HTML_BUFSIZE (1024 * 4000)

#define FRAMEWIDTH 800

#define FILENREQ_MATCH(req, filename) \
        !strcmp(req, filename) || \
        (strlen(req) && !strcmp(&req[1], filename))

struct tab_entry
{
  char *tab_label;
  char *filename;
  int(*build_body_cb)(char *, olsr_u32_t);
  olsr_bool display_tab;
};

struct static_bin_file_entry
{
  char *filename;
  unsigned char *data;
  unsigned int data_size;
};

struct static_txt_file_entry
{
  char *filename;
  const char **data;
};

struct dynamic_file_entry
{
  char *filename;
  int(*process_data_cb)(char *, olsr_u32_t, char *, olsr_u32_t);
};

static int get_http_socket(int);

static int build_tabs(char *, olsr_u32_t, int);

static void parse_http_request(int);

static int build_http_header(http_header_type, olsr_bool, olsr_u32_t, char *, olsr_u32_t);

static int build_frame(char *, olsr_u32_t, char *, char *, int, int(*frame_body_cb)(char *, olsr_u32_t));

static int build_routes_body(char *, olsr_u32_t);

static int build_config_body(char *, olsr_u32_t);

static int build_neigh_body(char *, olsr_u32_t);

static int build_topo_body(char *, olsr_u32_t);

static int build_mid_body(char *, olsr_u32_t);

static int build_nodes_body(char *, olsr_u32_t);

static int build_all_body(char *, olsr_u32_t);

static int build_about_body(char *, olsr_u32_t);

static int build_cfgfile_body(char *, olsr_u32_t);

static int check_allowed_ip(const struct allowed_net * const allowed_nets, const union olsr_ip_addr * const addr);

static int build_ip_txt(char *buf, const olsr_u32_t bufsize, const olsr_bool want_link,
                        const union olsr_ip_addr * const ipaddr, const int prefix_len);

static int build_ipaddr_link(char *buf, const olsr_u32_t bufsize, const olsr_bool want_link,
                             const union olsr_ip_addr * const ipaddr,
                             const int prefix_len);
static int section_title(char *buf, olsr_u32_t bufsize, const char *title);

static ssize_t writen(int fd, const void *buf, size_t count);

static struct timeval start_time;
static struct http_stats stats;
static int client_sockets[MAX_CLIENTS];
static int curr_clients;
static int http_socket;

#if 0
int netsprintf(char *str, const char* format, ...) __attribute__((format(printf, 2, 3)));
static int netsprintf_direct = 0;
static int netsprintf_error = 0;
#define sprintf netsprintf
#define NETDIRECT
#endif

static const struct tab_entry tab_entries[] = {
    {"Configuration",  "config",  build_config_body,  OLSR_TRUE},
    {"Routes",         "routes",  build_routes_body,  OLSR_TRUE},
    {"Links/Topology", "nodes",   build_nodes_body,   OLSR_TRUE},
    {"All",            "all",     build_all_body,     OLSR_TRUE},
#ifdef ADMIN_INTERFACE
    {"Admin",          "admin",   build_admin_body,   OLSR_TRUE},
#endif
    {"About",          "about",   build_about_body,   OLSR_TRUE},
    {"FOO",            "cfgfile", build_cfgfile_body, OLSR_FALSE},
    {NULL,             NULL,      NULL,               OLSR_FALSE}
};

static const struct static_bin_file_entry static_bin_files[] = {
    {"favicon.ico", favicon_ico, 1406/*favicon_ico_len*/},
    {"logo.gif", logo_gif, 2801/*logo_gif_len*/},
    {"grayline.gif", grayline_gif, 43/*grayline_gif_len*/},
    {NULL, NULL, 0}
};

static const struct static_txt_file_entry static_txt_files[] = {
    {"httpinfo.css", httpinfo_css},
    {NULL, NULL}
};


static const struct dynamic_file_entry dynamic_files[] =
  {
#ifdef ADMIN_INTERFACE
    {"set_values", process_set_values},
#endif
    {NULL, NULL}
  };

/**
 *Do initialization here
 *
 *This function is called by the my_init
 *function in uolsrd_plugin.c
 */
int
olsrd_plugin_init(void)
{
  /* Get start time */
  gettimeofday(&start_time, NULL);

  curr_clients = 0;
  /* set up HTTP socket */
  http_socket = get_http_socket(http_port != 0 ? http_port :  DEFAULT_TCP_PORT);

  if(http_socket < 0)
    {
      fprintf(stderr, "(HTTPINFO) could not initialize HTTP socket\n");
      exit(0);
    }

  /* Register socket */
  add_olsr_socket(http_socket, &parse_http_request);

  return 1;
}

static int
get_http_socket(int port)
{
  struct sockaddr_in sin;
  olsr_u32_t yes = 1;
  int s;

  /* Init ipc socket */
  if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) 
    {
      olsr_printf(1, "(HTTPINFO)socket %s\n", strerror(errno));
      return -1;
    }

  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes)) < 0) 
    {
      olsr_printf(1, "(HTTPINFO)SO_REUSEADDR failed %s\n", strerror(errno));
      close(s);
      return -1;
    }



  /* Bind the socket */
  
  /* complete the socket structure */
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = htons(port);
  
  /* bind the socket to the port number */
  if (bind(s, (struct sockaddr *) &sin, sizeof(sin)) == -1) 
    {
      olsr_printf(1, "(HTTPINFO) bind failed %s\n", strerror(errno));
      close(s);
      return -1;
    }
      
  /* show that we are willing to listen */
  if (listen(s, 1) == -1) 
    {
      olsr_printf(1, "(HTTPINFO) listen failed %s\n", strerror(errno));
      close(s);
      return -1;
    }

  return s;
}


/* Non reentrant - but we are not multithreaded anyway */
void
parse_http_request(int fd)
{
  struct sockaddr_in pin;
  socklen_t addrlen;
  char *addr;  
  char req[MAX_HTTPREQ_SIZE];
  static char body[HTML_BUFSIZE];
  char req_type[11];
  char filename[251];
  char http_version[11];
  int c = 0, r = 1, size = 0;

  if(curr_clients >= MAX_CLIENTS)
    return;

  curr_clients++;

  addrlen = sizeof(struct sockaddr_in);
  if ((client_sockets[curr_clients] = accept(fd, (struct sockaddr *)  &pin, &addrlen)) == -1)
    {
      olsr_printf(1, "(HTTPINFO) accept: %s\n", strerror(errno));
      goto close_connection;
    }

  if(!check_allowed_ip(allowed_nets, (union olsr_ip_addr *)&pin.sin_addr.s_addr))
    {
      olsr_printf(1, "HTTP request from non-allowed host %s!\n", 
		  olsr_ip_to_string((union olsr_ip_addr *)&pin.sin_addr.s_addr));
      close(client_sockets[curr_clients]);
    }

  addr = inet_ntoa(pin.sin_addr);


  memset(req, 0, MAX_HTTPREQ_SIZE);
  memset(body, 0, 1024*10);

  while((r = recv(client_sockets[curr_clients], &req[c], 1, 0)) > 0 && (c < (MAX_HTTPREQ_SIZE-1)))
    {
      c++;

      if((c > 3 && !strcmp(&req[c-4], "\r\n\r\n")) ||
	 (c > 1 && !strcmp(&req[c-2], "\n\n")))
	break;
    }
  
  if(r < 0)
    {
      olsr_printf(1, "(HTTPINFO) Failed to recieve data from client!\n");
      stats.err_hits++;
      goto close_connection;
    }
  
  /* Get the request */
  if(sscanf(req, "%10s %250s %10s\n", req_type, filename, http_version) != 3)
    {
      /* Try without HTTP version */
      if(sscanf(req, "%10s %250s\n", req_type, filename) != 2)
	{
	  olsr_printf(1, "(HTTPINFO) Error parsing request %s!\n", req);
	  stats.err_hits++;
	  goto close_connection;
	}
    }
  
  
  olsr_printf(1, "Request: %s\nfile: %s\nVersion: %s\n\n", req_type, filename, http_version);

  if(!strcmp(req_type, "POST"))
    {
#ifdef ADMIN_INTERFACE
      int i = 0;
      while(dynamic_files[i].filename)
	{
	  printf("POST checking %s\n", dynamic_files[i].filename);
	  if(FILENREQ_MATCH(filename, dynamic_files[i].filename))
	    {
	      olsr_u32_t param_size;

	      stats.ok_hits++;

	      param_size = recv(client_sockets[curr_clients], req, MAX_HTTPREQ_SIZE-1, 0);

	      req[param_size] = '\0';
	      printf("Dynamic read %d bytes\n", param_size);
	      
	      //memcpy(body, dynamic_files[i].data, static_bin_files[i].data_size);
	      size += dynamic_files[i].process_data_cb(req, param_size, &body[size], sizeof(body)-size);
	      c = build_http_header(HTTP_OK, OLSR_TRUE, size, req, MAX_HTTPREQ_SIZE);  
	      goto send_http_data;
	    }
	  i++;
	}
#endif
      /* We only support GET */
      strcpy(body, HTTP_400_MSG);
      stats.ill_hits++;
      c = build_http_header(HTTP_BAD_REQ, OLSR_TRUE, strlen(body), req, MAX_HTTPREQ_SIZE);
    }
  else if(!strcmp(req_type, "GET"))
    {
      int i = 0;
      int y = 0;

      while(static_bin_files[i].filename)
	{
	  if(FILENREQ_MATCH(filename, static_bin_files[i].filename))
	    break;
	  i++;
	}
      
      if(static_bin_files[i].filename)
	{
	  stats.ok_hits++;
	  memcpy(body, static_bin_files[i].data, static_bin_files[i].data_size);
	  size = static_bin_files[i].data_size;
	  c = build_http_header(HTTP_OK, OLSR_FALSE, size, req, MAX_HTTPREQ_SIZE);  
	  goto send_http_data;
	}

      i = 0;

      while(static_txt_files[i].filename)
	{
	  if(FILENREQ_MATCH(filename, static_txt_files[i].filename))
	    break;
	  i++;
	}
      
      if(static_txt_files[i].filename)
	{
	  stats.ok_hits++;
	  y = 0;
	  while(static_txt_files[i].data[y])
	    {
	      size += snprintf(&body[size], sizeof(body)-size, static_txt_files[i].data[y]);
	      y++;
	    }

	  c = build_http_header(HTTP_OK, OLSR_FALSE, size, req, MAX_HTTPREQ_SIZE);  
	  goto send_http_data;
	}

      i = 0;

      if(strlen(filename) > 1)
	{
	  while(tab_entries[i].filename)
	    {
	      if(FILENREQ_MATCH(filename, tab_entries[i].filename))
		break;
	      i++;
	    }
	}

      if(tab_entries[i].filename)
	{
#ifdef NETDIRECT
	  c = build_http_header(HTTP_OK, OLSR_TRUE, size, req, MAX_HTTPREQ_SIZE);
	  r = send(client_sockets[curr_clients], req, c, 0);   
	  if(r < 0)
	    {
	      olsr_printf(1, "(HTTPINFO) Failed sending data to client!\n");
	      goto close_connection;
	    }
	  netsprintf_error = 0;
	  netsprintf_direct = 1;
#endif
          size += snprintf(&body[size], sizeof(body)-size, "%s", http_ok_head);
	  
	  size += build_tabs(&body[size], sizeof(body)-size, i);
	  size += build_frame(&body[size], 
			      sizeof(body)-size, 
			      "Current Routes", 
			      "routes", 
			      FRAMEWIDTH, 
			      tab_entries[i].build_body_cb);
	  
	  stats.ok_hits++;

          size += snprintf(&body[size], sizeof(body)-size, http_ok_tail);
	  
#ifdef NETDIRECT
	  netsprintf_direct = 1;
	  goto close_connection;
#else
	  c = build_http_header(HTTP_OK, OLSR_TRUE, size, req, MAX_HTTPREQ_SIZE);
	  
	  goto send_http_data;
#endif
	}
      
      
      stats.ill_hits++;
      strcpy(body, HTTP_404_MSG);
      c = build_http_header(HTTP_BAD_FILE, OLSR_TRUE, strlen(body), req, MAX_HTTPREQ_SIZE);
    }
  else
    {
      /* We only support GET */
      strcpy(body, HTTP_400_MSG);
      stats.ill_hits++;
      c = build_http_header(HTTP_BAD_REQ, OLSR_TRUE, strlen(body), req, MAX_HTTPREQ_SIZE);
    }

 send_http_data:
  
  r = writen(client_sockets[curr_clients], req, c);   
  if(r < 0)
    {
      olsr_printf(1, "(HTTPINFO) Failed sending data to client!\n");
      goto close_connection;
    }

  r = writen(client_sockets[curr_clients], body, size);
  if(r < 0)
    {
      olsr_printf(1, "(HTTPINFO) Failed sending data to client!\n");
      goto close_connection;
    }

 close_connection:
  close(client_sockets[curr_clients]);
  curr_clients--;

}


int
build_http_header(http_header_type type, 
		  olsr_bool is_html, 
		  olsr_u32_t msgsize, 
		  char *buf, 
		  olsr_u32_t bufsize)
{
  time_t currtime;
  const char *h;
  int size;

  switch(type) {
  case(HTTP_BAD_REQ):
      h = HTTP_400;
      break;
  case(HTTP_BAD_FILE):
      h = HTTP_404;
      break;
  default:
      /* Defaults to OK */
      h = HTTP_200;
      break;
  }
  size = snprintf(buf, bufsize, "%s", h);

  /* Date */
  time(&currtime);
  size += strftime(&buf[size], bufsize-size, "Date: %a, %d %b %Y %H:%M:%S GMT\r\n", localtime(&currtime));
  
  /* Server version */
  size += snprintf(&buf[size], bufsize-size, "Server: %s %s %s\r\n", PLUGIN_NAME, PLUGIN_VERSION, HTTP_VERSION);

  /* connection-type */
  size += snprintf(&buf[size], bufsize-size, "Connection: closed\r\n");

  /* MIME type */
  size += snprintf(&buf[size], bufsize-size, "Content-type: text/%s\r\n", is_html ? "html" : "plain");

  /* Content length */
  if(msgsize > 0) {
      size += snprintf(&buf[size], bufsize-size, "Content-length: %i\r\n", msgsize);
  }

  /* Cache-control 
   * No caching dynamic pages
   */
  size += snprintf(&buf[size], bufsize-size, "Cache-Control: no-cache\r\n");

  if(!is_html) {
    size += snprintf(&buf[size], bufsize-size, "Accept-Ranges: bytes\r\n");
  }
  /* End header */
  size += snprintf(&buf[size], bufsize-size, "\r\n");
  
  olsr_printf(1, "HEADER:\n%s", buf);

  return size;
}



static int build_tabs(char *buf, const olsr_u32_t bufsize, int active)
{
  int size = 0, tabs = 0;

  size += snprintf(&buf[size], bufsize-size, html_tabs_prolog);
  for(tabs = 0; tab_entries[tabs].tab_label; tabs++) {
    if(!tab_entries[tabs].display_tab)
      continue;

    size += snprintf(&buf[size], bufsize-size, 
                     "<li><a href=\"%s\"%s>%s</a></li>\n",
                     tab_entries[tabs].filename, 
                     tabs == active ? " class=\"active\"" : "",
                     tab_entries[tabs].tab_label);
  }
  size += snprintf(&buf[size], bufsize-size, html_tabs_epilog);
  return size;
}


/*
 * destructor - called at unload
 */
void
olsr_plugin_exit(void)
{
  if(http_socket)
    close(http_socket);
}


static int section_title(char *buf, olsr_u32_t bufsize, const char *title)
{
  return snprintf(buf, bufsize,
                  "<h2>%s</h2>\n"
                  "<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"0\" align=\"center\">\n", title);
}

static int build_frame(char *buf,
                       olsr_u32_t bufsize,
                       char *title __attribute__((unused)), 
                       char *link __attribute__((unused)), 
                       int width __attribute__((unused)),
                       int(*frame_body_cb)(char *, olsr_u32_t))
{
  int size = 0;
  size += snprintf(&buf[size], bufsize-size, "<div id=\"maintable\">\n");
  size += frame_body_cb(&buf[size], bufsize-size);  
  size += snprintf(&buf[size], bufsize-size, "</div>\n");
  return size;
}

static int fmt_href(char *buf,
                    const olsr_u32_t bufsize,
                    const union olsr_ip_addr * const ipaddr)
{
  return snprintf(buf, bufsize,
                  "<a href=\"http://%s:%d/all\">",
                  olsr_ip_to_string(ipaddr),
                  http_port);

}

static int build_ip_txt(char *buf,
                        const olsr_u32_t bufsize,
                        const olsr_bool print_link,
                        const union olsr_ip_addr * const ipaddr,
                        const int prefix_len)
{
  int size = 0;
  if (print_link) { /* Print the link only if there is no prefix_len */
    size += fmt_href(&buf[size], bufsize-size, ipaddr);
  }

  /* print ip address or ip prefix ? */
  if (prefix_len == -1) {
      size += snprintf(&buf[size], bufsize-size, "%s", olsr_ip_to_string(ipaddr));
  } else {
      size += snprintf(&buf[size], bufsize-size, "%s/%d", olsr_ip_to_string(ipaddr),
                       prefix_len);
  }
  
  if (print_link) { /* Print the link only if there is no prefix_len */
    size += snprintf(&buf[size], bufsize-size, "</a>");
  }
  return size;
}

static int build_ipaddr_link(char *buf, const olsr_u32_t bufsize,
                             const olsr_bool want_link,
                             const union olsr_ip_addr * const ipaddr,
                             const int prefix_len)
{
  int size = 0;
  const struct hostent * const hp =
#ifndef WIN32
      resolve_ip_addresses ? gethostbyaddr(ipaddr, olsr_cnf->ipsize, olsr_cnf->ip_version) :
#endif
      NULL;
  const int print_link = want_link && (prefix_len == -1 || prefix_len == olsr_cnf->maxplen);

  size += snprintf(&buf[size], bufsize-size, "<td>");
  size += build_ip_txt(&buf[size], bufsize-size, print_link, ipaddr, prefix_len);
  size += snprintf(&buf[size], bufsize-size, "</td>");

  if (resolve_ip_addresses) {
    if (hp) {
      size += snprintf(&buf[size], bufsize-size, "<td>(");
      if (print_link) {
        size += fmt_href(&buf[size], bufsize-size, ipaddr);
      }
      size += snprintf(&buf[size], bufsize-size, "%s", hp->h_name);
      if (print_link) {
        size += snprintf(&buf[size], bufsize-size, "</a>");
      }
      size += snprintf(&buf[size], bufsize-size, ")</td>");
    } else {
      size += snprintf(&buf[size], bufsize-size, "<td/>");
    }
  }
  return size;
}

#define build_ipaddr_with_link(buf, bufsize, ipaddr, plen) \
          build_ipaddr_link((buf), (bufsize), OLSR_TRUE, (ipaddr), (plen))
#define build_ipaddr_no_link(buf, bufsize, ipaddr, plen) \
          build_ipaddr_link((buf), (bufsize), OLSR_FALSE, (ipaddr), (plen))

static int build_route(char *buf, olsr_u32_t bufsize, const struct rt_entry * rt)
{
  int size = 0;

  size += snprintf(&buf[size], bufsize-size, "<tr>");
  size += build_ipaddr_with_link(&buf[size], bufsize-size, &rt->rt_dst.prefix,
                                 rt->rt_dst.prefix_len);
  size += build_ipaddr_with_link(&buf[size], bufsize-size,
                                 &rt->rt_best->rtp_nexthop.gateway, -1);

  size += snprintf(&buf[size], bufsize-size, "<td align=\"center\">%d</td>",
                   rt->rt_best->rtp_metric.hops);
  size += snprintf(&buf[size], bufsize-size, "<td align=\"center\">%.3f</td>",
                     rt->rt_best->rtp_metric.etx);
  size += snprintf(&buf[size], bufsize-size, "<td align=\"center\">%s</td></tr>\n",
                   if_ifwithindex_name(rt->rt_best->rtp_nexthop.iif_index));
  return size;
}

static int build_routes_body(char *buf, olsr_u32_t bufsize)
{
  int size = 0;
  struct rt_entry *rt;
  size += section_title(&buf[size], bufsize-size, "OLSR Routes in Kernel");
  size += snprintf(&buf[size], bufsize-size,
                   "<tr><th%1$s>Destination</th><th%1$s>Gateway</th><th>Metric</th><th>ETX</th><th>Interface</th></tr>\n",
                  resolve_ip_addresses ? " colspan=\"2\"" : "");

  /* Walk the route table */
  OLSR_FOR_ALL_RT_ENTRIES(rt) {
      size += build_route(&buf[size], bufsize-size, rt);
  } OLSR_FOR_ALL_RT_ENTRIES_END(rt);

  size += snprintf(&buf[size], bufsize-size, "</table>\n");

  return size;
}

static int build_config_body(char *buf, olsr_u32_t bufsize)
{
    int size = 0;
    struct olsr_if *ifs;
    struct plugin_entry *pentry;
    struct plugin_param *pparam;

    size += snprintf(&buf[size], bufsize-size, "Version: %s (built on %s on %s)\n<br>", olsrd_version, build_date, build_host);
    size += snprintf(&buf[size], bufsize-size, "OS: %s\n<br>", OS);

    { 
      time_t currtime = time(NULL);
      int rc = strftime(&buf[size], bufsize-size, "System time: <em>%a, %d %b %Y %H:%M:%S</em><br>", localtime(&currtime));
      if (rc > 0) {
        size += rc;
      }
    }

    {
      struct timeval now, uptime;
      int hours, mins, days;
      gettimeofday(&now, NULL);
      timersub(&now, &start_time, &uptime);

      days = uptime.tv_sec/86400;
      uptime.tv_sec %= 86400;
      hours = uptime.tv_sec/3600;
      uptime.tv_sec %= 3600;
      mins = uptime.tv_sec/60;
      uptime.tv_sec %= 60;

      size += snprintf(&buf[size], bufsize-size, "Olsrd uptime: <em>");
      if (days) {
        size += snprintf(&buf[size], bufsize-size, "%d day(s) ", days);
      }
      size += snprintf(&buf[size], bufsize-size, "%02d hours %02d minutes %02d seconds</em><br/>\n", hours, mins, (int)uptime.tv_sec);
    }

    size += snprintf(&buf[size], bufsize-size, "HTTP stats(ok/dyn/error/illegal): <em>%d/%d/%d/%d</em><br>\n", stats.ok_hits, stats.dyn_hits, stats.err_hits, stats.ill_hits);

    size += snprintf(&buf[size], bufsize-size, "Click <a href=\"/cfgfile\">here</a> to <em>generate a configuration file for this node</em>.\n");

    size += snprintf(&buf[size], bufsize-size, "<h2>Variables</h2>\n");

    size += snprintf(&buf[size], bufsize-size, "<table width=\"100%%\" border=\"0\">\n<tr>");

    size += snprintf(&buf[size], bufsize-size, "<td>Main address: <strong>%s</strong></td>\n", olsr_ip_to_string(&olsr_cnf->main_addr));
    
    size += snprintf(&buf[size], bufsize-size, "<td>IP version: %d</td>\n", olsr_cnf->ip_version == AF_INET ? 4 : 6);

    size += snprintf(&buf[size], bufsize-size, "<td>Debug level: %d</td>\n", olsr_cnf->debug_level);

    size += snprintf(&buf[size], bufsize-size, "</tr>\n<tr>\n");

    size += snprintf(&buf[size], bufsize-size, "<td>Pollrate: %0.2f</td>\n", olsr_cnf->pollrate);
    size += snprintf(&buf[size], bufsize-size, "<td>TC redundancy: %d</td>\n", olsr_cnf->tc_redundancy);
    size += snprintf(&buf[size], bufsize-size, "<td>MPR coverage: %d</td>\n", olsr_cnf->mpr_coverage);


    size += snprintf(&buf[size], bufsize-size, "</tr>\n<tr>\n");

    size += snprintf(&buf[size], bufsize-size, "<td>Fisheye: %s</td>\n", olsr_cnf->lq_fish ? "Enabled" : "Disabled");

    size += snprintf(&buf[size], bufsize-size, "<td>TOS: 0x%04x</td>\n", olsr_cnf->tos);

    size += snprintf(&buf[size], bufsize-size, "<td>RtTable: 0x%04x</td>\n", olsr_cnf->rttable);

    size += snprintf(&buf[size], bufsize-size, "<td>Willingness: %d %s</td>\n", olsr_cnf->willingness, olsr_cnf->willingness_auto ? "(auto)" : "");
    
    size += snprintf(&buf[size], bufsize-size, "</tr>\n<tr>\n");

    if (olsr_cnf->lq_level == 0)
      {
        size += snprintf(&buf[size], bufsize-size, "<td>Hysteresis: %s</td>\n", olsr_cnf->use_hysteresis ? "Enabled" : "Disabled");
	if (olsr_cnf->use_hysteresis)
          {
            size += snprintf(&buf[size], bufsize-size, "<td>Hyst scaling: %0.2f</td>\n", olsr_cnf->hysteresis_param.scaling);
            size += snprintf(&buf[size], bufsize-size, "<td>Hyst lower/upper: %0.2f/%0.2f</td>\n", olsr_cnf->hysteresis_param.thr_low, olsr_cnf->hysteresis_param.thr_high);
          }
      }

    size += snprintf(&buf[size], bufsize-size, "</tr>\n<tr>\n");

    size += snprintf(&buf[size], bufsize-size, "<td>LQ extension: %s</td>\n", olsr_cnf->lq_level ? "Enabled" : "Disabled");
    if (olsr_cnf->lq_level)
      {
        size += snprintf(&buf[size], bufsize-size, "<td>LQ level: %d</td>\n", olsr_cnf->lq_level);
        size += snprintf(&buf[size], bufsize-size, "<td>LQ winsize: %d</td>\n", olsr_cnf->lq_wsize);
      }

    size += snprintf(&buf[size], bufsize-size, "</tr></table>\n");

    size += snprintf(&buf[size], bufsize-size, "<h2>Interfaces</h2>\n");


    size += snprintf(&buf[size], bufsize-size, "<table width=\"100%%\" border=\"0\">\n");


    for(ifs = olsr_cnf->interfaces; ifs; ifs = ifs->next)
      {
	struct interface *rifs = ifs->interf;

	size += snprintf(&buf[size], bufsize-size, "<tr><th colspan=3>%s</th>\n", ifs->name);
	if(!rifs)
	  {
	    size += snprintf(&buf[size], bufsize-size, "<tr><td colspan=3>Status: DOWN</td></tr>\n");
	    continue;
	  }
	
	if(olsr_cnf->ip_version == AF_INET)
	  {
	    size += snprintf(&buf[size], bufsize-size, "<tr><td>IP: %s</td>\n", 
			    sockaddr_to_string(&rifs->int_addr));
	    size += snprintf(&buf[size], bufsize-size, "<td>MASK: %s</td>\n", 
			    sockaddr_to_string(&rifs->int_netmask));
	    size += snprintf(&buf[size], bufsize-size, "<td>BCAST: %s</td></tr>\n",
			    sockaddr_to_string(&rifs->int_broadaddr));
	    size += snprintf(&buf[size], bufsize-size, "<tr><td>MTU: %d</td>\n", rifs->int_mtu);
	    size += snprintf(&buf[size], bufsize-size, "<td>WLAN: %s</td>\n", rifs->is_wireless ? "Yes" : "No");
	    size += snprintf(&buf[size], bufsize-size, "<td>STATUS: UP</td></tr>\n");
	  }
	else
	  {
	    size += snprintf(&buf[size], bufsize-size, "<tr><td>IP: %s</td>\n", olsr_ip_to_string((union olsr_ip_addr *)&rifs->int6_addr.sin6_addr));
	    size += snprintf(&buf[size], bufsize-size, "<td>MCAST: %s</td>\n", olsr_ip_to_string((union olsr_ip_addr *)&rifs->int6_multaddr.sin6_addr));
	    size += snprintf(&buf[size], bufsize-size, "<td></td></tr>\n");
	    size += snprintf(&buf[size], bufsize-size, "<tr><td>MTU: %d</td>\n", rifs->int_mtu);
	    size += snprintf(&buf[size], bufsize-size, "<td>WLAN: %s</td>\n", rifs->is_wireless ? "Yes" : "No");
	    size += snprintf(&buf[size], bufsize-size, "<td>STATUS: UP</td></tr>\n");
	  }	    
      }

    size += snprintf(&buf[size], bufsize-size, "</table>\n");

    if(olsr_cnf->allow_no_interfaces)
      size += snprintf(&buf[size], bufsize-size, "<em>Olsrd is configured to run even if no interfaces are available</em><br>\n");
    else
      size += snprintf(&buf[size], bufsize-size, "<em>Olsrd is configured to halt if no interfaces are available</em><br>\n");

    size += snprintf(&buf[size], bufsize-size, "<h2>Plugins</h2>\n");

    size += snprintf(&buf[size], bufsize-size, "<table width=\"100%%\" border=\"0\"><tr><th>Name</th><th>Parameters</th></tr>\n");

    for(pentry = olsr_cnf->plugins; pentry; pentry = pentry->next)
      {
	size += snprintf(&buf[size], bufsize-size, "<tr><td>%s</td>\n", pentry->name);

	size += snprintf(&buf[size], bufsize-size, "<td><select>\n");
	size += snprintf(&buf[size], bufsize-size, "<option>KEY, VALUE</option>\n");

	for(pparam = pentry->params; pparam; pparam = pparam->next)
	  {
	    size += snprintf(&buf[size], bufsize-size, "<option>\"%s\", \"%s\"</option>\n",
			    pparam->key,
			    pparam->value);
	  }
	size += snprintf(&buf[size], bufsize-size, "</select></td></tr>\n");

      }

    size += snprintf(&buf[size], bufsize-size, "</table>\n");

    size += section_title(&buf[size], bufsize-size, "Announced HNA entries");

    if((olsr_cnf->ip_version == AF_INET) && (olsr_cnf->hna4_entries))
      {
	struct hna4_entry *hna4;
	
	size += snprintf(&buf[size], bufsize-size,
                         "<tr><th>Network</th><th>Netmask</th></tr>\n");
	
	for(hna4 = olsr_cnf->hna4_entries; hna4; hna4 = hna4->next)
	  {
	    size += snprintf(&buf[size], bufsize-size, "<tr><td>%s</td><td>%s</td></tr>\n", 
			    olsr_ip_to_string(&hna4->net),
			    olsr_ip_to_string(&hna4->netmask));
	  }
      } 
   else if((olsr_cnf->ip_version == AF_INET6) && (olsr_cnf->hna6_entries))
      {
	struct hna6_entry *hna6;
	
	size += snprintf(&buf[size], bufsize-size,
                         "<tr><th>Network</th><th>Prefix length</th></tr>\n");
	
	for(hna6 = olsr_cnf->hna6_entries; hna6; hna6 = hna6->next)
	  {
	    size += snprintf(&buf[size], bufsize-size, "<tr><td>%s</td><td>%d</td></tr>\n", 
			    olsr_ip_to_string(&hna6->net),
			    hna6->prefix_len);
	  }
	
      }
    size += snprintf(&buf[size], bufsize-size, "</table>\n");

    return size;
}



static int build_neigh_body(char *buf, olsr_u32_t bufsize)
{
  struct neighbor_entry *neigh;
  struct neighbor_2_list_entry *list_2;
  struct link_entry *link = NULL;
  int size = 0, index, thop_cnt;

  size += section_title(&buf[size], bufsize-size, "Links");

  size += snprintf(&buf[size], bufsize-size,
                   "<tr><th>Local IP</th><th>Remote IP</th><th>Hysteresis</th>\n");
  if (olsr_cnf->lq_level > 0) {
    size += snprintf(&buf[size], bufsize-size, "<th align=\"right\">LinkQuality</th><th>lost</th><th>total</th><th align=\"right\">NLQ</th><th align=\"right\">ETX</th>\n");
  }
  size += snprintf(&buf[size], bufsize-size, "</tr>\n");

  /* Link set */
  link = link_set;
    while(link)
      {
        size += snprintf(&buf[size], bufsize-size, "<tr>");
        size += build_ipaddr_with_link(&buf[size], bufsize, &link->local_iface_addr, -1);
        size += build_ipaddr_with_link(&buf[size], bufsize, &link->neighbor_iface_addr, -1);
	size += snprintf(&buf[size], bufsize-size, "<td align=\"right\">%0.2f</td>", link->L_link_quality);
        if (olsr_cnf->lq_level > 0) {
	    size += snprintf(&buf[size], bufsize-size,
                           "<td align=\"right\">%0.2f</td>"
                           "<td>%d</td>"
                           "<td>%d</td>"
                           "<td align=\"right\">%0.2f</td>"
                           "<td align=\"right\">%0.2f</td></tr>\n",
                           link->loss_link_quality,
                           link->lost_packets, 
                           link->total_packets,
                           link->neigh_link_quality, 
                           (link->loss_link_quality * link->neigh_link_quality) ? 1.0 / (link->loss_link_quality * link->neigh_link_quality) : 0.0);
        }
	size += snprintf(&buf[size], bufsize-size, "</tr>\n");

	link = link->next;
      }

  size += snprintf(&buf[size], bufsize-size, "</table>\n");

  size += section_title(&buf[size], bufsize-size, "Neighbors");
  size += snprintf(&buf[size], bufsize-size, 
                   "<tr><th>IP address</th><th align=\"center\">SYM</th><th align=\"center\">MPR</th><th align=\"center\">MPRS</th><th align=\"center\">Willingness</th><th>2 Hop Neighbors</th></tr>\n");
  /* Neighbors */
  for(index=0;index<HASHSIZE;index++)
    {
      for(neigh = neighbortable[index].next;
	  neigh != &neighbortable[index];
	  neigh = neigh->next)
	{
          size += snprintf(&buf[size], bufsize-size, "<tr>");
          size += build_ipaddr_with_link(&buf[size], bufsize, &neigh->neighbor_main_addr, -1);
	  size += snprintf(&buf[size], bufsize-size, 
			  "<td align=\"center\">%s</td>"
			  "<td align=\"center\">%s</td>"
			  "<td align=\"center\">%s</td>"
			  "<td align=\"center\">%d</td>", 
			  (neigh->status == SYM) ? "YES" : "NO",
			  neigh->is_mpr ? "YES" : "NO",
			  olsr_lookup_mprs_set(&neigh->neighbor_main_addr) ? "YES" : "NO",
			  neigh->willingness);

	  size += snprintf(&buf[size], bufsize-size, "<td><select>\n"
                                                     "<option>IP ADDRESS</option>\n");

	  thop_cnt = 0;
	  for(list_2 = neigh->neighbor_2_list.next;
	      list_2 != &neigh->neighbor_2_list;
	      list_2 = list_2->next)
	    {
              size += snprintf(&buf[size], bufsize-size, "<option>%s</option>\n", olsr_ip_to_string(&list_2->neighbor_2->neighbor_2_addr));
	      thop_cnt ++;
	    }
	  size += snprintf(&buf[size], bufsize-size, "</select> (%d)</td></tr>\n", thop_cnt);

	}
    }

  size += snprintf(&buf[size], bufsize-size, "</table>\n");

  return size;
}

static int build_topo_body(char *buf, olsr_u32_t bufsize)
{
  int size = 0;
  struct tc_entry *tc;

  size += section_title(&buf[size], bufsize-size, "Topology Entries");
  size += snprintf(&buf[size], bufsize-size, "<tr><th>Destination IP</th><th>Last Hop IP</th>");
  if (olsr_cnf->lq_level > 0) {
    size += snprintf(&buf[size], bufsize-size, "<th align=\"right\">LQ</th><th align=\"right\">ILQ</th><th align=\"right\">ETX</th>");
  }
  size += snprintf(&buf[size], bufsize-size, "</tr>\n");

  OLSR_FOR_ALL_TC_ENTRIES(tc) {
      struct tc_edge_entry *tc_edge;
      OLSR_FOR_ALL_TC_EDGE_ENTRIES(tc, tc_edge) {

          size += snprintf(&buf[size], bufsize-size, "<tr>");
          size += build_ipaddr_with_link(&buf[size], bufsize, &tc_edge->T_dest_addr, -1);
          size += build_ipaddr_with_link(&buf[size], bufsize, &tc->addr, -1);
          if (olsr_cnf->lq_level > 0) {
              const double d = tc_edge->link_quality * tc_edge->inverse_link_quality;
              size += snprintf(&buf[size], bufsize-size,
                               "<td align=\"right\">%0.2f</td>"
                               "<td align=\"right\">%0.2f</td>"
                               "<td align=\"right\">%0.2f</td>\n",
                               tc_edge->link_quality,
                               tc_edge->inverse_link_quality,
                               d ? 1.0 / d : 0.0);
          }
          size += snprintf(&buf[size], bufsize-size, "</tr>\n");

      } OLSR_FOR_ALL_TC_EDGE_ENTRIES_END(tc, tc_edge);
  } OLSR_FOR_ALL_TC_ENTRIES_END(tc);

  size += snprintf(&buf[size], bufsize-size, "</table>\n");

  return size;
}

static int build_mid_body(char *buf, olsr_u32_t bufsize)
{
  int size = 0;
  olsr_u8_t index;

  size += section_title(&buf[size], bufsize-size, "MID Entries");
  size += snprintf(&buf[size], bufsize-size,
                   "<tr><th>Main Address</th><th>Aliases</th></tr>\n");
  
  /* MID */  
  for(index = 0;index < HASHSIZE; index++)
    {
      struct mid_entry *entry = mid_set[index].next;
      while(entry != &mid_set[index])
	{
          int mid_cnt;
          struct mid_address *alias;
          size += snprintf(&buf[size], bufsize-size, "<tr>");
          size += build_ipaddr_with_link(&buf[size], bufsize, &entry->main_addr, -1);
	  size += snprintf(&buf[size], bufsize-size, "<td><select>\n<option>IP ADDRESS</option>\n");

	  alias = entry->aliases;
	  mid_cnt = 0;
	  while(alias)
	    {
	      size += snprintf(&buf[size], bufsize-size, "<option>%s</option>\n", olsr_ip_to_string(&alias->alias));
	      mid_cnt++;
	      alias = alias->next_alias;
	    }
	  size += snprintf(&buf[size], bufsize-size, "</select> (%d)</td></tr>\n", mid_cnt);
	  entry = entry->next;
	}
    }

  size += snprintf(&buf[size], bufsize-size, "</table>\n");
  return size;
}


static int build_nodes_body(char *buf, olsr_u32_t bufsize)
{
  int size = 0;

  size += build_neigh_body(&buf[size], bufsize-size);
  size += build_topo_body(&buf[size], bufsize-size);
  size += build_mid_body(&buf[size], bufsize-size);

  return size;
}

static int build_all_body(char *buf, olsr_u32_t bufsize)
{
  int size = 0;

  size += build_config_body(&buf[size], bufsize-size);
  size += build_routes_body(&buf[size], bufsize-size);
  size += build_neigh_body(&buf[size], bufsize-size);
  size += build_topo_body(&buf[size], bufsize-size);
  size += build_mid_body(&buf[size], bufsize-size);

  return size;
}


static int build_about_body(char *buf, olsr_u32_t bufsize)
{
  return snprintf(buf, bufsize, about_frame, build_date, build_host);
}

static int build_cfgfile_body(char *buf, olsr_u32_t bufsize)
{
  int size = 0, i = 0;

  while(cfgfile_body[i] && strcmp(cfgfile_body[i], "<!-- CFGFILE -->")) {
      size += snprintf(&buf[size], bufsize-size, cfgfile_body[i]);
      i++;
  }

#ifdef NETDIRECT
  {
        /* Hack to make netdirect stuff work with
           olsrd_write_cnf_buf
        */
        char tmpBuf[10000];
        size = olsrd_write_cnf_buf(olsr_cnf, tmpBuf, 10000);
        snprintf(&buf[size], bufsize-size, tmpBuf);
  }
#else
  size += olsrd_write_cnf_buf(olsr_cnf, &buf[size], bufsize-size);
#endif
  
  if(size < 0) {
    size = snprintf(buf, size, "ERROR GENERATING CONFIGFILE!\n");
  }

  i++;
  while(cfgfile_body[i]) {
      size += snprintf(&buf[size], bufsize-size, cfgfile_body[i]);
      i++;
  }
#if 0
  printf("RETURNING %d\n", size);
#endif
  return size;
}

static int check_allowed_ip(const struct allowed_net * const allowed_nets, const union olsr_ip_addr * const addr)
{
    const struct allowed_net *alln;
    for (alln = allowed_nets; alln != NULL; alln = alln->next) {
        if((addr->v4 & alln->mask.v4) == (alln->net.v4 & alln->mask.v4)) {
            return 1;
        }
    }
    return 0;
}



#if 0
/*
 * In a bigger mesh, there are probs with the fixed
 * bufsize. Because the Content-Length header is
 * optional, the sprintf() is changed to a more
 * scalable solution here.
 */
 
int netsprintf(char *str, const char* format, ...)
{
	va_list arg;
	int rv;
	va_start(arg, format);
	rv = vsprintf(str, format, arg);
	va_end(arg);
	if (0 != netsprintf_direct) {
		if (0 == netsprintf_error) {
			if (0 > send(client_sockets[curr_clients], str, rv, 0)) {
				olsr_printf(1, "(HTTPINFO) Failed sending data to client!\n");
				netsprintf_error = 1;
			}
		}
		return 0;
	}
	return rv;
}
#endif

static ssize_t writen(int fd, const void *buf, size_t count)
{
    size_t bytes_left = count;
    const char *p = buf;
    while (bytes_left > 0) {
        const ssize_t written = write(fd, p, bytes_left);
        if (written == -1)  { /* error */
            if (errno == EINTR ) {
                continue;
            }
            return -1;
        }
        /* We wrote something */
        bytes_left -= written;
        p += written;
    }
    return count;
}

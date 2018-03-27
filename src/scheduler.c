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
 * $Id: scheduler.c,v 1.43 2007/09/17 22:24:22 bernd67 Exp $
 */


#include "defs.h"
#include "scheduler.h"
#include "log.h"
#include "tc_set.h"
#include "link_set.h"
#include "duplicate_set.h"
#include "mpr_selector_set.h"
#include "mid_set.h"
#include "mpr.h"
#include "olsr.h"
#include "build_msg.h"

/* Timer data, global. Externed in defs.h */
clock_t now_times;              /* current idea of times(2) reported uptime */
struct timeval now;		/* current idea of time */
struct tm *nowtm;		/* current idea of time (in tm) */

/* Lists */
static struct timeout_entry *timeout_functions;
static struct event_entry *event_functions;

static olsr_bool link_changes; /* is set if changes occur in MPRS set */ 

void
signal_link_changes(olsr_bool val)
{
  link_changes = val;
}

static void 
trigger_dijkstra(void *foo __attribute__((unused)))
{
  OLSR_PRINTF(3, "Triggering Dijkstra\n");

  changes_neighborhood = OLSR_TRUE;
  changes_topology = OLSR_TRUE;
  changes_force = OLSR_TRUE;
}

/**
 *Main scheduler event loop. Polls at every
 *sched_poll_interval and calls all functions
 *that are timed out or that are triggered.
 *Also calls the olsr_process_changes()
 *function at every poll.
 *
 *
 *@return nada
 */

void
scheduler(void)
{
  struct timespec remainder_spec;
  struct timespec sleeptime_spec;

  /*
   *Used to calculate sleep time
   */
  clock_t end_of_loop;
  struct timeval time_used;
  struct timeval interval;
  struct timeval sleeptime_val;

  olsr_u32_t interval_usec;

  struct event_entry *entry;
  struct timeout_entry *time_out_entry;

  struct interface *ifn;

  /* Global buffer for times(2) calls. Do not remove - at least OpenBSD needs it. */
  struct tms tms_buf;
 
  link_changes = OLSR_FALSE;

  if(olsr_cnf->lq_level > 1 && olsr_cnf->lq_dinter > 0.0)
    olsr_register_scheduler_event(trigger_dijkstra, NULL, olsr_cnf->lq_dinter, 0, NULL);

  interval_usec = (olsr_u32_t)(olsr_cnf->pollrate * 1000000);

  interval.tv_sec = interval_usec / 1000000;
  interval.tv_usec = interval_usec % 1000000;

  OLSR_PRINTF(1, "Scheduler started - polling every %0.2f seconds\n", olsr_cnf->pollrate);
  OLSR_PRINTF(3, "Max jitter is %f\n\n", olsr_cnf->max_jitter);

  /* Main scheduler event loop */
  for(;;)
    {
      /* Update now_times */
      now_times = times(&tms_buf);

      /* Update the global timestamp - kept for plugin compat */
      gettimeofday(&now, NULL);
      nowtm = localtime((time_t *)&now.tv_sec);

      while (nowtm == NULL)
	{
	  nowtm = localtime((time_t *)&now.tv_sec);
	}


      /* Run timout functions (before packet generation) */

      time_out_entry = timeout_functions;
      
      while(time_out_entry)
	{
	  time_out_entry->function();
	  time_out_entry = time_out_entry->next;
	}

      /* Update */
      
      olsr_process_changes();


      /* Check for changes in topology */
      if(link_changes)
        {
	  OLSR_PRINTF(3, "ANSN UPDATED %d\n\n", get_local_ansn());
	  increase_local_ansn();
          link_changes = OLSR_FALSE;
	}


      /* Check scheduled events */
      entry = event_functions;

      /* UPDATED - resets timer upon triggered execution */
      while(entry)
	{
	  entry->since_last += olsr_cnf->pollrate;

	  /* Timed out */
	  if((entry->since_last > entry->interval) ||
	     /* Triggered */
	     ((entry->trigger != NULL) &&
	      (*(entry->trigger) == 1)))
	    {
	      /* Run scheduled function */
	      entry->function(entry->param);

	      /* Set jitter */
	      entry->since_last = (float) random()/RAND_MAX;
	      entry->since_last *= olsr_cnf->max_jitter;
	      
	      /* Reset trigger */
	      if(entry->trigger != NULL)
		*(entry->trigger) = 0;
	      
	      //OLSR_PRINTF(3, "Since_last jitter: %0.2f\n", entry->since_last);

	    }

	  entry = entry->next;
	}



      /* looping trough interfaces and emmittin pending data */
      for (ifn = ifnet; ifn ; ifn = ifn->int_next) 
	{ 
	  if(net_output_pending(ifn) && TIMED_OUT(ifn->fwdtimer)) 
	    net_output(ifn);
	}


      end_of_loop = times(&tms_buf);

      //printf("Tick diff: %d\n", end_of_loop - now_times);
      time_used.tv_sec = ((end_of_loop - now_times) * olsr_cnf->system_tick_divider) / 1000;
      time_used.tv_usec = ((end_of_loop - now_times) * olsr_cnf->system_tick_divider) % 1000;

      //printf("Time used: %d.%04d\n", time_used.tv_sec, time_used.tv_usec);

      if(timercmp(&time_used, &interval, <))
	{
	  timersub(&interval, &time_used, &sleeptime_val);
	  
	  // printf("sleeptime_val = %u.%06u\n",
	  //        sleeptime_val.tv_sec, sleeptime_val.tv_usec);
	  
	  sleeptime_spec.tv_sec = sleeptime_val.tv_sec;
	  sleeptime_spec.tv_nsec = sleeptime_val.tv_usec * 1000;
	  
	  while(nanosleep(&sleeptime_spec, &remainder_spec) < 0)
	    sleeptime_spec = remainder_spec;
	}

#if defined WIN32
      // the Ctrl-C signal handler thread asks us to exit

      if (olsr_win32_end_request)
        break;
#endif
      
    }//end for

#if defined WIN32
  // tell the Ctrl-C signal handler thread that we have exited

  olsr_win32_end_flag = TRUE;

  // the Ctrl-C signal handler thread will exit the process and
  // hence also kill us
  
  while (1)
    Sleep(1000);
#endif
}


/*
 *
 *@param initial how long utnil the first generation
 *@param trigger pointer to a boolean indicating that
 *this function should be triggered immediatley
 */
int
olsr_register_scheduler_event(void (*event_function)(void *), 
			      void *par,
			      float interval, 
			      float initial, 
			      olsr_u8_t *trigger)
{
  struct event_entry *new_entry;

  OLSR_PRINTF(3, "Scheduler event registered int: %0.2f\n", interval);

  /* check that this entry is not added already */
  new_entry = event_functions;
  while(new_entry)
    {
      if((new_entry->function == event_function) &&
	 (new_entry->param == par) &&
	 (new_entry->trigger == trigger) &&
	 (new_entry->interval == interval))
	{
	  fprintf(stderr, "Register scheduler event: Event alread registered!\n");
	  olsr_syslog(OLSR_LOG_ERR, "Register scheduler event: Event alread registered!\n");
	  return 0;
	}
      new_entry = new_entry->next;
    }

  new_entry = olsr_malloc(sizeof(struct event_entry), "add scheduler event");

  new_entry->function = event_function;
  new_entry->param = par;
  new_entry->interval = interval;
  new_entry->since_last = interval - initial;
  new_entry->next = event_functions;
  new_entry->trigger = trigger;

  event_functions = new_entry;

  return 1;
}



/*
 *
 *@param initial how long until the first generation
 *@param trigger pointer to a boolean indicating that
 *this function should be triggered immediatley
 */
int
olsr_remove_scheduler_event(void (*event_function)(void *), 
			    void *par,
			    float interval, 
			    float initial __attribute__((unused)), 
			    olsr_u8_t *trigger)
{
  struct event_entry *entry, *prev;

  prev = NULL;
  entry = event_functions;

  while(entry)
    {
      if((entry->function == event_function) &&
	 (entry->param == par) &&
	 (entry->trigger == trigger) &&
	 (0.0 > interval || entry->interval == interval))
	{
	  if(entry == event_functions)
	    {
	      event_functions = entry->next;
	    }
	  else
	    {
	      prev->next = entry->next;
	    }
          free(entry);
	  return 1;
	}

      prev = entry;
      entry = entry->next;
    }

  return 0;
}

/*
 * Sven-Ola, 2007: Since the original timing and flagging is changed (which
 * saves lots of CPU time - see LinkQualityDijkstraLimit) the original timeout
 * functions called every olsr_cnf->polltime uses too much CPU now. Because the
 * changes_xxx handling is switched off with LQDL, it should be OK to call
 * all timeout handlers at a much lower rate. To overcome UDP packet loss,
 * a very low pollrate is used.
 */

static float dijkstra_initial = 0.0;

int
olsr_register_scheduler_event_dijkstra(void (*event_function)(void *), 
			      void *par,
			      float interval, 
			      float initial, 
			      olsr_u8_t *trigger)
{
  if (1 < olsr_cnf->lq_level && 0.0 < olsr_cnf->lq_dinter)
  {
    dijkstra_initial += olsr_cnf->lq_dinter / 10.0;
    return olsr_register_scheduler_event(event_function, par, olsr_cnf->lq_dinter, dijkstra_initial, trigger);
  }
  return olsr_register_scheduler_event(event_function, par, interval, initial, trigger);
}

int
olsr_register_timeout_function(void (*time_out_function)(void), olsr_bool dijkstra_limit_ok)
{
  struct timeout_entry *new_entry;

  if (dijkstra_limit_ok && 1 < olsr_cnf->lq_level && 0.0 < olsr_cnf->lq_dinter)
  {
    dijkstra_initial += olsr_cnf->lq_dinter / 10.0;
    return olsr_register_scheduler_event(
      (void *)time_out_function,
      NULL,
      olsr_cnf->lq_dinter,
      dijkstra_initial,
      NULL);
  }
  
  /* check that this entry is not added already */
  new_entry = timeout_functions;
  while(new_entry)
    {
      if(new_entry->function == time_out_function)
	{
	  fprintf(stderr, "Register scheduler timeout: Event alread registered!\n");
	  olsr_syslog(OLSR_LOG_ERR, "Register scheduler timeout: Event alread registered!\n");
	  return 0;
	}
      new_entry = new_entry->next;
    }

  new_entry = olsr_malloc(sizeof(struct timeout_entry), "scheduler add timeout");

  new_entry->function = time_out_function;
  new_entry->next = timeout_functions;

  timeout_functions = new_entry;

  return 1;
}



int
olsr_remove_timeout_function(void (*time_out_function)(void), olsr_bool dijkstra_limit_ok)
{
  struct timeout_entry *entry, *prev;

  if (dijkstra_limit_ok && 1 < olsr_cnf->lq_level && 0.0 < olsr_cnf->lq_dinter)
  {
    return olsr_remove_scheduler_event(
      (void *)time_out_function,
      NULL,
      -1.0,
      -1.0,
      NULL);
  }
  
  /* check that this entry is not added already */
  entry = timeout_functions;
  prev = NULL;

  while(entry)
    {
      if(entry->function == time_out_function)
	{
	  if(entry == timeout_functions)
	    {
	      timeout_functions = entry->next;
	    }
	  else
	    {
	      prev->next = entry->next;
	    }
	  free(entry);
	  return 1;
	}
      prev = entry;
      entry = entry->next;
    }

  return 0;
}


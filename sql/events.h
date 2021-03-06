#ifndef _EVENT_H_
#define _EVENT_H_
/* Copyright (c) 2004, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @defgroup Event_Scheduler Event Scheduler
  @ingroup Runtime_Environment
  @{

  @file sql/events.h

  A public interface of Events_Scheduler module.
*/

#include <stddef.h>
#include <sys/types.h>

#include "lex_string.h"
#include "my_inttypes.h"
#include "my_psi_config.h"
#include "my_time.h"                            /* interval_type */
#include "mysql/mysql_lex_string.h"             // LEX_STRING
#include "mysql/psi/psi_base.h"
#include "mysql/psi/psi_memory.h"               // PSI_memory_key
#include "mysql/psi/psi_stage.h"                // PSI_stage_info

class Event_db_repository;
class Event_parse_data;
class Event_queue;
class Event_scheduler;
class Item;
class String;
class THD;
struct TABLE_LIST;

namespace dd {
  class Schema;
}

typedef struct charset_info_st CHARSET_INFO;

#ifdef HAVE_PSI_INTERFACE
extern PSI_mutex_key key_event_scheduler_LOCK_scheduler_state;
extern PSI_cond_key key_event_scheduler_COND_state;
extern PSI_thread_key key_thread_event_scheduler, key_thread_event_worker;
#endif /* HAVE_PSI_INTERFACE */

extern PSI_memory_key key_memory_event_basic_root;

/* Always defined, for SHOW PROCESSLIST. */
extern PSI_stage_info stage_waiting_on_empty_queue;
extern PSI_stage_info stage_waiting_for_next_activation;
extern PSI_stage_info stage_waiting_for_scheduler_to_stop;

int
sortcmp_lex_string(LEX_STRING s, LEX_STRING t, CHARSET_INFO *cs);


/**
  Convert name to lowercase.

  @param from the string to be converted to lowercase.
  @param to   Buffer space for the coverted lowercase string.
  @param len  Maximum length of the buffer.
*/

void convert_name_lowercase(const char *from, char *to, size_t len);


/**
  @brief A facade to the functionality of the Event Scheduler.

  Every public operation against the scheduler has to be executed via the
  interface provided by a static method of this class. No instance of this
  class is ever created and it has no non-static data members.

  The life cycle of the Events module is the following:

  At server start up:
     init_mutexes() -> init()
  When the server is running:
     create_event(), drop_event(), start_or_stop_event_scheduler(), etc
  At shutdown:
     deinit(), destroy_mutexes().

  The peculiar initialization and shutdown cycle is an adaptation to the
  outside server startup/shutdown framework and mimics the rest of MySQL
  subsystems (ACL, time zone tables, etc).
*/

class Events
{
public:
  /*
    the following block is to support --event-scheduler command line option
    and the @@global.event_scheduler SQL variable.
    See sys_var.cc
  */
  enum enum_opt_event_scheduler { EVENTS_OFF, EVENTS_ON, EVENTS_DISABLED };
  /* Protected using LOCK_global_system_variables only. */
  static ulong opt_event_scheduler;
  static bool start(int *err_no);
  static bool stop();

  /* A hack needed for Event_queue_element */
  static Event_db_repository *get_db_repository() { return db_repository; }

  static bool init(bool opt_noacl);

  static void deinit();

  static void init_mutexes();

  static bool create_event(THD *thd, Event_parse_data *parse_data,
                           bool if_exists);

  static bool update_event(THD *thd, Event_parse_data *parse_data,
                           LEX_STRING *new_dbname, LEX_STRING *new_name);

  static bool drop_event(THD *thd, LEX_STRING dbname, LEX_STRING name,
                         bool if_exists);

  static bool lock_schema_events(THD *thd, const dd::Schema &schema);

  static bool drop_schema_events(THD *thd, const dd::Schema &schema);

  static bool show_create_event(THD *thd, LEX_STRING dbname, LEX_STRING name);

  /* Needed for both SHOW CREATE EVENT and INFORMATION_SCHEMA */
  static int reconstruct_interval_expression(String *buf,
                                             interval_type interval,
                                             longlong expression);

  static void dump_internal_status();

  Events(const Events &)= delete;
  void operator=(Events &)= delete;

private:
  static Event_queue         *event_queue;
  static Event_scheduler     *scheduler;
  static Event_db_repository *db_repository;
};

/**
  @} (end of group Event Scheduler)
*/

#endif /* _EVENT_H_ */

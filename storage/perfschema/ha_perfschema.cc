/* Copyright (c) 2008, 2016, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

/**
  @file storage/perfschema/ha_perfschema.cc
  Performance schema storage engine (implementation).
*/

#include "my_global.h"
#include "my_thread.h"
#include "my_atomic.h"
#include "hostname.h"
#include "mysqld.h"
#include "sql_class.h"
#include "sql_plugin.h"
#include "mysql/plugin.h"
#include "ha_perfschema.h"
#include "pfs_engine_table.h"
#include "pfs_column_values.h"
#include "pfs_instr_class.h"
#include "pfs_instr.h"
#include "pfs_account.h"
#include "pfs_host.h"
#include "pfs_user.h"
#include "pfs_program.h"
#include "pfs_prepared_stmt.h"
#include "pfs_buffer_container.h"

handlerton *pfs_hton= NULL;

#define PFS_ENABLED() (pfs_initialized && (pfs_enabled || m_table_share->m_perpetual))

static handler* pfs_create_handler(handlerton *hton,
                                   TABLE_SHARE *table,
                                   MEM_ROOT *mem_root)
{
  return new (mem_root) ha_perfschema(hton, table);
}

/**
  SHOW ENGINE PERFORMANCE_SCHEMA STATUS.
  @param hton               Storage engine handler
  @param thd                Current thread
  @param print              Print function
  @param stat               status to show
*/
static bool pfs_show_status(handlerton *hton, THD *thd,
                            stat_print_fn *print, enum ha_stat_type stat)
{
  char buf[1024];
  uint buflen;
  const char *name;
  int i;
  size_t size;

  DBUG_ENTER("pfs_show_status");

  /*
    Note about naming conventions:
    - Internal buffers exposed as a table in the performance schema are named
    after the table, as in 'events_waits_current'
    - Internal buffers not exposed by a table are named with parenthesis,
    as in '(pfs_mutex_class)'.
  */
  if (stat != HA_ENGINE_STATUS)
    DBUG_RETURN(false);

  size_t total_memory= 0;

  for (i=0; /* empty */; i++)
  {
    switch (i){
    case 0:
      name= "events_waits_current.size";
      size= sizeof(PFS_events_waits);
      break;
    case 1:
      name= "events_waits_current.count";
      size= WAIT_STACK_SIZE * global_thread_container.get_row_count();
      break;
    case 2:
      name= "events_waits_history.size";
      size= sizeof(PFS_events_waits);
      break;
    case 3:
      name= "events_waits_history.count";
      size= events_waits_history_per_thread * global_thread_container.get_row_count();
      break;
    case 4:
      name= "events_waits_history.memory";
      size= events_waits_history_per_thread * global_thread_container.get_row_count()
        * sizeof(PFS_events_waits);
      total_memory+= size;
      break;
    case 5:
      name= "events_waits_history_long.size";
      size= sizeof(PFS_events_waits);
      break;
    case 6:
      name= "events_waits_history_long.count";
      size= events_waits_history_long_size;
      break;
    case 7:
      name= "events_waits_history_long.memory";
      size= events_waits_history_long_size * sizeof(PFS_events_waits);
      total_memory+= size;
      break;
    case 8:
      name= "(pfs_mutex_class).size";
      size= sizeof(PFS_mutex_class);
      break;
    case 9:
      name= "(pfs_mutex_class).count";
      size= mutex_class_max;
      break;
    case 10:
      name= "(pfs_mutex_class).memory";
      size= mutex_class_max * sizeof(PFS_mutex_class);
      total_memory+= size;
      break;
    case 11:
      name= "(pfs_rwlock_class).size";
      size= sizeof(PFS_rwlock_class);
      break;
    case 12:
      name= "(pfs_rwlock_class).count";
      size= rwlock_class_max;
      break;
    case 13:
      name= "(pfs_rwlock_class).memory";
      size= rwlock_class_max * sizeof(PFS_rwlock_class);
      total_memory+= size;
      break;
    case 14:
      name= "(pfs_cond_class).size";
      size= sizeof(PFS_cond_class);
      break;
    case 15:
      name= "(pfs_cond_class).count";
      size= cond_class_max;
      break;
    case 16:
      name= "(pfs_cond_class).memory";
      size= cond_class_max * sizeof(PFS_cond_class);
      total_memory+= size;
      break;
    case 17:
      name= "(pfs_thread_class).size";
      size= sizeof(PFS_thread_class);
      break;
    case 18:
      name= "(pfs_thread_class).count";
      size= thread_class_max;
      break;
    case 19:
      name= "(pfs_thread_class).memory";
      size= thread_class_max * sizeof(PFS_thread_class);
      total_memory+= size;
      break;
    case 20:
      name= "(pfs_file_class).size";
      size= sizeof(PFS_file_class);
      break;
    case 21:
      name= "(pfs_file_class).count";
      size= file_class_max;
      break;
    case 22:
      name= "(pfs_file_class).memory";
      size= file_class_max * sizeof(PFS_file_class);
      total_memory+= size;
      break;
    case 23:
      name= "mutex_instances.size";
      size= global_mutex_container.get_row_size();
      break;
    case 24:
      name= "mutex_instances.count";
      size= global_mutex_container.get_row_count();
      break;
    case 25:
      name= "mutex_instances.memory";
      size= global_mutex_container.get_memory();
      total_memory+= size;
      break;
    case 26:
      name= "rwlock_instances.size";
      size= global_rwlock_container.get_row_size();
      break;
    case 27:
      name= "rwlock_instances.count";
      size= global_rwlock_container.get_row_count();
      break;
    case 28:
      name= "rwlock_instances.memory";
      size= global_rwlock_container.get_memory();
      total_memory+= size;
      break;
    case 29:
      name= "cond_instances.size";
      size= global_cond_container.get_row_size();
      break;
    case 30:
      name= "cond_instances.count";
      size= global_cond_container.get_row_count();
      break;
    case 31:
      name= "cond_instances.memory";
      size= global_cond_container.get_memory();
      total_memory+= size;
      break;
    case 32:
      name= "threads.size";
      size= global_thread_container.get_row_size();
      break;
    case 33:
      name= "threads.count";
      size= global_thread_container.get_row_count();
      break;
    case 34:
      name= "threads.memory";
      size= global_thread_container.get_memory();
      total_memory+= size;
      break;
    case 35:
      name= "file_instances.size";
      size= global_file_container.get_row_size();
      break;
    case 36:
      name= "file_instances.count";
      size= global_file_container.get_row_count();
      break;
    case 37:
      name= "file_instances.memory";
      size= global_file_container.get_memory();
      total_memory+= size;
      break;
    case 38:
      name= "(pfs_file_handle).size";
      size= sizeof(PFS_file*);
      break;
    case 39:
      name= "(pfs_file_handle).count";
      size= file_handle_max;
      break;
    case 40:
      name= "(pfs_file_handle).memory";
      size= file_handle_max * sizeof(PFS_file*);
      total_memory+= size;
      break;
    case 41:
      name= "events_waits_summary_by_thread_by_event_name.size";
      size= sizeof(PFS_single_stat);
      break;
    case 42:
      name= "events_waits_summary_by_thread_by_event_name.count";
      size= global_thread_container.get_row_count() * wait_class_max;
      break;
    case 43:
      name= "events_waits_summary_by_thread_by_event_name.memory";
      size= global_thread_container.get_row_count() * wait_class_max * sizeof(PFS_single_stat);
      total_memory+= size;
      break;
    case 44:
      name= "(pfs_table_share).size";
      size= global_table_share_container.get_row_size();
      break;
    case 45:
      name= "(pfs_table_share).count";
      size= global_table_share_container.get_row_count();
      break;
    case 46:
      name= "(pfs_table_share).memory";
      size= global_table_share_container.get_memory();
      total_memory+= size;
      break;
    case 47:
      name= "(pfs_table).size";
      size= global_table_container.get_row_size();
      break;
    case 48:
      name= "(pfs_table).count";
      size= global_table_container.get_row_count();
      break;
    case 49:
      name= "(pfs_table).memory";
      size= global_table_container.get_memory();
      total_memory+= size;
      break;
    case 50:
      name= "setup_actors.size";
      size= global_setup_actor_container.get_row_size();
      break;
    case 51:
      name= "setup_actors.count";
      size= global_setup_actor_container.get_row_count();
      break;
    case 52:
      name= "setup_actors.memory";
      size= global_setup_actor_container.get_memory();
      total_memory+= size;
      break;
    case 53:
      name= "setup_objects.size";
      size= global_setup_object_container.get_row_size();
      break;
    case 54:
      name= "setup_objects.count";
      size= global_setup_object_container.get_row_count();
      break;
    case 55:
      name= "setup_objects.memory";
      size= global_setup_object_container.get_memory();
      total_memory+= size;
      break;
    case 56:
      name= "(pfs_account).size";
      size= global_account_container.get_row_size();
      break;
    case 57:
      name= "(pfs_account).count";
      size= global_account_container.get_row_count();
      break;
    case 58:
      name= "(pfs_account).memory";
      size= global_account_container.get_memory();
      total_memory+= size;
      break;
    case 59:
      name= "events_waits_summary_by_account_by_event_name.size";
      size= sizeof(PFS_single_stat);
      break;
    case 60:
      name= "events_waits_summary_by_account_by_event_name.count";
      size= global_account_container.get_row_count() * wait_class_max;
      break;
    case 61:
      name= "events_waits_summary_by_account_by_event_name.memory";
      size= global_account_container.get_row_count() * wait_class_max * sizeof(PFS_single_stat);
      total_memory+= size;
      break;
    case 62:
      name= "events_waits_summary_by_user_by_event_name.size";
      size= sizeof(PFS_single_stat);
      break;
    case 63:
      name= "events_waits_summary_by_user_by_event_name.count";
      size= global_user_container.get_row_count() * wait_class_max;
      break;
    case 64:
      name= "events_waits_summary_by_user_by_event_name.memory";
      size= global_user_container.get_row_count() * wait_class_max * sizeof(PFS_single_stat);
      total_memory+= size;
      break;
    case 65:
      name= "events_waits_summary_by_host_by_event_name.size";
      size= sizeof(PFS_single_stat);
      break;
    case 66:
      name= "events_waits_summary_by_host_by_event_name.count";
      size= global_host_container.get_row_count() * wait_class_max;
      break;
    case 67:
      name= "events_waits_summary_by_host_by_event_name.memory";
      size= global_host_container.get_row_count() * wait_class_max * sizeof(PFS_single_stat);
      total_memory+= size;
      break;
    case 68:
      name= "(pfs_user).size";
      size= global_user_container.get_row_size();
      break;
    case 69:
      name= "(pfs_user).count";
      size= global_user_container.get_row_count();
      break;
    case 70:
      name= "(pfs_user).memory";
      size= global_user_container.get_memory();
      total_memory+= size;
      break;
    case 71:
      name= "(pfs_host).size";
      size= global_host_container.get_row_size();
      break;
    case 72:
      name= "(pfs_host).count";
      size= global_host_container.get_row_count();
      break;
    case 73:
      name= "(pfs_host).memory";
      size= global_host_container.get_memory();
      total_memory+= size;
      break;
    case 74:
      name= "(pfs_stage_class).size";
      size= sizeof(PFS_stage_class);
      break;
    case 75:
      name= "(pfs_stage_class).count";
      size= stage_class_max;
      break;
    case 76:
      name= "(pfs_stage_class).memory";
      size= stage_class_max * sizeof(PFS_stage_class);
      total_memory+= size;
      break;
    case 77:
      name= "events_stages_history.size";
      size= sizeof(PFS_events_stages);
      break;
    case 78:
      name= "events_stages_history.count";
      size= events_stages_history_per_thread * global_thread_container.get_row_count();
      break;
    case 79:
      name= "events_stages_history.memory";
      size= events_stages_history_per_thread * global_thread_container.get_row_count()
        * sizeof(PFS_events_stages);
      total_memory+= size;
      break;
    case 80:
      name= "events_stages_history_long.size";
      size= sizeof(PFS_events_stages);
      break;
    case 81:
      name= "events_stages_history_long.count";
      size= events_stages_history_long_size;
      break;
    case 82:
      name= "events_stages_history_long.memory";
      size= events_stages_history_long_size * sizeof(PFS_events_stages);
      total_memory+= size;
      break;
    case 83:
      name= "events_stages_summary_by_thread_by_event_name.size";
      size= sizeof(PFS_stage_stat);
      break;
    case 84:
      name= "events_stages_summary_by_thread_by_event_name.count";
      size= global_thread_container.get_row_count() * stage_class_max;
      break;
    case 85:
      name= "events_stages_summary_by_thread_by_event_name.memory";
      size= global_thread_container.get_row_count() * stage_class_max * sizeof(PFS_stage_stat);
      total_memory+= size;
      break;
    case 86:
      name= "events_stages_summary_global_by_event_name.size";
      size= sizeof(PFS_stage_stat);
      break;
    case 87:
      name= "events_stages_summary_global_by_event_name.count";
      size= stage_class_max;
      break;
    case 88:
      name= "events_stages_summary_global_by_event_name.memory";
      size= stage_class_max * sizeof(PFS_stage_stat);
      total_memory+= size;
      break;
    case 89:
      name= "events_stages_summary_by_account_by_event_name.size";
      size= sizeof(PFS_stage_stat);
      break;
    case 90:
      name= "events_stages_summary_by_account_by_event_name.count";
      size= global_account_container.get_row_count() * stage_class_max;
      break;
    case 91:
      name= "events_stages_summary_by_account_by_event_name.memory";
      size= global_account_container.get_row_count() * stage_class_max * sizeof(PFS_stage_stat);
      total_memory+= size;
      break;
    case 92:
      name= "events_stages_summary_by_user_by_event_name.size";
      size= sizeof(PFS_stage_stat);
      break;
    case 93:
      name= "events_stages_summary_by_user_by_event_name.count";
      size= global_user_container.get_row_count() * stage_class_max;
      break;
    case 94:
      name= "events_stages_summary_by_user_by_event_name.memory";
      size= global_user_container.get_row_count() * stage_class_max * sizeof(PFS_stage_stat);
      total_memory+= size;
      break;
    case 95:
      name= "events_stages_summary_by_host_by_event_name.size";
      size= sizeof(PFS_stage_stat);
      break;
    case 96:
      name= "events_stages_summary_by_host_by_event_name.count";
      size= global_host_container.get_row_count() * stage_class_max;
      break;
    case 97:
      name= "events_stages_summary_by_host_by_event_name.memory";
      size= global_host_container.get_row_count() * stage_class_max * sizeof(PFS_stage_stat);
      total_memory+= size;
      break;
    case 98:
      name= "(pfs_statement_class).size";
      size= sizeof(PFS_statement_class);
      break;
    case 99:
      name= "(pfs_statement_class).count";
      size= statement_class_max;
      break;
    case 100:
      name= "(pfs_statement_class).memory";
      size= statement_class_max * sizeof(PFS_statement_class);
      total_memory+= size;
      break;
    case 101:
      name= "events_statements_history.size";
      size= sizeof(PFS_events_statements);
      break;
    case 102:
      name= "events_statements_history.count";
      size= events_statements_history_per_thread * global_thread_container.get_row_count();
      break;
    case 103:
      name= "events_statements_history.memory";
      size= events_statements_history_per_thread * global_thread_container.get_row_count()
        * sizeof(PFS_events_statements);
      total_memory+= size;
      break;
    case 104:
      name= "events_statements_history_long.size";
      size= sizeof(PFS_events_statements);
      break;
    case 105:
      name= "events_statements_history_long.count";
      size= events_statements_history_long_size;
      break;
    case 106:
      name= "events_statements_history_long.memory";
      size= events_statements_history_long_size * (sizeof(PFS_events_statements));
      total_memory+= size;
      break;
    case 107:
      name= "events_statements_summary_by_thread_by_event_name.size";
      size= sizeof(PFS_statement_stat);
      break;
    case 108:
      name= "events_statements_summary_by_thread_by_event_name.count";
      size= global_thread_container.get_row_count() * statement_class_max;
      break;
    case 109:
      name= "events_statements_summary_by_thread_by_event_name.memory";
      size= global_thread_container.get_row_count() * statement_class_max * sizeof(PFS_statement_stat);
      total_memory+= size;
      break;
    case 110:
      name= "events_statements_summary_global_by_event_name.size";
      size= sizeof(PFS_statement_stat);
      break;
    case 111:
      name= "events_statements_summary_global_by_event_name.count";
      size= statement_class_max;
      break;
    case 112:
      name= "events_statements_summary_global_by_event_name.memory";
      size= statement_class_max * sizeof(PFS_statement_stat);
      total_memory+= size;
      break;
    case 113:
      name= "events_statements_summary_by_account_by_event_name.size";
      size= sizeof(PFS_statement_stat);
      break;
    case 114:
      name= "events_statements_summary_by_account_by_event_name.count";
      size= global_account_container.get_row_count() * statement_class_max;
      break;
    case 115:
      name= "events_statements_summary_by_account_by_event_name.memory";
      size= global_account_container.get_row_count() * statement_class_max * sizeof(PFS_statement_stat);
      total_memory+= size;
      break;
    case 116:
      name= "events_statements_summary_by_user_by_event_name.size";
      size= sizeof(PFS_statement_stat);
      break;
    case 117:
      name= "events_statements_summary_by_user_by_event_name.count";
      size= global_user_container.get_row_count() * statement_class_max;
      break;
    case 118:
      name= "events_statements_summary_by_user_by_event_name.memory";
      size= global_user_container.get_row_count() * statement_class_max * sizeof(PFS_statement_stat);
      total_memory+= size;
      break;
    case 119:
      name= "events_statements_summary_by_host_by_event_name.size";
      size= sizeof(PFS_statement_stat);
      break;
    case 120:
      name= "events_statements_summary_by_host_by_event_name.count";
      size= global_host_container.get_row_count() * statement_class_max;
      break;
    case 121:
      name= "events_statements_summary_by_host_by_event_name.memory";
      size= global_host_container.get_row_count() * statement_class_max * sizeof(PFS_statement_stat);
      total_memory+= size;
      break;
    case 122:
      name= "events_statements_current.size";
      size= sizeof(PFS_events_statements);
      break;
    case 123:
      name= "events_statements_current.count";
      size= global_thread_container.get_row_count() * statement_stack_max;
      break;
    case 124:
      name= "events_statements_current.memory";
      size= global_thread_container.get_row_count() * statement_stack_max * sizeof(PFS_events_statements);
      total_memory+= size;
      break;
    case 125:
      name= "(pfs_socket_class).size";
      size= sizeof(PFS_socket_class);
      break;
    case 126:
      name= "(pfs_socket_class).count";
      size= socket_class_max;
      break;
    case 127:
      name= "(pfs_socket_class).memory";
      size= socket_class_max * sizeof(PFS_socket_class);
      total_memory+= size;
      break;
    case 128:
      name= "socket_instances.size";
      size= global_socket_container.get_row_size();
      break;
    case 129:
      name= "socket_instances.count";
      size= global_socket_container.get_row_count();
      break;
    case 130:
      name= "socket_instances.memory";
      size= global_socket_container.get_memory();
      total_memory+= size;
      break;
    case 131:
      name= "events_statements_summary_by_digest.size";
      size= sizeof(PFS_statements_digest_stat);
      break;
    case 132:
      name= "events_statements_summary_by_digest.count";
      size= digest_max;
      break;
    case 133:
      name= "events_statements_summary_by_digest.memory";
      size= digest_max * (sizeof(PFS_statements_digest_stat));
      total_memory+= size;
      break;
    case 134:
      name= "events_statements_summary_by_program.size";
      size= global_program_container.get_row_size();
      break;
    case 135:
      name= "events_statements_summary_by_program.count";
      size= global_program_container.get_row_count();
      break;
    case 136:
      name= "events_statements_summary_by_program.memory";
      size= global_program_container.get_memory();
      total_memory+= size;
      break;
    case 137:
      name= "session_connect_attrs.size";
      size= global_thread_container.get_row_count();
      break;
    case 138:
      name= "session_connect_attrs.count";
      size= session_connect_attrs_size_per_thread;
      break;
    case 139:
      name= "session_connect_attrs.memory";
      size= global_thread_container.get_row_count() * session_connect_attrs_size_per_thread;
      total_memory+= size;
      break;
    case 140:
      name= "prepared_statements_instances.size";
      size= global_prepared_stmt_container.get_row_size();
      break;
    case 141:
      name= "prepared_statements_instances.count";
      size= global_prepared_stmt_container.get_row_count();
      break;
    case 142:
      name= "prepared_statements_instances.memory";
      size= global_prepared_stmt_container.get_memory();
      total_memory+= size;
      break;

    case 143:
      name= "(account_hash).count";
      size= account_hash.count;
      break;
    case 144:
      name= "(account_hash).size";
      size= account_hash.size;
      break;
    case 145:
      name= "(digest_hash).count";
      size= digest_hash.count;
      break;
    case 146:
      name= "(digest_hash).size";
      size= digest_hash.size;
      break;
    case 147:
      name= "(filename_hash).count";
      size= filename_hash.count;
      break;
    case 148:
      name= "(filename_hash).size";
      size= filename_hash.size;
      break;
    case 149:
      name= "(host_hash).count";
      size= host_hash.count;
      break;
    case 150:
      name= "(host_hash).size";
      size= host_hash.size;
      break;
    case 151:
      name= "(setup_actor_hash).count";
      size= setup_actor_hash.count;
      break;
    case 152:
      name= "(setup_actor_hash).size";
      size= setup_actor_hash.size;
      break;
    case 153:
      name= "(setup_object_hash).count";
      size= setup_object_hash.count;
      break;
    case 154:
      name= "(setup_object_hash).size";
      size= setup_object_hash.size;
      break;
    case 155:
      name= "(table_share_hash).count";
      size= table_share_hash.count;
      break;
    case 156:
      name= "(table_share_hash).size";
      size= table_share_hash.size;
      break;
    case 157:
      name= "(user_hash).count";
      size= user_hash.count;
      break;
    case 158:
      name= "(user_hash).size";
      size= user_hash.size;
      break;
    case 159:
      name= "(program_hash).count";
      size= program_hash.count;
      break;
    case 160:
      name= "(program_hash).size";
      size= program_hash.size;
      break;
    case 161:
      /*
        This is not a performance_schema buffer,
        the data is maintained in the server,
        in hostname_cache.
        Print the size only, there are:
        - no host_cache.count
        - no host_cache.memory
      */
      name= "host_cache.size";
      size= sizeof(Host_entry);
      break;

    case 162:
      name= "(pfs_memory_class).row_size";
      size= sizeof(PFS_memory_class);
      break;
    case 163:
      name= "(pfs_memory_class).row_count";
      size= memory_class_max;
      break;
    case 164:
      name= "(pfs_memory_class).memory";
      size= memory_class_max * sizeof(PFS_memory_class);
      total_memory+= size;
      break;

    case 165:
      name= "memory_summary_by_thread_by_event_name.row_size";
      size= sizeof(PFS_memory_stat);
      break;
    case 166:
      name= "memory_summary_by_thread_by_event_name.row_count";
      size= global_thread_container.get_row_count() * memory_class_max;
      break;
    case 167:
      name= "memory_summary_by_thread_by_event_name.memory";
      size= global_thread_container.get_row_count() * memory_class_max * sizeof(PFS_memory_stat);
      total_memory+= size;
      break;
    case 168:
      name= "memory_summary_global_by_event_name.row_size";
      size= sizeof(PFS_memory_stat);
      break;
    case 169:
      name= "memory_summary_global_by_event_name.row_count";
      size= memory_class_max;
      break;
    case 170:
      name= "memory_summary_global_by_event_name.memory";
      size= memory_class_max * sizeof(PFS_memory_stat);
      total_memory+= size;
      break;
    case 171:
      name= "memory_summary_by_account_by_event_name.row_size";
      size= sizeof(PFS_memory_stat);
      break;
    case 172:
      name= "memory_summary_by_account_by_event_name.row_count";
      size= global_account_container.get_row_count() * memory_class_max;
      break;
    case 173:
      name= "memory_summary_by_account_by_event_name.memory";
      size= global_account_container.get_row_count() * memory_class_max * sizeof(PFS_memory_stat);
      total_memory+= size;
      break;
    case 174:
      name= "memory_summary_by_user_by_event_name.row_size";
      size= sizeof(PFS_memory_stat);
      break;
    case 175:
      name= "memory_summary_by_user_by_event_name.row_count";
      size= global_user_container.get_row_count() * memory_class_max;
      break;
    case 176:
      name= "memory_summary_by_user_by_event_name.memory";
      size= global_user_container.get_row_count() * memory_class_max * sizeof(PFS_memory_stat);
      total_memory+= size;
      break;
    case 177:
      name= "memory_summary_by_host_by_event_name.row_size";
      size= sizeof(PFS_memory_stat);
      break;
    case 178:
      name= "memory_summary_by_host_by_event_name.row_count";
      size= global_host_container.get_row_count() * memory_class_max;
      break;
    case 179:
      name= "memory_summary_by_host_by_event_name.memory";
      size= global_host_container.get_row_count() * memory_class_max * sizeof(PFS_memory_stat);
      total_memory+= size;
      break;
    case 180:
      name= "metadata_locks.row_size";
      size= global_mdl_container.get_row_size();
      break;
    case 181:
      name= "metadata_locks.row_count";
      size= global_mdl_container.get_row_count();
      break;
    case 182:
      name= "metadata_locks.memory";
      size= global_mdl_container.get_memory();
      total_memory+= size;
      break;
    case 183:
      name= "events_transactions_history.size";
      size= sizeof(PFS_events_transactions);
      break;
    case 184:
      name= "events_transactions_history.count";
      size= events_transactions_history_per_thread * global_thread_container.get_row_count();
      break;
    case 185:
      name= "events_transactions_history.memory";
      size= events_transactions_history_per_thread * global_thread_container.get_row_count()
        * sizeof(PFS_events_transactions);
      total_memory+= size;
      break;
    case 186:
      name= "events_transactions_history_long.size";
      size= sizeof(PFS_events_transactions);
      break;
    case 187:
      name= "events_transactions_history_long.count";
      size= events_transactions_history_long_size;
      break;
    case 188:
      name= "events_transactions_history_long.memory";
      size= events_transactions_history_long_size * sizeof(PFS_events_transactions);
      total_memory+= size;
      break;
    case 189:
      name= "events_transactions_summary_by_thread_by_event_name.size";
      size= sizeof(PFS_transaction_stat);
      break;
    case 190:
      name= "events_transactions_summary_by_thread_by_event_name.count";
      size= global_thread_container.get_row_count() * transaction_class_max;
      break;
    case 191:
      name= "events_transactions_summary_by_thread_by_event_name.memory";
      size= global_thread_container.get_row_count() * transaction_class_max * sizeof(PFS_transaction_stat);
      total_memory+= size;
      break;
    case 192:
      name= "events_transactions_summary_by_account_by_event_name.size";
      size= sizeof(PFS_transaction_stat);
      break;
    case 193:
      name= "events_transactions_summary_by_account_by_event_name.count";
      size= global_account_container.get_row_count() * transaction_class_max;
      break;
    case 194:
      name= "events_transactions_summary_by_account_by_event_name.memory";
      size= global_account_container.get_row_count() * transaction_class_max * sizeof(PFS_transaction_stat);
      total_memory+= size;
      break;
    case 195:
      name= "events_transactions_summary_by_user_by_event_name.size";
      size= sizeof(PFS_transaction_stat);
      break;
    case 196:
      name= "events_transactions_summary_by_user_by_event_name.count";
      size= global_user_container.get_row_count() * transaction_class_max;
      break;
    case 197:
      name= "events_transactions_summary_by_user_by_event_name.memory";
      size= global_user_container.get_row_count() * transaction_class_max * sizeof(PFS_transaction_stat);
      total_memory+= size;
      break;
    case 198:
      name= "events_transactions_summary_by_host_by_event_name.size";
      size= sizeof(PFS_transaction_stat);
      break;
    case 199:
      name= "events_transactions_summary_by_host_by_event_name.count";
      size= global_host_container.get_row_count() * transaction_class_max;
      break;
    case 200:
      name= "events_transactions_summary_by_host_by_event_name.memory";
      size= global_host_container.get_row_count() * transaction_class_max * sizeof(PFS_transaction_stat);
      total_memory+= size;
      break;
    case 201:
      name= "table_lock_waits_summary_by_table.size";
      size= global_table_share_lock_container.get_row_size();
      break;
    case 202:
      name= "table_lock_waits_summary_by_table.count";
      size= global_table_share_lock_container.get_row_count();
      break;
    case 203:
      name= "table_lock_waits_summary_by_table.memory";
      size= global_table_share_lock_container.get_memory();
      total_memory+= size;
      break;
    case 204:
      name= "table_io_waits_summary_by_index_usage.size";
      size= global_table_share_index_container.get_row_size();
      break;
    case 205:
      name= "table_io_waits_summary_by_index_usage.count";
      size= global_table_share_index_container.get_row_count();
      break;
    case 206:
      name= "table_io_waits_summary_by_index_usage.memory";
      size= global_table_share_index_container.get_memory();
      total_memory+= size;
      break;
    case 207:
      name= "(history_long_statements_digest_token_array).count";
      size= events_statements_history_long_size;
      break;
    case 208:
      name= "(history_long_statements_digest_token_array).size";
      size= pfs_max_digest_length;
      break;
    case 209:
      name= "(history_long_statements_digest_token_array).memory";
      size= events_statements_history_long_size * pfs_max_digest_length;
      total_memory+= size;
      break;
    case 210:
      name= "(history_statements_digest_token_array).count";
      size= global_thread_container.get_row_count() * events_statements_history_per_thread;
      break;
    case 211:
      name= "(history_statements_digest_token_array).size";
      size= pfs_max_digest_length;
      break;
    case 212:
      name= "(history_statements_digest_token_array).memory";
      size= global_thread_container.get_row_count() * events_statements_history_per_thread * pfs_max_digest_length;
      total_memory+= size;
      break;
    case 213:
      name= "(current_statements_digest_token_array).count";
      size= global_thread_container.get_row_count() * statement_stack_max;
      break;
    case 214:
      name= "(current_statements_digest_token_array).size";
      size= pfs_max_digest_length;
      break;
    case 215:
      name= "(current_statements_digest_token_array).memory";
      size= global_thread_container.get_row_count() * statement_stack_max * pfs_max_digest_length;
      total_memory+= size;
      break;
    case 216:
      name= "(history_long_statements_text_array).count";
      size= events_statements_history_long_size;
      break;
    case 217:
      name= "(history_long_statements_text_array).size";
      size= pfs_max_sqltext;
      break;
    case 218:
      name= "(history_long_statements_text_array).memory";
      size= events_statements_history_long_size * pfs_max_sqltext;
      total_memory+= size;
      break;
    case 219:
      name= "(history_statements_text_array).count";
      size= global_thread_container.get_row_count() * events_statements_history_per_thread;
      break;
    case 220:
      name= "(history_statements_text_array).size";
      size= pfs_max_sqltext;
      break;
    case 221:
      name= "(history_statements_text_array).memory";
      size= global_thread_container.get_row_count() * events_statements_history_per_thread * pfs_max_sqltext;
      total_memory+= size;
      break;
    case 222:
      name= "(current_statements_text_array).count";
      size= global_thread_container.get_row_count() * statement_stack_max;
      break;
    case 223:
      name= "(current_statements_text_array).size";
      size= pfs_max_sqltext;
      break;
    case 224:
      name= "(current_statements_text_array).memory";
      size= global_thread_container.get_row_count() * statement_stack_max * pfs_max_sqltext;
      total_memory+= size;
      break;
    case 225:
      name= "(statements_digest_token_array).count";
      size= digest_max;
      break;
    case 226:
      name= "(statements_digest_token_array).size";
      size= pfs_max_digest_length;
      break;
    case 227:
      name= "(statements_digest_token_array).memory";
      size= digest_max * pfs_max_digest_length;
      total_memory+= size;
      break;
    case 228:
      name= "events_error_summary_by_thread_by_error.size";
      size= sizeof(PFS_error_stat);
      break;
    case 229:
      name= "events_error_summary_by_thread_by_error.count";
      size= global_thread_container.get_row_count() * error_class_max;
      break;
    case 230:
      name= "events_error_summary_by_thread_by_error.memory";
      size= global_thread_container.get_row_count() * error_class_max * sizeof(PFS_error_stat);
      total_memory+= size;
      break;
    case 231:
      name= "events_error_summary_by_account_by_error.size";
      size= sizeof(PFS_error_stat);
      break;
    case 232:
      name= "events_error_summary_by_account_by_error.count";
      size= global_account_container.get_row_count() * error_class_max;
      break;
    case 233:
      name= "events_error_summary_by_account_by_error.memory";
      size= global_account_container.get_row_count() * error_class_max * sizeof(PFS_error_stat);
      total_memory+= size;
      break;
    case 234:
      name= "events_error_summary_by_user_by_error.size";
      size= sizeof(PFS_error_stat);
      break;
    case 235:
      name= "events_error_summary_by_user_by_error.count";
      size= global_user_container.get_row_count() * error_class_max;
      break;
    case 236:
      name= "events_error_summary_by_user_by_error.memory";
      size= global_user_container.get_row_count() * error_class_max * sizeof(PFS_error_stat);
      total_memory+= size;
      break;
    case 237:
      name= "events_error_summary_by_host_by_error.size";
      size= sizeof(PFS_error_stat);
      break;
    case 238:
      name= "events_error_summary_by_host_by_error.count";
      size= global_host_container.get_row_count() * error_class_max;
      break;
    case 239:
      name= "events_error_summary_by_host_by_error.memory";
      size= global_host_container.get_row_count() * error_class_max * sizeof(PFS_error_stat);
      total_memory+= size;
      break;
    case 240:
      name= "(pfs_buffer_scalable_container).count";
      size= builtin_memory_scalable_buffer.m_stat.m_alloc_count - builtin_memory_scalable_buffer.m_stat.m_free_count;
      break;
    case 241:
      name= "(pfs_buffer_scalable_container).memory";
      size= builtin_memory_scalable_buffer.m_stat.m_alloc_size - builtin_memory_scalable_buffer.m_stat.m_free_size;
      total_memory+= size;
      break;
    /*
      This case must be last,
      for aggregation in total_memory.
    */
    case 242:
      name= "performance_schema.memory";
      size= total_memory;
      break;
    default:
      goto end;
      break;
    }

    buflen= (uint)(longlong10_to_str(size, buf, 10) - buf);
    if (print(thd,
              PERFORMANCE_SCHEMA_str.str, PERFORMANCE_SCHEMA_str.length,
              name, strlen(name),
              buf, buflen))
      DBUG_RETURN(true);
  }

end:
  DBUG_RETURN(false);
}

static int compare_database_names(const char *name1, const char *name2)
{
  if (name1 == nullptr || name2 == nullptr)
    return 1;

  if (lower_case_table_names)
    return native_strcasecmp(name1, name2);
  return strcmp(name1, name2);
}

static const PFS_engine_table_share*
find_table_share(const char *db, const char *name)
{
  DBUG_ENTER("find_table_share");

  if (compare_database_names(db, PERFORMANCE_SCHEMA_str.str) != 0)
    DBUG_RETURN(NULL);

  const PFS_engine_table_share* result;
  result= PFS_engine_table::find_engine_table_share(name);
  DBUG_RETURN(result);
}

static int pfs_init_func(void *p)
{
  DBUG_ENTER("pfs_init_func");

  pfs_hton= reinterpret_cast<handlerton *> (p);

  pfs_hton->state= SHOW_OPTION_YES;
  pfs_hton->create= pfs_create_handler;
  pfs_hton->show_status= pfs_show_status;
  pfs_hton->flags= HTON_ALTER_NOT_SUPPORTED |
    HTON_TEMPORARY_NOT_SUPPORTED |
    HTON_NO_PARTITION |
    HTON_NO_BINLOG_ROW_OPT;

  /*
    As long as the server implementation keeps using legacy_db_type,
    as for example in mysql_truncate(),
    we can not rely on the fact that different mysqld process will assign
    consistently the same legacy_db_type for a given storage engine name.
    In particular, using different --loose-skip-xxx options between
    ./mysqld --initialize
    ./mysqld
    creates bogus .frm forms when bootstrapping the performance schema,
    if we rely on ha_initialize_handlerton to assign a really dynamic value.
    To fix this, a dedicated DB_TYPE is officially assigned to
    the performance schema. See Bug#43039.
  */
  pfs_hton->db_type= DB_TYPE_PERFORMANCE_SCHEMA;

  PFS_engine_table_share::init_all_locks();

  DBUG_RETURN(0);
}

static int pfs_done_func(void *p)
{
  DBUG_ENTER("pfs_done_func");

  pfs_hton= NULL;

  PFS_engine_table_share::delete_all_locks();

  DBUG_RETURN(0);
}

static int show_func_mutex_instances_lost(THD *thd, SHOW_VAR *var, char *buff)
{
  var->type= SHOW_LONG;
  var->value= buff;
  long *value= reinterpret_cast<long*>(buff);
  *value= global_mutex_container.get_lost_counter();
  return 0;
}

static struct st_mysql_show_var pfs_status_vars[]=
{
  {"Performance_schema_mutex_classes_lost",
    (char*) &mutex_class_lost, SHOW_LONG_NOFLUSH, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_rwlock_classes_lost",
    (char*) &rwlock_class_lost, SHOW_LONG_NOFLUSH, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_cond_classes_lost",
    (char*) &cond_class_lost, SHOW_LONG_NOFLUSH, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_thread_classes_lost",
    (char*) &thread_class_lost, SHOW_LONG_NOFLUSH, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_file_classes_lost",
    (char*) &file_class_lost, SHOW_LONG_NOFLUSH, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_socket_classes_lost",
    (char*) &socket_class_lost, SHOW_LONG_NOFLUSH, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_memory_classes_lost",
    (char*) &memory_class_lost, SHOW_LONG_NOFLUSH, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_mutex_instances_lost",
    (char*) &show_func_mutex_instances_lost, SHOW_FUNC, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_rwlock_instances_lost",
    (char*) &global_rwlock_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_cond_instances_lost",
    (char*) &global_cond_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_thread_instances_lost",
    (char*) &global_thread_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_file_instances_lost",
    (char*) &global_file_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_file_handles_lost",
    (char*) &file_handle_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_socket_instances_lost",
    (char*) &global_socket_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_locker_lost",
    (char*) &locker_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  /* table shares, can be flushed */
  {"Performance_schema_table_instances_lost",
    (char*) &global_table_share_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  /* table handles, can be flushed */
  {"Performance_schema_table_handles_lost",
    (char*) &global_table_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  /* table lock stats, can be flushed */
  {"Performance_schema_table_lock_stat_lost",
    (char*) &global_table_share_lock_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  /* table index stats, can be flushed */
  {"Performance_schema_index_stat_lost",
    (char*) &global_table_share_index_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_hosts_lost",
    (char*) &global_host_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_users_lost",
    (char*) &global_user_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_accounts_lost",
    (char*) &global_account_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_stage_classes_lost",
    (char*) &stage_class_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_statement_classes_lost",
    (char*) &statement_class_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_digest_lost",
    (char*) &digest_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_session_connect_attrs_lost",
    (char*) &session_connect_attrs_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_session_connect_attrs_longest_seen",
   (char*) &session_connect_attrs_longest_seen, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_program_lost",
    (char*) &global_program_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_nested_statement_lost",
    (char*) &nested_statement_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_prepared_statements_lost",
    (char*) &global_prepared_stmt_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"Performance_schema_metadata_lock_lost",
    (char*) &global_mdl_container.m_lost, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {NullS, NullS, SHOW_LONG, SHOW_SCOPE_GLOBAL}
};

struct st_mysql_storage_engine pfs_storage_engine=
{ MYSQL_HANDLERTON_INTERFACE_VERSION };

const char* pfs_engine_name= "PERFORMANCE_SCHEMA";

mysql_declare_plugin(perfschema)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &pfs_storage_engine,
  pfs_engine_name,
  "Marc Alff, Oracle", /* Formerly Sun Microsystems, formerly MySQL */
  "Performance Schema",
  PLUGIN_LICENSE_GPL,
  pfs_init_func,                                /* Plugin Init */
  pfs_done_func,                                /* Plugin Deinit */
  0x0001 /* 0.1 */,
  pfs_status_vars,                              /* status variables */
  NULL,                                         /* system variables */
  NULL,                                         /* config options */
  0,                                            /* flags */
}
mysql_declare_plugin_end;

ha_perfschema::ha_perfschema(handlerton *hton, TABLE_SHARE *share)
  : handler(hton, share), m_table_share(NULL), m_table(NULL)
{}

ha_perfschema::~ha_perfschema()
{}

int ha_perfschema::open(const char *name, int mode, uint test_if_locked)
{
  DBUG_ENTER("ha_perfschema::open");

  if (! m_table_share)
    m_table_share= find_table_share(table_share->db.str,
                                    table_share->table_name.str);
  if (! m_table_share)
    DBUG_RETURN(HA_ERR_NO_SUCH_TABLE);

  thr_lock_data_init(m_table_share->m_thr_lock_ptr, &m_thr_lock, NULL);
  ref_length= m_table_share->m_ref_length;

  DBUG_RETURN(0);
}

int ha_perfschema::close(void)
{
  DBUG_ENTER("ha_perfschema::close");
  m_table_share= NULL;
  delete m_table;
  m_table= NULL;

  DBUG_RETURN(0);
}

int ha_perfschema::write_row(uchar *buf)
{
  int result;

  DBUG_ENTER("ha_perfschema::write_row");
  if (!PFS_ENABLED())
    DBUG_RETURN(HA_ERR_WRONG_COMMAND);

  DBUG_ASSERT(m_table_share);
  ha_statistic_increment(&System_status_var::ha_write_count);
  result= m_table_share->write_row(table, buf, table->field);
  DBUG_RETURN(result);
}

void ha_perfschema::use_hidden_primary_key(void)
{
  /*
    This is also called in case of row based replication,
    see TABLE::mark_columns_needed_for_update().
    Add all columns to the read set, but do not touch the write set,
    as some columns in the SETUP_ tables are not writable.
  */
  table->column_bitmaps_set_no_signal(&table->s->all_set, table->write_set);
}

int ha_perfschema::update_row(const uchar *old_data, uchar *new_data)
{
  DBUG_ENTER("ha_perfschema::update_row");
  if (!PFS_ENABLED())
    DBUG_RETURN(HA_ERR_WRONG_COMMAND);

  if (is_executed_by_slave())
    DBUG_RETURN(0);

  DBUG_ASSERT(m_table);
  ha_statistic_increment(&System_status_var::ha_update_count);
  int result= m_table->update_row(table, old_data, new_data, table->field);
  DBUG_RETURN(result);
}

int ha_perfschema::delete_row(const uchar *buf)
{
  DBUG_ENTER("ha_perfschema::delete_row");
  if (!PFS_ENABLED())
    DBUG_RETURN(HA_ERR_WRONG_COMMAND);

  DBUG_ASSERT(m_table);
  ha_statistic_increment(&System_status_var::ha_delete_count);
  int result= m_table->delete_row(table, buf, table->field);
  DBUG_RETURN(result);
}

int ha_perfschema::rnd_init(bool scan)
{
  int result;
  DBUG_ENTER("ha_perfschema::rnd_init");

  DBUG_ASSERT(m_table_share);
  DBUG_ASSERT(m_table_share->m_open_table != NULL);

  stats.records= 0;
  if (m_table == NULL)
    m_table= m_table_share->m_open_table();
  else
    m_table->reset_position();

  if (m_table != NULL)
    m_table->rnd_init(scan);

  result= m_table ? 0 : HA_ERR_OUT_OF_MEM;
  DBUG_RETURN(result);
}

int ha_perfschema::rnd_end(void)
{
  DBUG_ENTER("ha_perfschema::rnd_end");
  DBUG_ASSERT(m_table);
  delete m_table;
  m_table= NULL;
  DBUG_RETURN(0);
}

int ha_perfschema::rnd_next(uchar *buf)
{
  DBUG_ENTER("ha_perfschema::rnd_next");
  if (!PFS_ENABLED())
  {
    table->status= STATUS_NOT_FOUND;
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  DBUG_ASSERT(m_table);
  ha_statistic_increment(&System_status_var::ha_read_rnd_next_count);

  int result= m_table->rnd_next();
  if (result == 0)
  {
    result= m_table->read_row(table, buf, table->field);
    if (result == 0)
      stats.records++;
  }
  table->status= (result ? STATUS_NOT_FOUND : 0);
  DBUG_RETURN(result);
}

void ha_perfschema::position(const uchar *record)
{
  DBUG_ENTER("ha_perfschema::position");

  DBUG_ASSERT(m_table);
  m_table->get_position(ref);
  DBUG_VOID_RETURN;
}

int ha_perfschema::rnd_pos(uchar *buf, uchar *pos)
{
  DBUG_ENTER("ha_perfschema::rnd_pos");
  if (!PFS_ENABLED())
  {
    table->status= STATUS_NOT_FOUND;
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  DBUG_ASSERT(m_table);
  ha_statistic_increment(&System_status_var::ha_read_rnd_count);
  int result= m_table->rnd_pos(pos);
  if (result == 0)
    result= m_table->read_row(table, buf, table->field);
  table->status= (result ? STATUS_NOT_FOUND : 0);
  DBUG_RETURN(result);
}

int ha_perfschema::info(uint flag)
{
  DBUG_ENTER("ha_perfschema::info");
  DBUG_ASSERT(m_table_share);
  if (flag & HA_STATUS_VARIABLE)
    stats.records= m_table_share->get_row_count();
  if (flag & HA_STATUS_CONST)
    ref_length= m_table_share->m_ref_length;
  DBUG_RETURN(0);
}

int ha_perfschema::delete_all_rows(void)
{
  int result;

  DBUG_ENTER("ha_perfschema::delete_all_rows");
  if (!PFS_ENABLED())
    DBUG_RETURN(0);

  if (is_executed_by_slave())
    DBUG_RETURN(0);

  DBUG_ASSERT(m_table_share);
  if (m_table_share->m_delete_all_rows)
    result= m_table_share->m_delete_all_rows();
  else
  {
    result= HA_ERR_WRONG_COMMAND;
  }
  DBUG_RETURN(result);
}

int ha_perfschema::truncate()
{
  return delete_all_rows();
}

THR_LOCK_DATA **ha_perfschema::store_lock(THD *thd,
                                       THR_LOCK_DATA **to,
                                       enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && m_thr_lock.type == TL_UNLOCK)
    m_thr_lock.type= lock_type;
  *to++= &m_thr_lock;
  m_thr_lock.m_psi= m_psi;
  return to;
}

int ha_perfschema::delete_table(const char *name)
{
  DBUG_ENTER("ha_perfschema::delete_table");
  DBUG_RETURN(0);
}

int ha_perfschema::rename_table(const char * from, const char * to)
{
  DBUG_ENTER("ha_perfschema::rename_table ");
  DBUG_RETURN(HA_ERR_WRONG_COMMAND);
}

int ha_perfschema::create(const char *name, TABLE *table_arg,
                          HA_CREATE_INFO *create_info)
{
  DBUG_ENTER("ha_perfschema::create");
  DBUG_ASSERT(table_arg);
  DBUG_ASSERT(table_arg->s);
  if (find_table_share(table_arg->s->db.str,
                       table_arg->s->table_name.str))
  {
    /*
      Attempting to create a known performance schema table.
      Allowing the create, to create .FRM files,
      for the initial database install, and mysql_upgrade.
      This should fail once .FRM are removed.
    */
    DBUG_RETURN(0);
  }
  /*
    This is not a general purpose engine.
    Failure to CREATE TABLE is the expected result.
  */
  DBUG_RETURN(HA_ERR_WRONG_COMMAND);
}

void ha_perfschema::print_error(int error, myf errflag)
{
  switch (error)
  {
    case HA_ERR_TABLE_NEEDS_UPGRADE:
      /*
        The error message for ER_TABLE_NEEDS_UPGRADE refers to REPAIR table,
        which does not apply to performance schema tables.
      */
      my_error(ER_WRONG_NATIVE_TABLE_STRUCTURE, MYF(0),
               table_share->db.str, table_share->table_name.str);
      break;
    case HA_ERR_WRONG_COMMAND:
      /*
        The performance schema is not a general purpose storage engine,
        some operations are not supported, by design.
        We do not want to print "Command not supported",
        which gives the impression that a command implementation is missing,
        and that the failure should be considered a bug.
        We print "Invalid performance_schema usage." instead,
        to emphasise that the operation attempted is not meant to be legal,
        and that the failure returned is indeed the expected result.
      */
      my_error(ER_WRONG_PERFSCHEMA_USAGE, MYF(0));
      break;
    default:
     handler::print_error(error, errflag);
     break;
  }
}

const char *ha_perfschema::index_type(uint)
{ return ""; }

ulong ha_perfschema::index_flags(uint idx, uint part, bool all_parts) const
{
  const PFS_engine_table_share *tmp;

  if (m_table_share != NULL)
  {
    tmp= m_table_share;
  }
  else
  {
    tmp= find_table_share(table_share->db.str,
                          table_share->table_name.str);
    /* ha_perfschema::index_flags is const, can not save in m_table_share. */
  }

  ulong flags = HA_KEY_SCAN_NOT_ROR;

  if (!tmp)
    return 0;

  return flags;
}

/**
  Initializes a handle to use an index.
  @return 0 or error number
*/
int ha_perfschema::index_init(uint idx, bool sorted)
{
  int result;
  DBUG_ENTER("ha_perfschema::index_init");

  DBUG_ASSERT(m_table_share);
  DBUG_ASSERT(m_table_share->m_open_table != NULL);

  if (m_table == NULL)
    m_table= m_table_share->m_open_table();
  else
    m_table->reset_position();

  active_index= idx;

  if (m_table)
    result= m_table->index_init(idx, sorted);
  else
    result= HA_ERR_OUT_OF_MEM;

  DBUG_RETURN(result);
}

int ha_perfschema::index_end()
{
  DBUG_ENTER("ha_perfschema::index_end");
  DBUG_ASSERT(m_table);
  DBUG_ASSERT(active_index != MAX_KEY);
  delete m_table;
  m_table= NULL;
  active_index= MAX_KEY;
  DBUG_RETURN(0);
}

/**
  Positions an index cursor to the index specified in the handle. Fetches the
  row if any.
  @return 0, HA_ERR_KEY_NOT_FOUND, or error
*/
int ha_perfschema::index_read(uchar *buf, const uchar *key, uint key_len,
                              enum ha_rkey_function find_flag)
{
  DBUG_ENTER("ha_perfschema::index_read");
  if (!PFS_ENABLED())
  {
    table->status= STATUS_NOT_FOUND;
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }
  DBUG_ASSERT(m_table_share);
  DBUG_ASSERT(m_table_share->m_open_table != NULL);

  if (m_table == NULL)
    m_table= m_table_share->m_open_table();
  else
    m_table->reset_position();

  DBUG_ASSERT(m_table);
  ha_statistic_increment(&System_status_var::ha_read_key_count);

  DBUG_ASSERT(table != NULL);
  DBUG_ASSERT(table->s != NULL);
  DBUG_ASSERT(table->s->key_info != NULL);
  KEY *key_infos= table->s->key_info;

  int result= m_table->index_read(key_infos, active_index, key, key_len, find_flag);
  if (result == 0)
  {
    result= m_table->read_row(table, buf, table->field);
  }
  table->status= (result ? STATUS_NOT_FOUND : 0);
  DBUG_RETURN(result);
}

/**
  Reads the next row from a cursor, which must have previously been
  positioned by index_read.
  @return 0, HA_ERR_END_OF_FILE, or error
*/
int ha_perfschema::index_next(uchar *buf)
{
  DBUG_ENTER("ha_perfschema::index_next");
  if (!PFS_ENABLED())
  {
    table->status= STATUS_NOT_FOUND;
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  ha_statistic_increment(&System_status_var::ha_read_next_count);

  DBUG_ASSERT(m_table);

  int result= m_table->index_next();
  if (result == 0)
  {
    result= m_table->read_row(table, buf, table->field);
  }
  table->status= (result ? STATUS_NOT_FOUND : 0);
  DBUG_RETURN(result);
}

/**
  Reads the next row matching the given key value.
  @return 0, HA_ERR_END_OF_FILE, or error
*/
int ha_perfschema::index_next_same(uchar *buf, const uchar* key, uint keylen)
{
  DBUG_ENTER("ha_perfschema::index_next_same");
  if (!PFS_ENABLED())
  {
    table->status= STATUS_NOT_FOUND;
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  ha_statistic_increment(&System_status_var::ha_read_next_count);

  DBUG_ASSERT(m_table);

  int result= m_table->index_next_same(key, keylen);
  if (result == 0)
  {
    result= m_table->read_row(table, buf, table->field);
  }

  table->status= (result ? STATUS_NOT_FOUND : 0);

  DBUG_RETURN(result);
}

bool ha_perfschema::is_executed_by_slave() const
{
  DBUG_ASSERT(table != NULL);
  DBUG_ASSERT(table->in_use != NULL);
  return table->in_use->slave_thread;
}

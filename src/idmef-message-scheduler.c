/*****
*
* Copyright (C) 2001, 2002, 2003, 2004 Yoann Vandoorselaere <yoann@prelude-ids.org>
* All Rights Reserved
*
* This file is part of the Prelude program.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2, or (at your option)
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; see the file COPYING.  If not, write to
* the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <netinet/in.h> /* required by common.h */

#include <libprelude/prelude-log.h>
#include <libprelude/prelude-list.h>
#include <libprelude/idmef.h>
#include <libprelude/idmef-message.h>
#include <libprelude/idmef-message-id.h>
#include <libprelude/extract.h>
#include <libprelude/threads.h>
#include <libprelude/common.h>
#include <libprelude/prelude-client.h>

#include "plugin-decode.h"
#include "plugin-report.h"
#include "plugin-filter.h"
#include "pconfig.h"
#include "relaying.h"
#include "pmsg-to-idmef.h"
#include "idmef-message-scheduler.h"

#define MESSAGE_PER_SENSOR 10

#define ROUND_ROBBIN_HIGH 5
#define ROUND_ROBBIN_MID  3
#define ROUND_ROBBIN_LOW  2


#define MAX_MESSAGE_IN_MEMORY 200

#define HIGH_PRIORITY_MESSAGE_FILENAME MANAGER_FIFO_DIR "/high-priority-fifo"
#define MID_PRIORITY_MESSAGE_FILENAME MANAGER_FIFO_DIR "/mid-priority-fifo"
#define LOW_PRIORITY_MESSAGE_FILENAME MANAGER_FIFO_DIR "/low-priority-fifo"

#define QUEUE_STATE_DESTROYED 0x01


typedef struct {

        char *filename;
        int input_available;
        
        prelude_io_t *wfd;
        prelude_io_t *rfd;
        
} file_output_t;



typedef struct {
        unsigned int in_memory_count;
        struct list_head message_list;
        file_output_t disk_message_list;
} message_queue_t;



struct idmef_queue {
        struct list_head list;

        int state;

        message_queue_t high;
        message_queue_t mid;
        message_queue_t low;
                
        pthread_mutex_t mutex;
};


static LIST_HEAD(message_queue);
static unsigned int global_id = 0;
static pthread_mutex_t queue_list_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned int input_available = 0;
static pthread_cond_t input_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t input_mutex = PTHREAD_MUTEX_INITIALIZER;



/*
 * Thread controling stuff.
 */
static pthread_t thread;
static volatile sig_atomic_t stop_processing = 0;



static void signal_input_available(void)
{
        pthread_mutex_lock(&input_mutex);

        if ( ! input_available ) {
                input_available = 1;
                pthread_cond_signal(&input_cond);
        }

        pthread_mutex_unlock(&input_mutex);
}




/*
 * Wait until a message is queued.
 */
static void wait_for_message(void) 
{        
        pthread_mutex_lock(&input_mutex);            
          
        while ( ! input_available && ! stop_processing ) 
                pthread_cond_wait(&input_cond, &input_mutex);
        
        if ( ! input_available && stop_processing ) {
                pthread_mutex_unlock(&input_mutex);
                pthread_exit(NULL);
        }
        
        /*
         * We are going to process all available data.
         */
        input_available = 0;
        pthread_mutex_unlock(&input_mutex);
}




static int clear_fifo(file_output_t *out) 
{
        int ret;
                
        ret = ftruncate(prelude_io_get_fd(out->wfd), 0);
        if ( ret < 0 ) {
                log(LOG_ERR, "error truncating fifo to 0.\n");
                return -1;
        }

        lseek(prelude_io_get_fd(out->rfd), 0, SEEK_SET);
                
        return 0;
}




static void destroy_file_output(file_output_t *out) 
{        
        prelude_io_close(out->rfd);
        prelude_io_destroy(out->rfd);

        prelude_io_close(out->wfd);
        prelude_io_destroy(out->wfd);
        
        assert(out->input_available == 0);
                
        unlink(out->filename);
        free(out->filename);
}




/*
 * Get a low / mid priority queued message
 */
static prelude_msg_t *get_message_from_file(file_output_t *out) 
{
        prelude_msg_t *msg = NULL;
        prelude_msg_status_t status;
        
        if ( ! out->input_available )
                return NULL;
                
        status = prelude_msg_read(&msg, out->rfd);
        if ( status == prelude_msg_finished )
                return msg;

        else if ( status == prelude_msg_eof )
                out->input_available = 0;

        
        else {
                /*
                 * unfinished and error should never happen 
                 */
                log(LOG_ERR, "on disk message fifo is corrupted (status=%d).\n", status);
                exit(1);
        }

        return msg;
}



static int process_message(prelude_msg_t *msg) 
{
        idmef_message_t *idmef = NULL;
        int relay_filter_available = 0;
        
        relay_filter_available = filter_plugins_available(FILTER_CATEGORY_RELAYING);
        if ( relay_filter_available < 0 )
                manager_relay_msg_if_needed(msg);
        
        if ( report_plugins_available() < 0 && relay_filter_available < 0 ) {
                /*
                 * we are probably a simple relaying manager.
                 */
                prelude_msg_destroy(msg);
                return 0;
        }
        
        idmef = pmsg_to_idmef(msg);
        if ( ! idmef ) {
                prelude_msg_destroy(msg);
                return -1;
        }

        if ( relay_filter_available == 0 && filter_plugins_run_by_category(idmef, FILTER_CATEGORY_RELAYING) == 0 )
                manager_relay_msg_if_needed(msg);


        /*
         * run simple reporting plugin.
         */
        report_plugins_run(idmef);

        /*
         * free data.
         */
	idmef_message_destroy(idmef);
	prelude_msg_destroy(msg);
        
        return 0;
}



static void queue_destroy(idmef_queue_t *queue)
{
        pthread_mutex_lock(&queue_list_mutex);
        list_del(&queue->list);
        pthread_mutex_unlock(&queue_list_mutex);
        
        pthread_mutex_destroy(&queue->mutex);
        
        destroy_file_output(&queue->high.disk_message_list);
        destroy_file_output(&queue->mid.disk_message_list);
        destroy_file_output(&queue->low.disk_message_list);
        
        free(queue);
}




static prelude_msg_t *get_message(idmef_queue_t *queue, message_queue_t *mqueue) 
{
        prelude_msg_t *msg = NULL;
                
        if ( ! list_empty(&mqueue->message_list) ) {
                msg = prelude_list_get_object(mqueue->message_list.next, prelude_msg_t);
                prelude_list_del( (prelude_linked_object_t *) msg);
                mqueue->in_memory_count--;
        }
        
        return msg ? msg : get_message_from_file(&mqueue->disk_message_list); 
}



static int is_queue_dirty(idmef_queue_t *queue)
{
        int ret;
        
        pthread_mutex_lock(&queue->mutex);
        
        ret =   ! list_empty(&queue->high.message_list) +
                ! list_empty(&queue->mid.message_list) +
                ! list_empty(&queue->low.message_list) +
                queue->high.disk_message_list.input_available +
                queue->mid.disk_message_list.input_available +
                queue->low.disk_message_list.input_available;
        
        pthread_mutex_unlock(&queue->mutex);
        
        return ret;
}




static prelude_msg_t *get_first_message_in_queue(idmef_queue_t *queue)
{
        prelude_msg_t *msg;
        
        msg = get_message(queue, &queue->high);
        if ( msg )
                return msg;

        msg = get_message(queue, &queue->mid);
        if ( msg )
                return msg;

        return get_message(queue, &queue->low);
}




static void read_message_scheduled(idmef_queue_t *queue)
{
        int ret, i = 0;
        prelude_msg_t *msg;
        uint32_t msg_count = 0;

        while ( i++ < MESSAGE_PER_SENSOR ) {

                msg = NULL;

                pthread_mutex_lock(&queue->mutex);
                
                if ( msg_count < ROUND_ROBBIN_HIGH ) 
                        msg = get_message(queue, &queue->high);
                
                else if ( msg_count < (ROUND_ROBBIN_HIGH + ROUND_ROBBIN_MID) )
                        msg = get_message(queue, &queue->mid);
                
                else 
                        msg = get_message(queue, &queue->low);
                
                if ( ! msg && !(msg = get_first_message_in_queue(queue)) ) {
                        pthread_mutex_unlock(&queue->mutex);
                        break;
                }
                
                pthread_mutex_unlock(&queue->mutex);

                ret = process_message(msg);
                if ( ret < 0 ) {
                        /*
                         * FIXME: need a way to close connection on invalid message.
                         */
                        log(LOG_ERR, "Invalid message received.\n");
                }
                
                msg_count = (msg_count + 1) % (ROUND_ROBBIN_HIGH + ROUND_ROBBIN_MID + ROUND_ROBBIN_LOW);
        }
}



static void schedule_queued_message(void)
{
        int dirty, any_queue_dirty;
        idmef_queue_t *queue, *bkp = NULL;
        
        do {
                queue = NULL;
                any_queue_dirty = 0;
                
                while ( 1 ) {
                        pthread_mutex_lock(&queue_list_mutex);
                        queue = list_get_next_safe(queue, bkp, &message_queue, idmef_queue_t, list);
                        pthread_mutex_unlock(&queue_list_mutex);

                        if ( ! queue )
                                break;
                                                
                        read_message_scheduled(queue);

                        dirty = is_queue_dirty(queue);
                        any_queue_dirty += dirty;
                        
                        if ( ! dirty && queue->state & QUEUE_STATE_DESTROYED )
                                queue_destroy(queue);
                }
        } while ( any_queue_dirty );
}
        



/*
 * This is the function responssible for handling queued message.
 */
static void *message_reader(void *arg) 
{
        int ret;
        sigset_t set;
        
        sigfillset(&set);
        
        ret = pthread_sigmask(SIG_SETMASK, &set, NULL);
        if ( ret < 0 ) {
                log(LOG_ERR, "couldn't set thread signal mask.\n");
                return NULL;
        }

        while ( ! stop_processing ) {
                schedule_queued_message();
                wait_for_message();
        }

        /*
         * make sure we don't miss some.
         */
        schedule_queued_message();

        return NULL;
}



static int queue_message_to_fd(file_output_t *out, prelude_msg_t *msg) 
{
        ssize_t ret;

        /*
         * if this condition is true, then it mean the reader is positioned
         * at EOF, and that we can truncate the file and reset the reader.
         */
        if ( out->input_available == 0 )
                clear_fifo(out);
        
        ret = prelude_msg_write(msg, out->wfd);
        if ( ret <= 0 )
                log(LOG_ERR, "couldn't write message to file.\n");

        out->input_available = 1;
        
        /*
         * Message was copied to a file, we do not need it anymore.
         */
        prelude_msg_destroy(msg);

        return (ret > 0) ? 0 : -1;
}




/*
 * Queue this message to memory.
 */
static void queue_message(idmef_queue_t *queue, message_queue_t *mqueue, prelude_msg_t *msg) 
{
        int queue_to_fd = 0;
        
        pthread_mutex_lock(&queue->mutex);

        if ( mqueue->in_memory_count < MAX_MESSAGE_IN_MEMORY ) {
                mqueue->in_memory_count++;
                prelude_list_add_tail((prelude_linked_object_t *) msg, &mqueue->message_list);
        } else
                queue_to_fd = 1;

        pthread_mutex_unlock(&queue->mutex);

        /*
         * we don't need the lock.
         */
        if ( queue_to_fd )
                queue_message_to_fd(&mqueue->disk_message_list, msg);
}



static int flush_existing_fifo(const char *filename, file_output_t *out, off_t size) 
{
        int num = 0, ret;
        prelude_msg_t *msg;
        
        log(LOG_INFO, "%s contain unflushed message (%d bytes). Flushing.\n", filename, size);
        
        while ( 1 ) {
                
                msg = get_message_from_file(out);
                if ( ! msg )
                        break;
                
                ret = process_message(msg);
                if ( ret < 0 )
                        return -1;

                num++;
        }
        
        log(LOG_INFO, "Done - %d messages flushed.\n", num);
                                
        return 0;
}




static prelude_io_t *new_sysio_from_fd(int fd)
{
        prelude_io_t *ptr;

        ptr = prelude_io_new();
        if ( ! ptr )
                return NULL;

        prelude_io_set_sys_io(ptr, fd);

        return ptr;
}



/*
 * 
 */
static int init_file_output(const char *filename, file_output_t *out) 
{
        int rfd, wfd;

        out->filename = strdup(filename);
        if ( ! out->filename ) {
                log(LOG_ERR, "memory exhausted.\n");
                return -1;
        }
        
        wfd = prelude_open_persistant_tmpfile(filename, O_WRONLY|O_APPEND, S_IRUSR|S_IWUSR);
        if ( wfd < 0 ) {
                log(LOG_ERR, "couldn't open %s in append mode.\n", filename);
                return -1;
        }
        
        rfd = open(filename, O_RDONLY);
        if ( rfd < 0 ) {
                log(LOG_ERR, "couldn't open %s for reading.\n", filename);
                close(wfd);
                return -1;
        }

        out->wfd = new_sysio_from_fd(wfd);
        if ( ! out->wfd ){
                close(rfd);
                close(wfd);
                return -1;
        }
        
        out->rfd = new_sysio_from_fd(rfd);
        if ( ! out->rfd ) {
                close(rfd);
                prelude_io_close(out->wfd);
                prelude_io_destroy(out->wfd);
                return -1;
        }
        
        out->input_available = 0;

        return 0;
}




static int flush_orphan_fifo(const char *filename)
{
        int ret;
        struct stat st;
        file_output_t tmp;
        
        ret = stat(filename, &st);
        if ( ret < 0 ) {
                if ( errno != ENOENT )
                        log(LOG_ERR, "couldn't stats %s.\n", filename);

                return -1;
        }

        if ( st.st_size > 0 ) {
                ret = init_file_output(filename, &tmp);
                if ( ret < 0 )
                        return -1;
                
                tmp.input_available = 1;
                flush_existing_fifo(filename, &tmp, st.st_size);

                destroy_file_output(&tmp);
        }

        unlink(filename);
        
        return 0;
}




void idmef_message_schedule(idmef_queue_t *queue, prelude_msg_t *msg) 
{
        message_queue_t *mqueue = NULL;
        
        switch (prelude_msg_get_priority(msg)) {

        case PRELUDE_MSG_PRIORITY_HIGH:
                mqueue = &queue->high;
                break;

        case PRELUDE_MSG_PRIORITY_MID:
                mqueue = &queue->mid;
                break;

        case PRELUDE_MSG_PRIORITY_LOW:
                mqueue = &queue->low;
                break;
        }

        assert(mqueue);
        
        queue_message(queue, mqueue, msg);        
        signal_input_available();
}





idmef_queue_t *idmef_message_scheduler_queue_new(void)
{
        int ret;
        char buf[256];
        idmef_queue_t *queue;
        
        queue = calloc(1, sizeof(*queue));
        if ( ! queue ) {
                log(LOG_ERR, "memory exhausted.\n");
                return NULL;
        }
        
        INIT_LIST_HEAD(&queue->high.message_list);
        INIT_LIST_HEAD(&queue->mid.message_list);
        INIT_LIST_HEAD(&queue->low.message_list);

        snprintf(buf, sizeof(buf), "%s.%u", HIGH_PRIORITY_MESSAGE_FILENAME, global_id);        
        ret = init_file_output(buf, &queue->high.disk_message_list);
        if ( ret < 0 ) {
                free(queue);
                return NULL;
        }
        
        snprintf(buf, sizeof(buf), "%s.%u", MID_PRIORITY_MESSAGE_FILENAME, global_id);        
        ret = init_file_output(buf, &queue->mid.disk_message_list);
        if ( ret < 0 ) {
                destroy_file_output(&queue->high.disk_message_list);
                free(queue);
                return NULL;
        }
        
        snprintf(buf, sizeof(buf), "%s.%u", LOW_PRIORITY_MESSAGE_FILENAME, global_id);        
        ret = init_file_output(buf, &queue->low.disk_message_list);
        if ( ret < 0 ) {
                destroy_file_output(&queue->high.disk_message_list);
                destroy_file_output(&queue->mid.disk_message_list);
                free(queue);
                return NULL;
        }
        
        pthread_mutex_init(&queue->mutex, NULL);
        pthread_mutex_lock(&queue_list_mutex);
        
        global_id++;
        list_add_tail(&queue->list, &message_queue);

        pthread_mutex_unlock(&queue_list_mutex);
        
        return queue;
}




void idmef_message_scheduler_queue_destroy(idmef_queue_t *queue)
{
        queue->state |= QUEUE_STATE_DESTROYED;
        signal_input_available();
}




int idmef_message_scheduler_init(void) 
{
        char buf[256];
        int ret, i, continue_check = 1;

        /*
         * this code recover orphaned fifo in case of a prelude-manager crash.
         */
        for ( i = 0; continue_check != 0; i++ ) {
                continue_check = 0;
                
                snprintf(buf, sizeof(buf), "%s.%d", HIGH_PRIORITY_MESSAGE_FILENAME, i);
                ret = flush_orphan_fifo(buf);
                if ( ret == 0 )
                        continue_check = 1;
                
                snprintf(buf, sizeof(buf), "%s.%d", MID_PRIORITY_MESSAGE_FILENAME, i);

                ret = flush_orphan_fifo(buf);
                if ( ret == 0 )
                        continue_check = 1;
                
                snprintf(buf, sizeof(buf), "%s.%d", LOW_PRIORITY_MESSAGE_FILENAME, i);

                ret = flush_orphan_fifo(buf);
                if ( ret == 0 )
                        continue_check = 1;
        }

        ret = pthread_create(&thread, NULL, &message_reader, NULL);
        if ( ret < 0 ) {
                log(LOG_ERR, "couldn't create message processing thread.\n");
                return -1;
        }
        
        return 0;
}




void idmef_message_scheduler_exit(void) 
{
        idmef_queue_t *queue;
        struct list_head *tmp, *bkp;
        
        pthread_mutex_lock(&input_mutex);

        stop_processing = 1;
        pthread_cond_signal(&input_cond);

        pthread_mutex_unlock(&input_mutex);
        
        log(LOG_INFO, "- Waiting queued message to be processed.\n");
        pthread_join(thread, NULL);
        
        pthread_cond_destroy(&input_cond);
        pthread_mutex_destroy(&input_mutex);

        list_for_each_safe(tmp, bkp, &message_queue) {
                queue = list_entry(tmp, idmef_queue_t, list);
                queue_destroy(queue);
        }
}











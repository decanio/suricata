/* Copyright (C) 2007-2010 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Victor Julien <victor@inliniac.net>
 *
 * Stream Chunk Handling API
 */

#include "suricata-common.h"
#include "decode.h"
#include "threads.h"
#include "stream.h"
#include "util-pool.h"
#include "util-debug.h"

#ifdef  __tile__
static tmc_spin_queued_mutex_t stream_pool_memuse_mutex;
#else
static SCMutex stream_pool_memuse_mutex;
#endif
static uint64_t stream_pool_memuse = 0;
static uint64_t stream_pool_memcnt = 0;

/* per queue setting */
static uint16_t toserver_min_chunk_len = 2560;
static uint16_t toclient_min_chunk_len = 2560;

static Pool *stream_msg_pool = NULL;
#ifdef __tile__
static tmc_spin_queued_mutex_t stream_msg_pool_mutex = TMC_SPIN_QUEUED_MUTEX_INIT;
#else
static SCMutex stream_msg_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

void *StreamMsgAlloc(void *null) {
    StreamMsg *s = SCMalloc(sizeof(StreamMsg));
    if (s == NULL)
        return NULL;

    memset(s, 0, sizeof(StreamMsg));

#ifdef __tile__
    tmc_spin_queued_mutex_lock(&stream_pool_memuse_mutex);
#else
    SCMutexLock(&stream_pool_memuse_mutex);
#endif
    stream_pool_memuse += sizeof(StreamMsg);
    stream_pool_memcnt ++;
#ifdef __tile__
    tmc_spin_queued_mutex_unlock(&stream_pool_memuse_mutex);
#else
    SCMutexUnlock(&stream_pool_memuse_mutex);
#endif
    return s;
}

void StreamMsgFree(void *ptr) {
    if (ptr == NULL)
        return;

    StreamMsg *s = (StreamMsg *)ptr;
    SCFree(s);
    return;
}

static void StreamMsgEnqueue (StreamMsgQueue *q, StreamMsg *s) {
    SCEnter();
    SCLogDebug("s %p", s);
    /* more packets in queue */
    if (q->top != NULL) {
        s->next = q->top;
        q->top->prev = s;
        q->top = s;
    /* only packet */
    } else {
        q->top = s;
        q->bot = s;
    }
    q->len++;
#ifdef DBG_PERF
    if (q->len > q->dbg_maxlen)
        q->dbg_maxlen = q->len;
#endif /* DBG_PERF */
    SCReturn;
}

static StreamMsg *StreamMsgDequeue (StreamMsgQueue *q) {
    SCEnter();

    /* if the queue is empty there are no packets left.
     * In that case we sleep and try again. */
    if (q->len == 0) {
        SCReturnPtr(NULL, "StreamMsg");
    }

    /* pull the bottom packet from the queue */
    StreamMsg *s = q->bot;

    /* more packets in queue */
    if (q->bot->prev != NULL) {
        q->bot = q->bot->prev;
        q->bot->next = NULL;
        /* just the one we remove, so now empty */
    } else {
        q->top = NULL;
        q->bot = NULL;
    }
    q->len--;

    s->next = NULL;
    s->prev = NULL;
    SCReturnPtr(s, "StreamMsg");
}

/* Used by stream reassembler to get msgs */
StreamMsg *StreamMsgGetFromPool(void)
{
#ifdef __tile__
    tmc_spin_queued_mutex_lock(&stream_msg_pool_mutex);
#else
    SCMutexLock(&stream_msg_pool_mutex);
#endif
    StreamMsg *s = (StreamMsg *)PoolGet(stream_msg_pool);
#ifdef __tile__
    tmc_spin_queued_mutex_unlock(&stream_msg_pool_mutex);
#else
    SCMutexUnlock(&stream_msg_pool_mutex);
#endif
    return s;
}

/* Used by l7inspection to return msgs to pool */
void StreamMsgReturnToPool(StreamMsg *s) {
    SCLogDebug("s %p", s);
#ifdef __tile__
    tmc_spin_queued_mutex_lock(&stream_msg_pool_mutex);
#else
    SCMutexLock(&stream_msg_pool_mutex);
#endif
    PoolReturn(stream_msg_pool, (void *)s);
#ifdef __tile__
    tmc_spin_queued_mutex_unlock(&stream_msg_pool_mutex);
#else
    SCMutexUnlock(&stream_msg_pool_mutex);
#endif
}

/* Used by l7inspection to get msgs with data */
StreamMsg *StreamMsgGetFromQueue(StreamMsgQueue *q)
{
    if (q->len > 0) {
        StreamMsg *s = StreamMsgDequeue(q);
        return s;
    } else {
        /* return NULL if we have no stream msg. Should only happen on signals. */
        return NULL;
    }
}

/* Used by stream reassembler to fill the queue for l7inspect reading */
void StreamMsgPutInQueue(StreamMsgQueue *q, StreamMsg *s)
{
    StreamMsgEnqueue(q, s);
    SCLogDebug("q->len %" PRIu32 "", q->len);
}

void StreamMsgQueuesInit(void) {
#ifdef __tile__
    tmc_spin_queued_mutex_init(&stream_pool_memuse_mutex);
#else
    SCMutexInit(&stream_pool_memuse_mutex, NULL);
#endif

    stream_msg_pool = PoolInit(0,250,StreamMsgAlloc,NULL,StreamMsgFree);
    if (stream_msg_pool == NULL)
        exit(EXIT_FAILURE); /* XXX */
}

void StreamMsgQueuesDeinit(char quiet) {
    PoolFree(stream_msg_pool);
#ifndef __tile__
    SCMutexDestroy(&stream_pool_memuse_mutex);
#endif

    if (quiet == FALSE)
        SCLogDebug("stream_pool_memuse %"PRIu64", stream_pool_memcnt %"PRIu64"", stream_pool_memuse, stream_pool_memcnt);
}

/** \brief alloc a stream msg queue
 *  \retval smq ptr to the queue or NULL */
StreamMsgQueue *StreamMsgQueueGetNew(void) {
    StreamMsgQueue *smq = SCMalloc(sizeof(StreamMsgQueue));
    if (smq == NULL)
        return NULL;

    memset(smq, 0x00, sizeof(StreamMsgQueue));
    return smq;
}

/** \brief Free a StreamMsgQueue
 *  \param q the queue to free
 *  \todo we may want to consider non empty queue's
 */
void StreamMsgQueueFree(StreamMsgQueue *q) {
    SCFree(q);
}

StreamMsgQueue *StreamMsgQueueGetByPort(uint16_t port) {
    /* XXX implement this */
    return NULL;//&stream_q;
}

void StreamMsgQueueSetMinChunkLen(uint8_t dir, uint16_t len) {
    if (dir == FLOW_PKT_TOSERVER) {
        toserver_min_chunk_len = len;
    } else {
        toclient_min_chunk_len = len;
    }
}

uint16_t StreamMsgQueueGetMinChunkLen(uint8_t dir) {
    if (dir == FLOW_PKT_TOSERVER) {
        return toserver_min_chunk_len;
    } else {
        return toclient_min_chunk_len;
    }
}

/** \brief Return a list of smsgs to the pool */
void StreamMsgReturnListToPool(void *list) {
    /* if we have (a) smsg(s), return to the pool */
    StreamMsg *smsg = (StreamMsg *)list;
    while (smsg != NULL) {
        StreamMsg *smsg_next = smsg->next;
        SCLogDebug("returning smsg %p to pool", smsg);
        smsg->next = NULL;
        smsg->prev = NULL;
        smsg->flow = NULL;
        StreamMsgReturnToPool(smsg);
        smsg = smsg_next;
    }
}

/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include <string.h>

#include "peer.h"
#include "session.h"

#include "processor.h"
#include "proc-factory.h"
#include "timer.h"
#include "processors/keepalive2-proc.h"
#include "processors/rcvcmd-proc.h"
#include "processors/service-proxy-proc.h"
#include "processors/service-stub-proc.h"

#ifdef CCNET_SERVER
#include "processors/recvlogin-proc.h"
#include "processors/recvlogout-proc.h"
#else
#include "processors/sendlogin-proc.h"
#include "processors/sendlogout-proc.h"
#endif


#define DEBUG_FLAG  CCNET_DEBUG_PROCESSOR
#include "log.h"
#include "utils.h"


/* Note, timeout here must be larger than the timeout of keepalive-proc 
 *
 * Here, we only handle problems which happen when tcp connection is ok but
 * processors are dead or not created by peer.
 *
 * 
 */

/* The timeout of keepalive-proc is 180s now. */
#define DEFAULT_NO_PACKET_TIMEOUT    10   /* 10 seconds */
#define KEEPALIVE_PULSE              5 * 1000 /* 5 seconds */
#define CONNECTION_TIMEOUT           182
#define MAX_PROCS_KEEPALIVE          50    /* we check 50 proc at most */

typedef struct {
    GHashTable *proc_type_table;
} CcnetProcFactoryPriv;

#define GET_PRIV(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), CCNET_TYPE_PROC_FACTORY, CcnetProcFactoryPriv))

G_DEFINE_TYPE (CcnetProcFactory, ccnet_proc_factory, G_TYPE_OBJECT);

static void
ccnet_proc_factory_class_init (CcnetProcFactoryClass *klass)
{
    g_type_class_add_private (klass, sizeof (CcnetProcFactoryPriv));
}

static void
ccnet_proc_factory_init (CcnetProcFactory *factory)
{
    CcnetProcFactoryPriv *priv = GET_PRIV (factory);

    priv->proc_type_table = g_hash_table_new_full (
        g_str_hash, g_str_equal, g_free, NULL);
}

void
ccnet_proc_factory_register_processor (CcnetProcFactory *factory,
                                       const char *serv_name,
                                       GType type)
{
    CcnetProcFactoryPriv *priv = GET_PRIV (factory);

    CcnetProcessorClass *proc_class = 
        (CcnetProcessorClass *)g_type_class_ref(type);
    g_assert (proc_class->start != NULL);
    g_type_class_unref (proc_class);

    g_hash_table_insert (priv->proc_type_table, g_strdup (serv_name), 
                         (gpointer) type);
}


GType ccnet_getpubinfo_proc_get_type ();
GType ccnet_putpubinfo_proc_get_type ();

GType ccnet_sendmsg_proc_get_type ();
GType ccnet_rcvmsg_proc_get_type ();

GType ccnet_rcvcmd_proc_get_type ();
GType ccnet_getperm_proc_get_type ();

GType ccnet_keepalive2_proc_get_type ();

GType ccnet_mqserver_proc_get_type ();

GType ccnet_service_proxy_proc_get_type ();
GType ccnet_service_stub_proc_get_type ();

GType ccnet_sync_relay_proc_get_type ();
GType ccnet_sync_relay_slave_proc_get_type ();

GType ccnet_rpcserver_proc_get_type ();
GType ccnet_echo_proc_get_type ();

static int keepalive_pulse (CcnetProcFactory *factory);

CcnetProcFactory *
ccnet_proc_factory_new (CcnetSession *session)
{
    CcnetProcFactory *factory;

    factory = g_object_new (CCNET_TYPE_PROC_FACTORY, NULL);
    factory->session = session;
    factory->no_packet_timeout = DEFAULT_NO_PACKET_TIMEOUT;
    INIT_LIST_HEAD(&(factory->procs_list));
    factory->procs = NULL;

    /* register fundamental processors */
    /* FIXME: These processor types shall be regitered by managers */
    ccnet_proc_factory_register_processor (factory, "get-pubinfo",
                                           ccnet_getpubinfo_proc_get_type ());
    ccnet_proc_factory_register_processor (factory, "put-pubinfo",
                                           ccnet_putpubinfo_proc_get_type ());

    ccnet_proc_factory_register_processor (factory, "send-msg",
                                           ccnet_sendmsg_proc_get_type ());
    ccnet_proc_factory_register_processor (factory, "receive-msg",
                                           ccnet_rcvmsg_proc_get_type ());

    ccnet_proc_factory_register_processor (factory, "receive-cmd",
                                           ccnet_rcvcmd_proc_get_type ());
    /* ccnet_proc_factory_register_processor (factory, "receive-event", */
    /*                                        ccnet_rcvevent_proc_get_type ()); */
    
    ccnet_proc_factory_register_processor (factory, "keepalive2",
                                           ccnet_keepalive2_proc_get_type ());

    ccnet_proc_factory_register_processor (factory, "mq-server",
                                           ccnet_mqserver_proc_get_type ());

    ccnet_proc_factory_register_processor (factory, "service-proxy",
                                           ccnet_service_proxy_proc_get_type());

    ccnet_proc_factory_register_processor (factory, "service-stub",
                                           ccnet_service_stub_proc_get_type());

    ccnet_proc_factory_register_processor (factory, "ccnet-rpcserver",
                                           ccnet_rpcserver_proc_get_type());


#ifdef CCNET_SERVER
    ccnet_proc_factory_register_processor (factory, "recvlogin",
                                           ccnet_recvlogin_proc_get_type());
    
    ccnet_proc_factory_register_processor (factory, "recvlogout",
                                           ccnet_recvlogout_proc_get_type());
#else
    ccnet_proc_factory_register_processor (factory, "sendlogin",
                                           ccnet_sendlogin_proc_get_type());
    
    ccnet_proc_factory_register_processor (factory, "sendlogout",
                                           ccnet_sendlogout_proc_get_type());
#endif

/*
    if (session->is_relay) {
        ccnet_proc_factory_register_processor (factory, "sync-relay-slave",
                                      ccnet_sync_relay_slave_proc_get_type());
    } else {
        ccnet_proc_factory_register_processor (factory, "sync-relay",
                                               ccnet_sync_relay_proc_get_type());
    }
*/

   ccnet_proc_factory_register_processor (factory, "echo",
                                          ccnet_echo_proc_get_type ());

    return factory;
}

void
ccnet_proc_factory_start (CcnetProcFactory *factory)
{
    factory->keepalive_timer = ccnet_timer_new (
        (TimerCB) keepalive_pulse, factory, KEEPALIVE_PULSE);
}

static GType
ccnet_proc_factory_get_proc_type (CcnetProcFactory *factory,
                                  const char *serv_name)
{
    CcnetProcFactoryPriv *priv = GET_PRIV (factory);

    return (GType) g_hash_table_lookup (priv->proc_type_table, serv_name);
}

static inline CcnetProcessor *
create_processor_common (CcnetProcFactory *factory,
                         const char *serv_name,
                         CcnetPeer *peer,
                         int req_id)
{
    GType type;
    CcnetProcessor *processor;

    type = ccnet_proc_factory_get_proc_type (factory, serv_name);
    if (type == 0) {
        return NULL;
    }

    processor = g_object_new (type, NULL);
    processor->peer = peer;
    g_object_ref (peer);
    processor->session = factory->session;
    processor->id = req_id;
    /* Set the real processor name.
     * This may be different from the processor class name.
     */
    processor->name = g_strdup(serv_name);

    ccnet_debug ("Create processor %s(%d) %s\n", GET_PNAME(processor),
                 PRINT_ID(processor->id), processor->name);
    ccnet_peer_add_processor (processor->peer, processor);

    list_add (&(processor->list), &(factory->procs_list));
    factory->procs_alive_cnt++;

    return processor;
}

CcnetProcessor *
ccnet_proc_factory_create_slave_processor (CcnetProcFactory *factory,
                                           const char *serv_name,
                                           CcnetPeer *peer,
                                           int req_id)
{

    return create_processor_common(factory, serv_name,
                                   peer, SLAVE_ID (req_id));
}

CcnetProcessor *
ccnet_proc_factory_create_master_processor (CcnetProcFactory *factory,
                                            const char *serv_name,
                                            CcnetPeer *peer)
{
    if (peer->redirect_to) {
        ccnet_debug ("Redirect processor request from %s(%.8s) to %s(%.8s)\n",
                     peer->name, peer->id, 
                     peer->redirect_to->name, peer->redirect_to->id);
        peer = peer->redirect_to;
    }

    return create_processor_common (
        factory, serv_name,
        peer, MASTER_ID(ccnet_peer_get_request_id (peer)) );
}

static void inline
recycle (CcnetProcFactory *factory, CcnetProcessor *processor)
{
    list_del (&(processor->list));
    factory->procs_alive_cnt--;

#ifdef DEBUG_PROC
    if (strcmp(GET_PNAME(processor), "rpcserver-proc") != 0) {
        /* ignore rpcserver-proc */

        CcnetProc *proc = ccnet_proc_new();
        g_object_set (proc, "name", GET_PNAME(processor),
                      "peer-name", processor->peer->name,
                      "ctime", (int) processor->start_time,
                      "dtime", (int) time(NULL), NULL);
        factory->procs = g_list_prepend (factory->procs, proc);
    }
#endif

    /* TODO: implement processor pool */
    g_object_unref (processor);
}

void
ccnet_proc_factory_recycle (CcnetProcFactory *factory,
                            CcnetProcessor *processor)
{
    recycle (factory, processor);
}


void
ccnet_proc_factory_shutdown_processors (CcnetProcFactory *factory,
                                        CcnetPeer *peer)
{
    GList *list, *ptr;
    CcnetProcessor *processor;

    list = g_hash_table_get_values (peer->processors);
    for (ptr = list; ptr; ptr = ptr->next) {
        processor = CCNET_PROCESSOR (ptr->data);
        ccnet_processor_shutdown (processor, PROC_NETDOWN);
    }
    g_hash_table_remove_all (peer->processors);
    g_list_free (list);
}

void
ccnet_proc_factory_set_keepalive_timeout (CcnetProcFactory *factory,
                                          int timeout)
{
    factory->no_packet_timeout = timeout;
}

/* keep processors alive by sending keepalive packets. 

   Three different status codes are used for this purpose:

        SC_PROC_KEEPALIVE "100"
        SS_PROC_KEEPALIVE "processor keep alive"
        SC_PROC_ALIVE "101"
        SS_PROC_ALIVE "processor is alive"
        SC_PROC_DEAD "102"
        SS_PROC_DEAD "processor is dead"

   If we have not received packets from the peer processor for
   `no_packet_timeout`, send a SC_PROC_KEEPALIVE
   packet to the peer processor. The peer may:
  
   1. send SC_PROC_ALIVE back, then the `processor->t_packet_recv`
      is updated.
   2. send SC_PROC_DEAD back, then shutdown the processor.
   3. no response, then when if `no_packet_timeout + 30`
      threshold is reached, shutdown the processor.
*/
static int
keepalive_pulse (CcnetProcFactory *factory)
{
    time_t now = time(NULL);

    const int no_packet_timeout1 = factory->no_packet_timeout;
    const int no_packet_timeout2 = factory->no_packet_timeout + CONNECTION_TIMEOUT;
    struct list_head *pos, *tmp;
    CcnetProcessor *processor;
    int count = 0;

    /*
     * Use list_for_each_safe since we may delete an entry when looping. 
     */
    list_for_each_safe (pos, tmp, &(factory->procs_list)) {
        processor = list_entry (pos, CcnetProcessor, list);

        if (CCNET_IS_KEEPALIVE2_PROC(processor))
            continue;

        /*
         * Service proxy and stub processors should be shutdown in pair.
         * However, we cannot remove 2 procs from the procs_list in one loop.
         * This may invalidate the 'pos' pointer in case proxy and stub
         * procs are adjacent in the procs_list.
         *
         * When proxy proc is shutting down, it declares the stub becomes an
         * orphan. Then we shutdown the orphan stub here. The same thing happens
         * when stub proc shuts down.
         */
        if (CCNET_IS_SERVICE_PROXY_PROC(processor) &&
            ccnet_service_proxy_is_orphan((CcnetServiceProxyProc *)processor))
        {
            ccnet_debug ("[proc-fact] Shutdown service_proxy:%d when it's orphan\n",
                         PRINT_ID(processor->id));
            ccnet_processor_shutdown (processor, PROC_NOTSET);
            continue;
        }

        if (CCNET_IS_SERVICE_STUB_PROC(processor) &&
            ccnet_service_stub_is_orphan((CcnetServiceStubProc *)processor))
        {
            ccnet_debug ("[proc-fact] Shutdown service_stub:%d when it's orphan\n",
                         PRINT_ID(processor->id));
            ccnet_processor_shutdown (processor, PROC_NOTSET);
            continue;
        }

        if (processor->peer->is_local) {
            /* No need to call keepalive to local peer */
            continue;
        }
        
        /* a just started master processor */
        if (processor->t_packet_recv == 0) {
            g_assert (processor->start_time != 0);
            if (now - processor->start_time >= CONNECTION_TIMEOUT) {
                ccnet_debug ("[proc-fact] Shutdown processsor %s(%d) when connect timeout %ds\n",
                             GET_PNAME(processor), PRINT_ID(processor->id),
                             now - processor->start_time);
                ccnet_processor_shutdown (processor, PROC_CON_TIMEOUT);
            }
            continue;
        }

        if (now - processor->t_packet_recv <= no_packet_timeout1)
            continue;

        if (processor->t_keepalive_sent <= processor->t_packet_recv) {
            /* has not send a keepalive packet yet */
            ccnet_processor_keep_alive (processor);
            if (++count > MAX_PROCS_KEEPALIVE) break;

            continue;
        }

        /* if keepalive is already sent and timeout */
        if (now - processor->t_packet_recv > no_packet_timeout2) {
            ccnet_debug ("Shutdown processsor %s(%d) when timeout\n", 
                         GET_PNAME(processor), PRINT_ID(processor->id));
            /* receive command processor should only be called by 
               local, and they may only timeout when debuging. */
            /* g_assert (!CCNET_IS_RCVCMD_PROC(processor)); */
            ccnet_processor_shutdown (processor, PROC_TIMEOUT);
            continue;
        }

    }

    return TRUE;
}
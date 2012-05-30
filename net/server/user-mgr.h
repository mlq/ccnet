/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef CCNET_USER_MGR_H
#define CCNET_USER_MGR_H

#include <glib.h>
#include <glib-object.h>

#define CCNET_TYPE_USER_MANAGER                  (ccnet_user_manager_get_type ())
#define CCNET_USER_MANAGER(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), CCNET_TYPE_USER_MANAGER, CcnetUserManager))
#define CCNET_IS_USER_MANAGER(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CCNET_TYPE_USER_MANAGER))
#define CCNET_USER_MANAGER_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), CCNET_TYPE_USER_MANAGER, CcnetUserManagerClass))
#define CCNET_IS_USER_MANAGER_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), CCNET_TYPE_USER_MANAGER))
#define CCNET_USER_MANAGER_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), CCNET_TYPE_USER_MANAGER, CcnetUserManagerClass))


typedef struct _CcnetUserManager CcnetUserManager;
typedef struct _CcnetUserManagerClass CcnetUserManagerClass;

typedef struct CcnetUserManagerPriv CcnetUserManagerPriv;


struct _CcnetUserManager
{
    GObject         parent_instance;

    CcnetSession   *session;
    
    char           *userdb_path;
    GHashTable     *user_hash;

    CcnetUserManagerPriv *priv;
};

struct _CcnetUserManagerClass
{
    GObjectClass    parent_class;
};

GType ccnet_user_manager_get_type  (void);

CcnetUserManager* ccnet_user_manager_new (CcnetSession *);
int ccnet_user_manager_prepare (CcnetUserManager *manager);

void ccnet_user_manager_free (CcnetUserManager *manager);

void ccnet_user_manager_start (CcnetUserManager *manager);

int
ccnet_user_manager_add_emailuser (CcnetUserManager *manager, const char *email,
                                  const char *passwd, int is_staff, int is_active);

int
ccnet_user_manager_remove_emailuser (CcnetUserManager *manager, const char *email);

int
ccnet_user_manager_validate_emailuser (CcnetUserManager *manager, const char *email,
                                       const char *passwd);

CcnetEmailUser*
ccnet_user_manager_get_emailuser (CcnetUserManager *manager, const char *email);

CcnetEmailUser*
ccnet_user_manager_get_emailuser_by_id (CcnetUserManager *manager, int id);

GList*
ccnet_user_manager_get_emailusers (CcnetUserManager *manager, int start, int limit);

int
ccnet_user_manager_update_emailuser (CcnetUserManager *manager, int id,
                                     const char* passwd, int is_staff, int is_active);
                                     
int
ccnet_user_manager_add_binding (CcnetUserManager *manager, const char *email,
                                const char *peer_id);

/* Remove all bindings to an email */
int
ccnet_user_manager_remove_binding (CcnetUserManager *manager, const char *email);

/* Remove one specific peer-id binding to an email */
int
ccnet_user_manager_remove_one_binding (CcnetUserManager *manager,
                                       const char *email,
                                       const char *peer_id);

char *
ccnet_user_manager_get_binding_email (CcnetUserManager *manager, const char *peer_id);

GList *
ccnet_user_manager_get_binding_peerids (CcnetUserManager *manager, const char *email);

#endif
 #ifndef _NMDC_LOCAL_H_
#define _NMDC_LOCAL_H_

#include "proto.h"
#include "nmdc_protocol.h"

extern cache_t cache;
extern ratelimiting_t rates;
extern user_t *userlist;
extern hashlist_t hashlist;

extern user_t *HubSec;

extern unsigned int keylen;
extern unsigned int keyoffset;
extern char key[16 + sizeof (LOCK) + 4 + LOCKLENGTH + 1];

extern banlist_t reconnectbanlist;

extern unsigned int cloning;

extern unsigned int chatmaxlength;
extern unsigned int searchmaxlength;
extern unsigned int srmaxlength;
extern unsigned int researchmininterval, researchperiod, researchmaxcount;

extern unsigned char *defaultbanmessage;

/* function prototypes */
extern int proto_nmdc_setup ();
extern int proto_nmdc_init ();
extern int proto_nmdc_handle_token (user_t * u, buffer_t * b);
extern int proto_nmdc_handle_input (user_t * user, buffer_t ** buffers);
extern void proto_nmdc_flush_cache ();
extern int proto_nmdc_user_disconnect (user_t * u);
extern int proto_nmdc_user_forcemove (user_t * u, unsigned char *destination, buffer_t * message);
extern int proto_nmdc_user_redirect (user_t * u, buffer_t * message);
extern int proto_nmdc_user_drop (user_t * u, buffer_t * message);
extern user_t *proto_nmdc_user_find (unsigned char *nick);
extern user_t *proto_nmdc_user_alloc (void *priv);
extern int proto_nmdc_user_free (user_t * user);

extern user_t *proto_nmdc_user_addrobot (unsigned char *nick, unsigned char *description);
extern int proto_nmdc_user_delrobot (user_t * u);

extern int proto_nmdc_user_say (user_t * u, buffer_t * b, buffer_t * message);
extern int proto_nmdc_user_say_string (user_t * u, buffer_t * b, unsigned char *message);

extern int proto_nmdc_user_warn (user_t * u, struct timeval *now, unsigned char *message, ...);

extern int proto_nmdc_user_chat_all (user_t * u, buffer_t * message);
extern int proto_nmdc_user_send (user_t * u, user_t * target, buffer_t * message);
extern int proto_nmdc_user_send_direct (user_t * u, user_t * target, buffer_t * message);
extern int proto_nmdc_user_priv (user_t * u, user_t * target, user_t * source, buffer_t * message);
extern int proto_nmdc_user_priv_direct (user_t * u, user_t * target, user_t * source, buffer_t * message);
extern int proto_nmdc_user_raw (user_t * target, buffer_t * message);
extern int proto_nmdc_user_raw_all (buffer_t * message);

#endif

/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "plugin.h"
#include "user.h"
#include "commands.h"
#include "hub.h"
#include "proto.h"

/*********************************************************************************************/

#define CHATROOM_FLAG_AUTOJOIN_NONE		 0x1
#define CHATROOM_FLAG_AUTOJOIN_REG		 0x2
#define CHATROOM_FLAG_AUTOJOIN_RIGHTS		 0x4
#define CHATROOM_FLAG_PRIVATE			 0x8

/*********************************************************************************************/

typedef struct chatroom_member {
  struct chatroom_member *next, *prev;

  plugin_user_t *user;
} chatroom_member_t;

typedef struct chatroom {
  struct chatroom *next, *prev;

  plugin_user_t *user;
  unsigned char *name;
  unsigned char *description;
  unsigned long flags;
  unsigned long rights;
  unsigned long count;
  chatroom_member_t members;
} chatroom_t;

unsigned long pi_chatroom_event_pm (plugin_user_t * user, void *dummy, unsigned long event,
				    buffer_t * token);

/*********************************************************************************************/

plugin_t *plugin_chatroom = NULL;
chatroom_t chatrooms;

unsigned char *SaveFile;

/*********************************************************************************************/

chatroom_member_t *chatroom_member_add (chatroom_t * room, plugin_user_t * user)
{
  chatroom_member_t *member;

  member = malloc (sizeof (chatroom_member_t));
  if (!member)
    return NULL;

  member->user = user;

  member->next = room->members.next;
  member->prev = &room->members;
  member->next->prev = member;
  member->prev->next = member;

  room->count++;

  return member;
}

chatroom_member_t *chatroom_member_find (chatroom_t * room, plugin_user_t * user)
{
  chatroom_member_t *member;

  for (member = room->members.next; member != &room->members; member = member->next)
    if (member->user == user)
      return member;

  return NULL;
}


unsigned int chatroom_member_del (chatroom_t * room, chatroom_member_t * member)
{
  member->next->prev = member->prev;
  member->prev->next = member->next;

  room->count--;

  free (member);

  return 0;
}


chatroom_t *chatroom_new (unsigned char *name, unsigned long rights, unsigned long flags,
			  unsigned char *description, plugin_event_handler_t * handler)
{
  chatroom_t *room;

  /* init */
  room = malloc (sizeof (chatroom_t));
  if (!room)
    return NULL;

  room->name = strdup (name);
  room->description = strdup (description);
  room->flags = flags;
  room->rights = rights;
  room->count = 0;
  room->members.next = &room->members;
  room->members.prev = &room->members;

  room->user = plugin_robot_add (name, description, handler);

  /* link */
  room->next = chatrooms.next;
  room->prev = &chatrooms;
  room->next->prev = room;
  room->prev->next = room;

  return room;
}

chatroom_t *chatroom_find (unsigned char *name)
{
  chatroom_t *room = NULL;

  for (room = chatrooms.next; (room != &chatrooms); room = room->next)
    if (!strcasecmp (name, room->name))
      return room;

  return NULL;
}

unsigned int chatroom_del (chatroom_t * room)
{
  /* remove from list */
  room->next->prev = room->prev;
  room->prev->next = room->next;

  /* delete all users */
  while (room->members.next != &room->members)
    chatroom_member_del (room, room->members.next);

  /* delete */
  plugin_robot_remove (room->user);
  free (room->name);
  free (room->description);
  free (room);

  return 0;
}

unsigned int chatroom_save (unsigned char *filename)
{
  FILE *fp;
  chatroom_t *room;

  fp = fopen (filename, "w+");
  if (!fp)
    return errno;

  for (room = chatrooms.next; (room != &chatrooms); room = room->next) {
    fprintf (fp, "%s %lu %lu %s\n", room->name, room->flags, room->rights, room->description);
  }
  fclose (fp);

  return 0;
}

unsigned int chatroom_load (unsigned char *filename)
{
  FILE *fp;
  unsigned char buffer[1024];
  unsigned char name[NICKLENGTH];
  unsigned long flags, rights;
  int offset, i;
  chatroom_t *room;

  fp = fopen (filename, "r+");
  if (!fp)
    return 0;

  fgets (buffer, 1024, fp);
  while (!feof (fp)) {
    sscanf (buffer, "%s %lu %lu %n", name, &flags, &rights, &offset);

    /* scrap any \n's in the description. */
    for (i = offset; buffer[i] && buffer[i] != '\n'; i++);
    if (buffer[i] == '\n')
      buffer[i] = '\0';

    /* all this to make sure we do not recreate a room that hasn't changed. it would loose it's users. */
    if ((room = chatroom_find (name))) {
      if ((room->rights != rights) ||
	  (room->flags != flags) || (strcmp (room->description, buffer + offset))) {
	chatroom_del (room);
	chatroom_new (name, rights, flags, buffer + offset,
		      (plugin_event_handler_t *) & pi_chatroom_event_pm);
      }
    } else
      chatroom_new (name, rights, flags, buffer + offset,
		    (plugin_event_handler_t *) & pi_chatroom_event_pm);

    fgets (buffer, 1024, fp);
  }

  fclose (fp);

  return 0;
}

/********************************** COMMAND HANDLERS ********************************************/

unsigned long pi_chatroom_handler_roomadd (plugin_user_t * user, buffer_t * output, void *dummy,
					   unsigned int argc, unsigned char **argv)
{
  unsigned long roomflags = 0, roomrights = 0, ncap = 0, counter = 0;

  if (argc < 3) {
    bf_printf (output,
	       "Usage: %s <room> <description> [<private>] [<autoreg>] [<autorights>] [<rights <rights>...>]\n"
	       " supply the flag \"private\" if you want the room autojoin or invite only.\n"
	       " supply the flag \"autoreg\" if you only want registered users to autojoin.\n"
	       " supply the flag \"autorights\" if you to autojoin all users with correct rights.\n"
	       " supply the flag \"rights\" if you want to autojoin users based on their rights, must be last argument!\n"
	       " be sure to add a description between \". For an empty description, provide \"\".",
	       argv[0]);
    return 0;
  };

  if (strlen (argv[1]) > NICKLENGTH) {
    bf_printf (output, "Chatroom name %s is too long. Max %d characters\n", argv[1], NICKLENGTH);
    return 0;
  }

  if (chatroom_find (argv[1])) {
    bf_printf (output, "Chatroom %s already exists.", argv[1]);
    return 0;
  }

  if (argc > 3) {
    counter = 3;
    while ((argc > counter) && (strcmp (argv[counter], "rights"))) {
      if (!strcmp (argv[counter], "private")) {
	roomflags |= CHATROOM_FLAG_PRIVATE;
	bf_printf (output, "Room is private.\n");
	counter++;
	continue;
      }
      if (!strcmp (argv[counter], "autoreg")) {
	roomflags |= CHATROOM_FLAG_AUTOJOIN_REG;
	bf_printf (output, "Room will autojoin registered users.\n");
	counter++;
	continue;
      }
      if (!strcmp (argv[counter], "autorights")) {
	roomflags |= CHATROOM_FLAG_AUTOJOIN_REG;
	bf_printf (output, "Room will autojoin users with sufficient rights.\n");
	counter++;
	continue;
      }
      if (!strcmp (argv[counter], "rights"))
	break;
      bf_printf (output, "Ignoring unknown argument %s\n", argv[counter++]);
    }

    if ((argc > counter) && (!strcmp (argv[counter], "rights"))) {
      roomflags |= CHATROOM_FLAG_AUTOJOIN_RIGHTS;
      counter++;
      if (argc > counter)
	command_flags_parse ((command_flag_t *) Capabilities, output, argc, argv, counter,
			     &roomrights, &ncap);
      roomrights &= ~ncap;
      bf_printf (output, "Room requires: ");
      command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), output,
			   roomrights);
      bf_strcat (output, "\n");
    }
  }

  if (!chatroom_new
      (argv[1], roomrights, roomflags, argv[2],
       (plugin_event_handler_t *) & pi_chatroom_event_pm)) {
    bf_printf (output, "Room creation failed!\n");
  } else {
    bf_printf (output, "Room %s created successfully!\n", argv[1]);
  }

  return 0;
}

unsigned long pi_chatroom_handler_roomdel (plugin_user_t * user, buffer_t * output, void *dummy,
					   unsigned int argc, unsigned char **argv)
{
  chatroom_t *room;

  if (argc < 2) {
    bf_printf (output, "Usage: %s <room>\n", argv[0]);
    return 0;
  };

  room = chatroom_find (argv[1]);
  if (!room) {
    bf_printf (output, "Cannot find room %s\n", argv[1]);
    return 0;
  }
  chatroom_del (room);

  return 0;
}

unsigned int pi_chatroom_show (buffer_t * buf, chatroom_t * room)
{
  bf_printf (buf, "Room %s has %d users", room->name, room->count);
  if (room->flags & CHATROOM_FLAG_PRIVATE)
    bf_printf (buf, ", is private");
  if (room->flags & CHATROOM_FLAG_AUTOJOIN_REG)
    bf_printf (buf, ", autojoins registered users");
  if (room->flags & CHATROOM_FLAG_AUTOJOIN_RIGHTS)
    bf_printf (buf, ", autojoins users with sufficient rights");
  if (room->flags & CHATROOM_FLAG_AUTOJOIN_RIGHTS) {
    bf_printf (buf, " and requires rights ");
    command_flags_print ((command_flag_t *) (Capabilities + CAP_PRINT_OFFSET), buf, room->rights);
  }
  bf_strcat (buf, ".\n");

  return 0;
}

unsigned long pi_chatroom_handler_roomlist (plugin_user_t * user, buffer_t * output, void *dummy,
					    unsigned int argc, unsigned char **argv)
{
  unsigned int count;
  chatroom_t *room;

  if (argc > 2) {
    count = 2;
    while (count < argc) {
      room = chatroom_find (argv[count]);
      if (!room) {
	bf_printf (output, "Cannot find room %s\n", argv[count]);
	return 0;
      }
      pi_chatroom_show (output, room);
      count++;
    }
  }

  for (room = chatrooms.next; (room != &chatrooms); room = room->next)
    pi_chatroom_show (output, room);

  return 0;
}

unsigned long pi_chatroom_handler_userstat (plugin_user_t * user, buffer_t * output, void *dummy,
					    unsigned int argc, unsigned char **argv)
{
  return 0;
}


/********************************* EVENT HANDLERS *************************************/

unsigned long pi_chatroom_event_login (plugin_user_t * user, buffer_t * output, void *dummy,
				       unsigned long event, buffer_t * token)
{
  chatroom_t *room;
  chatroom_member_t *member;

  /* find room */
  for (room = chatrooms.next; room != &chatrooms; room = room->next) {
    if (room->flags & CHATROOM_FLAG_AUTOJOIN_NONE)
      continue;

    if ((room->flags & CHATROOM_FLAG_AUTOJOIN_REG) && (!(user->flags & PROTO_FLAG_REGISTERED)))
      continue;

    if ((room->flags & CHATROOM_FLAG_AUTOJOIN_RIGHTS)
	&& (!((user->rights & room->rights) == room->rights)))
      continue;

    member = chatroom_member_add (room, user);
  }
  return PLUGIN_RETVAL_CONTINUE;
}

unsigned long pi_chatroom_event_logout (plugin_user_t * user, buffer_t * output, void *dummy,
					unsigned long event, buffer_t * token)
{
  chatroom_t *room;
  chatroom_member_t *member = dummy;

  /* find room */
  for (room = chatrooms.next; room != &chatrooms; room = room->next) {
    member = chatroom_member_find (room, user);
    if (member)
      chatroom_member_del (room, member);
  }

  return PLUGIN_RETVAL_CONTINUE;
}

unsigned long pi_chatroom_event_pm (plugin_user_t * user, void *dummy, unsigned long event,
				    buffer_t * token)
{
  chatroom_t *room;
  unsigned char *n;
  plugin_user_t *source;
  chatroom_member_t *m, *member;
  buffer_t *buf;

  /* only interested in PMs */
  if (event != PLUGIN_EVENT_PM_IN)
    return PLUGIN_RETVAL_CONTINUE;

  /* find room */
  for (room = chatrooms.next; room != &chatrooms; room = room->next)
    if (user == room->user)
      break;

  /* no room found? */
  if (room == &chatrooms)
    return PLUGIN_RETVAL_CONTINUE;

  /* reformat and send pm */
  buf = bf_copy (token, 0);

  /* skip from field. */
  buf->s = strstr (buf->s, "From:");
  buf->s += 6;

  n = buf->s;

  /* skip nick and space */
  buf->s = strchr (buf->s, ' ');
  *buf->s++ = '\0';

  /* skip until after nick */
  buf->s = strchr (buf->s, '$');
  buf->s = strchr (buf->s, ' ');
  buf->s++;

  /* find user. */
  source = plugin_user_find (n);
  if (!source)
    goto leave;

  /* verify user is allowed to talk to room. */
  if (!(source->flags & PLUGIN_FLAG_HUBSEC)) {
    member = chatroom_member_find (room, source);
    if (!member) {
      /* not a member.  */

      /* exit if the room is private. */
      if (room->flags & CHATROOM_FLAG_PRIVATE)
	return PLUGIN_RETVAL_CONTINUE;

      /* does he have the necessary rights? */
      if (!((user->rights & room->rights) == room->rights))
	return PLUGIN_RETVAL_CONTINUE;

      /* if so, add him. */
      member = chatroom_member_add (room, source);
      if (!member)
	return PLUGIN_RETVAL_CONTINUE;
    }
  }

  /* send as pm to all users */
  for (m = room->members.next; m != &(room->members); m = m->next)
    if (m->user != source) {
      plugin_user_priv (room->user, m->user, source, buf, 0);
    }

leave:
  /* free the buffer */
  bf_free (buf);

  return PLUGIN_RETVAL_CONTINUE;
}

unsigned long pi_chatroom_event_save (plugin_user_t * user, buffer_t * output, void *dummy,
				      unsigned long event, buffer_t * token)
{
  chatroom_save (SaveFile);
  return PLUGIN_RETVAL_CONTINUE;
}

unsigned long pi_chatroom_event_load (plugin_user_t * user, buffer_t * output, void *dummy,
				      unsigned long event, buffer_t * token)
{
  chatroom_load (SaveFile);
  return PLUGIN_RETVAL_CONTINUE;
}


/********************************* INIT *************************************/

int pi_chatroom_init ()
{
  chatrooms.next = &chatrooms;
  chatrooms.prev = &chatrooms;

  SaveFile = strdup ("chatroom.conf");

  /* config_register ("chatroom.file",  CFG_ELEM_STRING, &SaveFile, "Save file for chatrooms."); */

  plugin_chatroom = plugin_register ("chatroom");

  plugin_request (plugin_chatroom, PLUGIN_EVENT_LOGIN,
		  (plugin_event_handler_t *) & pi_chatroom_event_login);
  plugin_request (plugin_chatroom, PLUGIN_EVENT_LOGOUT,
		  (plugin_event_handler_t *) & pi_chatroom_event_logout);
  plugin_request (plugin_chatroom, PLUGIN_EVENT_SAVE,
		  (plugin_event_handler_t *) & pi_chatroom_event_save);
  plugin_request (plugin_chatroom, PLUGIN_EVENT_LOAD,
		  (plugin_event_handler_t *) & pi_chatroom_event_load);

  command_register ("chatroomadd", &pi_chatroom_handler_roomadd, CAP_CONFIG, "Add a chatroom.");
  command_register ("chatroomdel", &pi_chatroom_handler_roomdel, CAP_CONFIG, "Remove a chatroom.");
  command_register ("chatroomlist", &pi_chatroom_handler_roomlist, CAP_CONFIG,
		    "Lists available chatrooms.");

  return 0;
}
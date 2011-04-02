/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "nmdc_token.h"

/*
	Protocol Definition
*/

/* *INDENT-OFF* */
struct token_definition Tokens[] = {
	{ TOKEN_CHAT, 			 1, "<" },
	{ TOKEN_MYINFO,			 8, "$MyINFO "},
	{ TOKEN_MYPASS,			 8, "$MyPass "},
	{ TOKEN_MULTISEARCH,		13, "$MultiSearch "},
	{ TOKEN_MULTICONNECTTOME,	18, "$MultiConnectToMe "},
	{ TOKEN_SEARCH,			 8, "$Search "},
	{ TOKEN_SR,			 4, "$SR "},
	{ TOKEN_SUPPORTS, 		10, "$Supports "},
	{ TOKEN_LOCK, 			 6, "$Lock "} ,
	{ TOKEN_KEY, 			 5, "$Key "},
	{ TOKEN_KICK,			 6, "$Kick "},
	{ TOKEN_HELLO, 			 7, "$Hello "},
	{ TOKEN_GETINFO, 		 9, "$GetINFO "},
	{ TOKEN_GETNICKLIST, 		12, "$GetNickList"},
	{ TOKEN_CONNECTTOME, 		13, "$ConnectToMe "},
	{ TOKEN_REVCONNECTOTME, 	16, "$RevConnectToMe "},
	{ TOKEN_TO, 			 5, "$To: "},
	{ TOKEN_QUIT,			 6, "$Quit "},
	{ TOKEN_OPFORCEMOVE,		13, "$OpForceMove "},
	{ TOKEN_VALIDATENICK,		14, "$ValidateNick "},
	{ TOKEN_BOTINFO,		 9, "$BotINFO "},
	{ TOKEN_NUM, 			 0, NULL}
};
/* *INDENT-ON* */

/* local tables */

unsigned char *hashtable;

#define HASH_EXT(string) ((tolower (*(string+1)) - 'a')&0x1F)

/*
	TOKENISER
*/

void token_init ()
{
  int i, h;
  unsigned char ht[256];

  /* EXTERNDED PROTOCOL TOKEN HASHTABLE */
  memset (ht, 0, 256);
  /* start from 1 to skip < */
  for (i = 1; i < (TOKEN_NUM - 1); i++) {
    h = HASH_EXT (Tokens[i].identifier);
    if (ht[h] == 0)
      ht[h] = i;
  }
  hashtable = malloc (32);
  memcpy (hashtable, ht, 32);
}


int token_parse (struct token *token, unsigned char *string)
{
  int i, h;

  if (!*string)
    return TOKEN_UNIDENTIFIED;

  token->token = string;

  /* filter out the chat lines */
  if (string[0] == '<') {
    token->type = TOKEN_CHAT;
    token->argument = string;
    return TOKEN_CHAT;
  };

  /* calculate hash and look for token in the table */
  h = HASH_EXT (string);
  i = hashtable[h];
  if (!i)
    return TOKEN_UNIDENTIFIED;
  do {
    if (!strncmp (Tokens[i].identifier, string, Tokens[i].len)) {
      /* matches */
      token->type = Tokens[i].num;
      token->argument = string + Tokens[i].len;
      return Tokens[i].num;
    }
  } while (Tokens[++i].identifier && (HASH_EXT (Tokens[i].identifier) == h));

  return TOKEN_UNIDENTIFIED;
}

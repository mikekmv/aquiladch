/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _TOKEN_H_
#define _TOKEN_H_

enum {
  TOKEN_UNIDENTIFIED = 0,
  TOKEN_CHAT,
  TOKEN_MYINFO,
  TOKEN_MYPASS,
  TOKEN_MULTISEARCH,
  TOKEN_MULTICONNECTTOME,
  TOKEN_SEARCH,
  TOKEN_SR,
  TOKEN_SUPPORTS,
  TOKEN_LOCK,
  TOKEN_KEY,
  TOKEN_KICK,
  TOKEN_HELLO,
  TOKEN_GETNICKLIST,
  TOKEN_GETINFO,
  TOKEN_CONNECTTOME,
  TOKEN_REVCONNECTOTME,
  TOKEN_TO,
  TOKEN_QUIT,
  TOKEN_OPFORCEMOVE,
  TOKEN_VALIDATENICK,
  TOKEN_BOTINFO,
  TOKEN_NUM
};

typedef struct token_definition {
  unsigned short num;
  unsigned short len;
  unsigned char *identifier;
} token_definition_t;

typedef struct token {
  unsigned short type;
  unsigned char *token, *argument;
} token_t;

extern struct token_definition Tokens[];

void token_init ();
int token_parse (struct token *token, unsigned char *string);

#endif /* _TOKEN_H_ */

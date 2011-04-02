/*                                                                                                                                    
 *  (C) Copyright 2006 Johan Verrept (Johan.Verrept@advalvas.be)                                                                      
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  distribution for more details.     
 *  
 */

#ifndef _CORE_CONFIG_H_
#define _CORE_CONFIG_H_

#include "defaults.h"

typedef struct {
  /* hub data */
  unsigned int ListenPort;
  unsigned char *NMDCExtraPorts;
  unsigned char *HubName;
  unsigned char *HubDesc;
  unsigned char *HubOwner;
  unsigned char *Hostname;
  unsigned char *HubSecurityNick;
  unsigned char *HubSecurityDesc;

  /* protocol settings */
  unsigned int MaxDescriptionLength;
  unsigned int MaxTagLength;
  unsigned int MaxSpeedLength;
  unsigned int MaxEMailLength;
  unsigned int MaxShareLength;

  /* user specific settings */
  unsigned long DefaultRights;
  unsigned long defaultKickPeriod;

  /* more settings */
  unsigned char *Redirect;
} config_t;

extern config_t config;

extern int core_config_init ();

#endif /* _CORE_CONFIG_H_ */

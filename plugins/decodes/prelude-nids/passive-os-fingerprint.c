/*****
*
* Copyright (C) 2003 Laurent Oudot <oudot.laurent@wanadoo.fr>
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
#include <stdlib.h>
#include <assert.h>

#include <libprelude/prelude-log.h>
#include <libprelude/list.h>
#include <libprelude/idmef-tree.h>
#include <libprelude/idmef-tree-func.h>

#include "passive-os-fingerprint.h"



void passive_os_fingerprint_zero(pof_host_data_t *p)
{	
	p->mss= -1;
	p->timestamp = 0;
	p->wscale = -1;
	p->sackok = 0;
	p->nop = 0;
	p->win = 0;
	p->ttl = 0;
	p->df = 0;
	p->flags = 0;
	p->len = 0;
}




void passive_os_fingerprint_dump(idmef_alert_t *alert, pof_host_data_t *pof)
{
        int ret;
        char MSS[5];
	char WSS[3];
	idmef_additional_data_t *data;
        static char fingerprint[FINGERPRINT_SIZE + 1];
        
        if ( ! (pof->flags == 'A' || pof->flags == 'S') )
                return;
        
	data = idmef_alert_additional_data_new(alert);
	if ( ! data )
		return;
        
	if ( pof->wscale >= 0 )
		snprintf(WSS, sizeof(WSS), "%02d", pof->wscale);
	else
		sprintf(WSS, "WS");

	if ( pof->mss >= 0 )
		snprintf(MSS, sizeof(MSS), "%04X", pof->mss);
	else
		sprintf(MSS, "_MSS");

	ret = snprintf(fingerprint, sizeof(fingerprint), "%04X:%s:%02X:%s:%d:%d:%d:%d:%c:%02X",
                       pof->win, MSS, pof->ttl, WSS, pof->sackok, pof->nop,
                       pof->df, pof->timestamp, pof->flags, pof->len);

        /*
         * return -1 if the output was truncated due to this limit. (Thus until
         * glibc 2.0.6. Since glibc 2.1 these functions follow the
         * C99 standard and return the number of characters (excluding
         * the trailing '\0') which would have been written to
         * the final string if enough space had been available.)
         */
        assert(ret > 0 && ret < sizeof(fingerprint));
        
	idmef_string_set_constant(&data->meaning, "Passive OS Fingerprint");
	idmef_additional_data_set_data(data, string, fingerprint, ret + 1);
}
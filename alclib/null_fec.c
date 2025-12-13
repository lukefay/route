/** \file null_fec.c \brief Compact No-Code FEC
 *
 *  $Author: peltotal $ $Date: 2007/02/28 08:58:00 $ $Revision: 1.33 $
 *
 *  MAD-ALCLIB: Implementation of ALC/LCT protocols, Compact No-Code FEC,
 *  Simple XOR FEC, Reed-Solomon FEC, and RLC Congestion Control protocol.
 *  Copyright (c) 2003-2007 TUT - Tampere University of Technology
 *  main authors/contacts: jani.peltotalo@tut.fi and sami.peltotalo@tut.fi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  In addition, as a special exception, TUT - Tampere University of Technology
 *  gives permission to link the code of this program with the OpenSSL library (or
 *  with modified versions of OpenSSL that use the same license as OpenSSL), and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify this file, you may extend this exception to your version
 *  of the file, but you are not obligated to do so. If you do not wish to do so,
 *  delete this exception statement from your version.
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <memory.h>
#include <assert.h>

#include "null_fec.h"

 /**
  * FEC variables semaphore
  */

#ifdef _MSC_VER
RTL_CRITICAL_SECTION fec_variables_semaphore;
#else
pthread_mutex_t fec_variables_semaphore = PTHREAD_MUTEX_INITIALIZER;
#endif

/**
 * This is a private function, which locks the FEC core functions.
 *
 */

void lock_fec(void) {
#ifdef _MSC_VER
	EnterCriticalSection(&fec_variables_semaphore);
#else
	pthread_mutex_lock(&fec_variables_semaphore);
#endif
}

/**
 * This is a private function, which unlocks the FEC core functions.
 *
 */

void unlock_fec(void) {
#ifdef _MSC_VER
	LeaveCriticalSection(&fec_variables_semaphore);
#else
	pthread_mutex_unlock(&fec_variables_semaphore);
#endif
}

void initialize_fec(void) {
#ifdef _MSC_VER
	InitializeCriticalSection(&fec_variables_semaphore);
#else
#endif
}

void release_fec(void) {
#ifdef _MSC_VER
	DeleteCriticalSection(&fec_variables_semaphore);
#else
#endif
}


trans_block_t* null_fec_encode_src_block(char *data, unsigned long long len,
										 unsigned int sbn, unsigned short es_len) {
	
	trans_block_t *tr_block;		/* transport block struct */
	trans_unit_t *tr_unit;			/* transport unit struct */
	unsigned int nb_of_units;		/* number of units */
	unsigned int i;					/* loop variables */
	unsigned long long data_left;
	char *ptr;						/* pointer to left data */

	lock_fec();

	data_left = len;

	nb_of_units = (unsigned int)ceil((double)(unsigned int)len / (double)es_len);

	tr_block = create_block();

	if(tr_block == NULL) {
		return tr_block;
	}

	tr_unit = create_units(nb_of_units);

	if(tr_unit == NULL) {
		free(tr_block);
		unlock_fec();
		return NULL;
	}

	ptr = data;

	tr_block->unit_list = tr_unit;
	tr_block->sbn = sbn;
	tr_block->n = nb_of_units;
	tr_block->k = nb_of_units;

	//Malek El Khatib
	unsigned short es0_len;
	//End
	for(i = 0; i < nb_of_units; i++) {

		//Malek El Khatib
		//We want to send esi=0 at the end to simulate transmission during the live prodution of a media file
		//Only after the whole media file is created do we know the metadata for this file which is populated in esi=0
		if ( i == 0 && nb_of_units > 1)
		{
			es0_len = data_left < es_len ? (unsigned short)data_left : es_len;
			ptr += es0_len;
			data_left -= es0_len;
			continue;
		}
		///End

		tr_unit->esi = i;
		tr_unit->len = data_left < es_len ? (unsigned short)data_left : es_len; /*min(eslen, data_left);*/


		/* Alloc memory for TU data */
		if(!(tr_unit->data = (char*)calloc(tr_unit->len, sizeof(char)))) {
			printf("Could not alloc memory for transport unit's data!\n");
			
			tr_unit = tr_block->unit_list;	

			while(tr_unit != NULL) {
				free(tr_unit->data);
				tr_unit++;
			}
	
			free(tr_block->unit_list);
			free(tr_block);
			unlock_fec();
			return NULL;
		}

		memcpy(tr_unit->data, ptr, tr_unit->len);
		
		ptr += tr_unit->len;
		data_left -= tr_unit->len;
		tr_unit++;
	}

	//Malek El Khatib 25.07.2014
	//Start
	if (nb_of_units > 1)
	{
		ptr = data;
		tr_unit->esi = 0;
		tr_unit->len =  es0_len;

		if(!(tr_unit->data = (char*)calloc(tr_unit->len, sizeof(char)))) {
			printf("Could not alloc memory for transport unit's data!\n");

			tr_unit = tr_block->unit_list;	

			while(tr_unit != NULL) {
				free(tr_unit->data);
				tr_unit++;
			}

			free(tr_block->unit_list);
			free(tr_block);
			unlock_fec();
			return NULL;
		}
		memcpy(tr_unit->data, ptr, tr_unit->len);
	}
	//End

	unlock_fec();
	return tr_block;
}

char *null_fec_decode_src_block(trans_block_t *tr_block, unsigned long long *block_len,
								unsigned long long eslen) {

	char *buf = NULL; /* buffer where to construct the source block from data units */

    trans_unit_t *next_tu;
    trans_unit_t *tu;

	unsigned long long len;
	unsigned long long tmp;

	lock_fec();
	len = eslen;
	//len = eslen*tr_block->k;
	//len = ceil(eslen / tr_block->k);
	//len = min(tr_block->k * tr_block->unit_list->len, eslen);
	//printf("xfer len %llu, #units %d, #eslen %u\n", eslen, tr_block->k, tr_block->unit_list->len);
	//fflush(stdout);

	/* Allocate memory for buf */
	//if (!(buf = (char*)calloc((unsigned int)(eslen + 1), sizeof(char)))) {	// length +1 for NULL character
	if (!(buf = (char*)calloc((unsigned int)len, sizeof(char)))) {	// Length +1 for the NULL character
        printf("NULL FEC Could not alloc memory for buf!\n");
		fflush(stdout);

		unlock_fec();
        return NULL;
    }

    tmp = 0;
	
	next_tu = tr_block->unit_list;
	//printf("FEC #units: %u, #symbols: %u\n", tr_block->nb_of_rx_units, tr_block->nb_of_rx_symbols);
	//fflush(stdout);

	while (next_tu != NULL) {

        tu = next_tu;

		//printf("FEC eslen %llu, tu->len %u\n", eslen, tu->len);
		//fflush(stdout);
		
		if (tu->data == NULL) {
			//printf("SB: %u, esi: %u, len: %u\n", tr_block->sbn, tu->esi, tu->len);
			printf("Buffer Length: %llu, Unit Length: %u Build: %llu\n", eslen, tu->len, tmp);
			fflush(stdout);

			unlock_fec();
			return NULL;
		}
		else {
			memcpy((buf + tmp), tu->data, tu->len);
		}

        tmp += tu->len;

        next_tu = tu->next;
	}

	//*block_len = eslen;
	*block_len = tmp;
	//printf("NULL FEC SRC Block return length %llu\n", tmp);
	//fflush(stdout);

#ifndef USE_RETRIEVE_UNIT
	free(tu->data);
	tu = NULL;
#endif

	unlock_fec();
	return buf;
}

char *null_fec_decode_object(trans_obj_t *to, unsigned long long *data_len,
							 alc_session_t *s) {
	
	char *object = NULL;
	char *block = NULL;

	trans_block_t *tb;

	unsigned long long to_data_left;
	unsigned long long len;
	unsigned long long block_len;
	unsigned long long position;
	unsigned int i;

	//lock_fec();
	//printf("FEC DECODE Symbol Length %u, Object length %llu\n", to->es_len, to->len);
	//fflush(stdout);

	/* Allocate memory for buf */
	if(!(object = (char*)calloc((unsigned int)(to->len+1), sizeof(char)))) {
		printf("Could not alloc memory for object!\n");
		*data_len = 0;

		//unlock_fec();
		return NULL;
	}
	
	to_data_left = to->len;

	tb = to->block_list;
	//tb = s->fdt_list->block_list;

	position = 0;
	//printf("FEC Object %llu Decode of length %u...or %u\n", to->toi, to->block_list->unit_list->len, s->fdt_list->block_list->unit_list->len);
	//fflush(stdout);
	for(i = 0; i < to->bs->N; i++) {
		//block = null_fec_decode_src_block(tb, &block_len, (unsigned short)to->es_len);	// FLUTE operation
		block = null_fec_decode_src_block(tb, &block_len, to->len);					// ROUTE operation
		//block = null_fec_decode_src_block(tb, &block_len, tb->unit_list->len);		// ROUTE operation

		/* the last packet of the last source block might be padded with zeros */
		len = to_data_left < block_len ? to_data_left : block_len;

		assert (0 <= position);
		assert (position < to->len+1);
		assert (len <= (to->len-position));

		//printf("BLOCK LENGTH %lld, block: %s\n", len, block);
		//fflush(stdout);

		memcpy(object+(unsigned int)position, block, (unsigned int)len);
		position += len;
		to_data_left -= len;

		//printf("object %s", object);
		//fflush(stdout);
		free(block);

		//printf("INCREMENT BLOCK LIST %d\n", to->bs->N);
		//fflush(stdout);

		//tb = s->fdt_list->block_list+(i+1);
		tb = to->block_list + (i + 1);
		//tb++;
	}

	*data_len = to->len;

	//unlock_fec();
	return object;
}

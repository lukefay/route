/** \file blocking_alg.c \brief Algorithm for computing source block structure
 *
 *  $Author: peltotal $ $Date: 2007/02/28 08:58:00 $ $Revision: 1.26 $
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

#include "blocking_alg.h"
#include "mad.h"

blocking_struct_t* compute_blocking_structure(unsigned long long L,
					      unsigned int B, unsigned int E) {
  
  unsigned int T;
  blocking_struct_t *bs;
  lldiv_t div_T;
  div_t div_N;
  div_t div_A;
  
  if (!(bs = (blocking_struct_t*)calloc(1, sizeof(blocking_struct_t)))) {
    printf("Could not alloc memory for blocking_struct!\n");
    return NULL;
  }
  
  /* (a) */
  
  div_T = lldiv(L, E);
  
  if(div_T.rem == 0) {
    T = (unsigned int)div_T.quot;
  }
  else {
    T = (unsigned int)div_T.quot + 1;
  }
  
  /* (b) */
  
  div_N = div(T, B);
  
  if(div_N.rem == 0) {
    bs->N = div_N.quot;
  }
  else {
    bs->N = div_N.quot + 1;
  }
  
  /* (c) */
  
  div_A = div(T, bs->N);
  
  if(div_A.rem == 0) {
    bs->A_large = div_A.quot;
  }
  else {
    bs->A_large = div_A.quot + 1;
  }
  
  /* (d) */
  
  bs->A_small = div_A.quot;
  
  /* (e) */
  
  //bs->I = div_A.rem;	
  bs->I = E;    // Keep current symbol length

  return bs;
} 

update_blocking_structure(blocking_struct_t* bs, unsigned long long L,
    unsigned int B, unsigned int E) {

    unsigned int T;
    unsigned int e = E;
    BOOL retval = FALSE;
    lldiv_t div_T;
    div_t div_N;
    div_t div_A;

    if (E < bs->I) {    // If current symbol length < previous, short packet
        //printf("end of file of length %llu\n", L);
        //fflush(stdout);
        //L = E;
        E = bs->I;
        retval = TRUE;
    }

    /* (a) */

    div_T = lldiv(L, E);

    if (div_T.rem == 0) {
        T = (unsigned int)div_T.quot;
    }
    else {
        T = (unsigned int)div_T.quot + 1;
    }
    //printf("div_T %d.%d\n", div_T.quot, div_T.rem);   // remainder is in units of E, i.e. # of E symbols.  
    //fflush(stdout);

    /* (b) */

    div_N = div(T, B);

    if (div_N.quot == 0) {
        bs->N = div_N.quot + 1;
    }
    else {
        bs->N = div_N.quot;
    }
    //printf("div_N %d.%d\n", div_N.quot, div_N.rem);   // remainder is in units of B, i.e. # of B symbols
    //fflush(stdout);
    /* (c) */

    div_A = div(T, bs->N);
    
    //if (E >= bs->I) {
    if (E >= div_A.rem && !retval) {
        if (div_A.quot == 0) {
            bs->A_large = div_A.quot + 1;
        }
        else {
            bs->A_large = div_A.quot;
        }
    }
    //printf("div_A %d.%d\n", div_A.quot, div_A.rem);   // remainder is in units of N, i.e. # of N blocks
    //fflush(stdout);

    /* (d) */

    //if (E >= bs->I) {
    if (E >= div_A.rem) {
        bs->A_small = div_A.quot;
    }

    /* (e) */

    if ((div_N.quot > 0) && (div_N.rem > 0)) {
        bs->N++;
    }

    //bs->I = div_A.rem;
    bs->I = e;
    if (retval) {    // If current symbol length < previous, short packet
        bs->I = div_N.rem;
        //bs->A_large = div_N.rem;
        //bs->A_small = div_N.rem;
    }

    return retval;
}

/************************************************************************
 *
 *  sac.c, part of tmn (TMN encoder)
 *
 *  Copyright (C) 1997  University of BC, Canada
 *
 *  Contacts: 
 *  Michael Gallant                   <mikeg@ee.ubc.ca>
 *  Guy Cote                          <guyc@ee.ubc.ca>
 *  Berna Erol                        <bernae@ee.ubc.ca>
 *
 *  UBC Image Processing Laboratory   http://www.ee.ubc.ca/image
 *  2356 Main Mall                    tel.: +1 604 822 4051
 *  Vancouver BC Canada V6T1Z4        fax.: +1 604 822 5949
 *
 *  Copyright (C) 1995, 1996  Telenor R&D, Norway
 *
 *  Contacts:
 *  Robert Danielsen                  <Robert.Danielsen@nta.no>
 *
 *  Telenor Research and Development  http://www.nta.no/brukere/DVC/
 *  P.O.Box 83                        tel.:   +47 63 84 84 00
 *  N-2007 Kjeller, Norway            fax.:   +47 63 81 00 76
 *
 ************************************************************************/

/* Disclaimer of Warranty
 * 
 * These software programs are available to the user without any license fee
 * or royalty on an "as is" basis. The University of British Columbia
 * disclaims any and all warranties, whether express, implied, or
 * statuary, including any implied warranties or merchantability or of
 * fitness for a particular purpose.  In no event shall the
 * copyright-holder be liable for any incidental, punitive, or
 * consequential damages of any kind whatsoever arising from the use of
 * these programs.
 * 
 * This disclaimer of warranty extends to the user of these programs and
 * user's customers, employees, agents, transferees, successors, and
 * assigns.
 * 
 * The University of British Columbia does not represent or warrant that the
 * programs furnished hereunder are free of infringement of any
 * third-party patents.
 * 
 * Commercial implementations of H.263, including shareware, are subject to
 * royalty fees to patent holders.  Many of these patents are general
 * enough such that they are unavoidable regardless of implementation
 * design.
 * 
 */

/*********************************************************************
 *
 * SAC Encoder Module
 * Algorithm as specified in H263 (Annex E)
 *         (c) BT Labs 1995
 *
 * Author: Pat Mulroy <pmulroy@visual.bt.co.uk>
 *
 *********************************************************************/

#include <stdio.h>
#include "sim.h"

#define   q1    16384
#define   q2    32768
#define   q3    49152
#define   top   65535

static long low = 0, high = top, opposite_bits = 0, length = 0, zerorun = 0;

/*********************************************************************
 *
 *      Name:           AR_Encode
 *
 *      Description:    Encodes a symbol using syntax based arithmetic
 *        coding. Algorithm specified in H.263 (Annex E).
 *
 *      Input:          Array holding cumulative frequency data.
 *        Index into specific cumulative frequency array.
 *                      Static data for encoding endpoints.
 *
 *      Returns:        Number of bits used while encoding symbol.
 *
 *      Side Effects:   Modifies low, high, length and opposite_bits
 *        variables.
 *
 *      Author:         pmulroy@visual.bt.co.uk
 *
 *********************************************************************/

int AR_Encode (int index, int cumul_freq[])
{
  int bitcount = 0;

  if (index < 0)
    return -1;                  /* Escape Code */

  length = high - low + 1;
  high = low - 1 + (length * cumul_freq[index]) / cumul_freq[0];
  low += (length * cumul_freq[index + 1]) / cumul_freq[0];

  for (;;)
  {
    if (high < q2)
    {
      bitcount += bit_opp_bits (0);
    } else if (low >= q2)
    {
      bitcount += bit_opp_bits (1);
      low -= q2;
      high -= q2;
    } else if (low >= q1 && high < q3)
    {
      opposite_bits += 1;
      low -= q1;
      high -= q1;
    } else
      break;

    low *= 2;
    high = 2 * high + 1;
  }
  return bitcount;
}

int bit_opp_bits (int bit)      /* Output a bit and the following opposite
                                 * bits */
{
  int bitcount = 0;

  bitcount = bit_in_psc_layer (bit);

  while (opposite_bits > 0)
  {
    bitcount += bit_in_psc_layer (!bit);
    opposite_bits--;
  }
  return bitcount;
}

/*********************************************************************
 *
 *      Name:           encoder_flush
 *
 *      Description:    Completes arithmetic coding stream before any
 *        fixed length codes are transmitted.
 *
 *      Input:          None
 *
 *      Returns:        Number of bits used.
 *
 *      Side Effects:   Resets low, high, zerorun and opposite_bits
 *        variables.
 *
 *      Author:         pmulroy@visual.bt.co.uk
 *
 *********************************************************************/

int encoder_flush ()
{
  int bitcount = 0;

  if (trace)
    fprintf (tf, "encoder_flush:\n");

  opposite_bits++;
  if (low < q1)
  {
    bitcount += bit_opp_bits (0);
  } else
  {
    bitcount += bit_opp_bits (1);
  }
  low = 0;
  high = top;

  zerorun = 0;

  return bitcount;
}

/*********************************************************************
 *
 *      Name:           bit_in_psc_layer
 *
 *      Description:    Inserts a bit into output bitstream and avoids
 *        picture start code emulation by stuffing a one
 *        bit.
 *
 *      Input:          Bit to be output.
 *
 *      Returns:        Nothing
 *
 *      Side Effects:   Updates zerorun variable.
 *
 *      Author:         pmulroy@visual.bt.co.uk
 *
 *********************************************************************/

int bit_in_psc_layer (int bit)
{
  void putbits (int, int);
  int bitcount = 0;

  if (zerorun > 13)
  {
    if (trace)
      fprintf (tf, "PSC emulation ... Bit stuffed.\n");
    putbits (1, 1);
    bitcount++;
    zerorun = 0;
  }
  putbits (1, bit);
  bitcount++;

  if (bit)
    zerorun = 0;
  else
    zerorun++;

  return bitcount;
}

/*********************************************************************
 *
 *      Name:           indexfn
 *
 *      Description:    Translates between symbol value and symbol
 *        index.
 *
 *      Input:          Symbol value, index table, max number of
 *        values.
 *
 *      Returns:        Index into cumulative frequency tables or
 *        escape code.
 *
 *      Side Effects:   none
 *
 *      Author:         pmulroy@visual.bt.co.uk
 *
 *********************************************************************/

int indexfn (int value, int table[], int max)
{
  int n = 0;

  while (1)
  {
    if (table[n++] == value)
      return n - 1;
    if (n > max)
      return -1;
  }

}

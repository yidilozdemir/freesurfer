/*
 *       FILE NAME:   histo.c
 *
 *       DESCRIPTION: utilities for HISTOGRAM  data structure
 *
 *       AUTHOR:      Bruce Fischl
 *       DATE:        1/8/97
 *
*/

/*-----------------------------------------------------
                    INCLUDE FILES
-------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <memory.h>

#include "error.h"
#include "proto.h"
#include "histo.h"
#include "diag.h"
#include "macros.h"

/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
int
HISTOfree(HISTOGRAM **phisto)
{
  HISTOGRAM *histo ;

  histo = *phisto ;
  *phisto = NULL ;
  if (histo)
    free(histo) ;
  return(NO_ERROR) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
int
HISTOdump(HISTOGRAM *histo, FILE *fp)
{
  int  bin_no ;

  if (!histo)
    fprintf(stderr, "NULL histogram") ;
  else
  {
    fprintf(fp, "nbins = %d\n", histo->nbins) ;
    for (bin_no = 0 ; bin_no < histo->nbins ; bin_no++)
      fprintf(fp, "bin[%d] = %d = %d\n",
              bin_no, histo->bins[bin_no], histo->counts[bin_no]) ;
  }
  return(NO_ERROR) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
HISTOGRAM *
HISTOalloc(int nbins)
{
  HISTOGRAM *histo ;

  histo = (HISTOGRAM *)calloc(1, sizeof(HISTOGRAM)) ;
  if (!histo)
    ErrorExit(ERROR_NO_MEMORY, "HISTOalloc(%d): allocation failed", nbins) ;

  histo->nbins = nbins ;
  return(histo) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
HISTOGRAM *
HISTOcrunch(HISTOGRAM *histo_src, HISTOGRAM *histo_dst)
{
  int  b, deleted ;

  if (!histo_dst)
    histo_dst = HISTOalloc(histo_src->nbins) ;

  deleted = 0 ;
  for (b = 0 ; b < histo_src->nbins ; b++)
  {
    if (!histo_src->counts[b])
      deleted++ ;
    else
    {
      histo_dst->counts[b-deleted] = histo_src->counts[b] ;
      histo_dst->bins[b-deleted] = histo_src->bins[b] ;
    }
  }

  histo_dst->nbins -= deleted ;
  return(histo_dst) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
HISTOGRAM *
HISTOcopy(HISTOGRAM *histo_src, HISTOGRAM *histo_dst)
{
  if (!histo_dst)
    histo_dst = HISTOalloc(histo_src->nbins) ;
  histo_dst->nbins = histo_src->nbins ;
  memcpy(histo_dst->counts, histo_src->counts, sizeof(histo_src->counts)) ;
  memcpy(histo_dst->bins, histo_src->bins, sizeof(histo_src->bins)) ;
  return(histo_dst) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
          Treat histo_src as a function mapping input intensities
          to output intensities, and numerically invert it.
------------------------------------------------------*/
HISTOGRAM *
HISTOinvert(HISTOGRAM *histo_src, HISTOGRAM *histo_dst, int max_dst)
{
  int       b, bdst, val, max_count ;

  histo_dst = HISTOclear(histo_src, histo_dst) ;

  if (!max_dst)
    max_dst = 255 ;

  for (max_count = b = 0 ; b < histo_src->nbins ; b++)
  {
    val = histo_src->counts[b];
    if (val > max_count)
      max_count = val ;
  }

  for (b = 0 ; b < histo_src->nbins ; b++)
  {
    val = histo_src->counts[b] ;
    bdst = nint((float)max_dst * (float)val / (float)max_count) ;
    if (bdst > max_dst)
      bdst = max_dst ;
    if (!histo_dst->bins[bdst])
    {
      histo_dst->counts[bdst] = b ;
      histo_dst->bins[bdst] = 1 ;
    }
  }

  histo_dst->nbins = max_dst ;
/* 
   fill in zeros in the inverse histogram - they correspond to
   flat regions in the forward map (i.e. multiple-valued).
*/
  for (val = b = 0 ; b < histo_dst->nbins ; b++)
  {
    if (histo_dst->counts[b] > 0)
      val = histo_dst->counts[b] ;
    else
      histo_dst->counts[b] = val ;
  }

  return(histo_dst) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
HISTOGRAM *
HISTOnormalize(HISTOGRAM *histo_src, HISTOGRAM *histo_dst, int max_out)
{
  int    max_count, b ;
  float  scale ;

  if (!max_out)
    max_out = 255 ;

  if (!histo_dst)
    histo_dst = HISTOalloc(histo_src->nbins) ;

  for (max_count = b = 0 ; b < histo_src->nbins ; b++)
    if (histo_src->counts[b] > max_count)
      max_count = histo_src->counts[b] ;

  scale = (float)max_out / (float)max_count ;
  for (b = 0 ; b < histo_src->nbins ; b++)
    histo_dst->counts[b] = nint(scale * (float)histo_src->counts[b]) ;

  return(histo_dst) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
HISTOGRAM *
HISTOclear(HISTOGRAM *histo_src, HISTOGRAM *histo_dst)
{
  if (!histo_dst)
    histo_dst = HISTOalloc(histo_src->nbins) ;

  memset(histo_dst->counts, 0, sizeof(histo_dst->counts)) ;
  memset(histo_dst->bins, 0, sizeof(histo_dst->bins)) ;
  return(histo_dst) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
          treating histo1 and histo2 as mappings from intensities to
          intensities (via the ->counts[] field), compose the two
          mappings into a composite transformation (first histo1 then
          histo2).
------------------------------------------------------*/
HISTOGRAM *
HISTOcompose(HISTOGRAM *histo1, HISTOGRAM *histo2, HISTOGRAM *histo_dst)
{
  int  b, val ;

  if (!histo_dst)
    histo_dst = HISTOalloc(histo1->nbins) ;

  for (b = 0 ; b < histo1->nbins ; b++)
  {
    val = histo1->counts[b] ;
    if (val >= histo2->nbins)
      val = histo2->nbins - 1 ;
    histo_dst->counts[b] = histo2->counts[val] ;
  }

  return(histo_dst) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
HISTOGRAM *
HISTOfillZeros(HISTOGRAM *histo_src, HISTOGRAM *histo_dst)
{
  int  b, val ;

  histo_dst = HISTOcopy(histo_src, histo_dst) ;
/* 
   fill in zeros in the inverse histogram - they correspond to
   flat regions in the forward map (i.e. multiple-valued).
*/
  for (val = b = 0 ; b < histo_dst->nbins ; b++)
  {
    if (histo_dst->counts[b] > 0)
      val = histo_dst->counts[b] ;
    else
      histo_dst->counts[b] = val ;
  }

  return(histo_dst) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
          Make a compose two mappings - one forward and one
          inverse. This is useful in the context of histogram
          specification in which the forward is the equalization
          histogram of the source image, and the inverse is
          the equalization histogram of the template image.
------------------------------------------------------*/
#define MAX_STRETCH 2

HISTOGRAM *
HISTOcomposeInvert(HISTOGRAM *histo_fwd, HISTOGRAM *histo_inv, 
                   HISTOGRAM *histo_dst)
{
  int   b, binv, val, max_fwd, max_inv, stretch ;
  float ffwd, next = 0.0f, prev ;

  if (!histo_dst)
    histo_dst = HISTOalloc(histo_fwd->nbins) ;
  else
  {
    HISTOclear(histo_dst, histo_dst) ;
    histo_dst->nbins = histo_fwd->nbins ;
  }

  for (max_fwd = b = 0 ; b < histo_fwd->nbins ; b++)
    if (histo_fwd->counts[b] > max_fwd)
      max_fwd = histo_fwd->counts[b] ;
  for (max_inv = b = 0 ; b < histo_inv->nbins ; b++)
    if (histo_inv->counts[b] > max_inv)
      max_inv = histo_inv->counts[b] ;

  if (!max_inv || !max_fwd)
    return(histo_dst) ;

  for (binv = b = 0 ; b < histo_fwd->nbins ; b++)
  {
    val = histo_fwd->counts[b] ;
    ffwd = (float)val / (float)max_fwd ;

    for (stretch = 0 ; stretch < MAX_STRETCH && binv < histo_dst->nbins ; 
         stretch++, binv++)
    {
      next = (float)histo_inv->counts[binv] / (float)max_inv ;
      if (next >= ffwd)
        break ;
    }
    if (binv > 0)  /* see whether bin to left or to right is closer */
    {
      prev = (float)histo_inv->counts[binv-1] / (float)max_inv ;
      if (fabs(next-ffwd) > abs(prev-ffwd))  /* 'prev' bin is closer */
        binv-- ;
    }
    histo_dst->counts[b] = binv++ ;
  }

  return(histo_dst) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
HISTOGRAM *
HISTOadd(HISTOGRAM *h1, HISTOGRAM *h2, HISTOGRAM *histo_dst)
{
  int  b, *pc1, *pc2, *pcdst ;

  if (!histo_dst)
    histo_dst = HISTOalloc(h1->nbins) ;
  else
    histo_dst->nbins = h1->nbins ;

  pc1 = &h1->counts[0] ; pc2 = &h2->counts[0] ; pcdst = &histo_dst->counts[0];
  for (b = 0 ; b < h1->nbins ; b++)
    *pcdst++ = *pc1++ + *pc2++ ;

  return(histo_dst) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
HISTOGRAM *
HISTOsubtract(HISTOGRAM *h1, HISTOGRAM *h2, HISTOGRAM *histo_dst)
{
  int  b, *pc1, *pc2, *pcdst ;

  if (!histo_dst)
    histo_dst = HISTOalloc(h1->nbins) ;
  else
    histo_dst->nbins = h1->nbins ;

  pc1 = &h1->counts[0] ; pc2 = &h2->counts[0] ; pcdst = &histo_dst->counts[0];
  for (b = 0 ; b < h1->nbins ; b++)
    *pcdst++ = *pc1++ - *pc2++ ;

  return(histo_dst) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
HISTOGRAM *
HISTOclearBins(HISTOGRAM *histo_src, HISTOGRAM *histo_dst, int b0, int b1)
{
  int b ;

  if (!histo_src)
    return(NULL) ;

  if (!histo_dst || histo_dst != histo_src)
    histo_dst = HISTOcopy(histo_src, histo_dst) ;

  if (b0 < 0)
    b0 = 0 ;
  if (b1 >= histo_src->nbins)
    b1 = histo_src->nbins-1 ;

  for (b = b0 ; b <= b1 ; b++)
  {
    histo_dst->counts[b] = 0 ;
    histo_dst->bins[b] = 0 ;
  }

  if (b1 == histo_dst->nbins-1)
    histo_dst->nbins = b0 ;

  return(histo_dst) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
#define MAX_LEN 9
HISTOGRAM *
HISTOsmooth(HISTOGRAM *histo_src, HISTOGRAM *histo_dst,float sigma)
{
  float     norm, two_sigma, fx, k, kernel[MAX_LEN], total ;
  int       x, half, len, b, kx, b1, nbins ;

  nbins = histo_src->nbins ;
  if (!histo_dst)
    histo_dst = HISTOalloc(nbins) ;
  else
    histo_dst->nbins = nbins ;

  /* build the kernel in k */
  len = (int)nint(8.0f * sigma)+1 ;
  if (ISEVEN(len))   /* ensure it's even */
    len++ ;
  if (MAX_LEN && (MAX_LEN < len))
    len = MAX_LEN ;
  half = len/2 ;

  norm = 0.0f ;
  two_sigma = 2.0f * sigma ;

  for (norm = 0.0f, x = 0 ; x < len ; x++)
  {
    fx = (float)(x-half) ;
    if (fabs(fx) <= two_sigma)
      k = (float)exp((double)(-fx*fx/(two_sigma*sigma))) ;
    else if (two_sigma < fabs(fx) && fabs(fx) <= 4.0f*sigma)
      k = 1.0f / (16.0f * (float)(M_E * M_E)) * 
        (float)pow(4.0f - fabs(fx)/(double)sigma, 4.0) ;
    else
      k = 0 ;

    kernel[x] = k ;
    norm += k ;
  }
  for (x = 0 ; x < len ; x++)
    kernel[x] /= norm ;

  for (b = 0 ; b < nbins ; b++)
  {
    for (total = 0.0f, x = 0 ; x < len ; x++)
    {
      kx = x - half ;
      b1 = b + kx ;
      if (b1 >= nbins || b1 < 0)
        continue ;
      total += kernel[x] * (float)histo_src->counts[b1] ;
    }
    histo_dst->counts[b] = nint(total) ;
  }
  return(histo_dst) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
int
HISTOfindLastPeak(HISTOGRAM *h, int wsize, float min_pct)
{
  int  peak, b, bw, nbins, whalf, val, max_count, count, min_count ;

  for (max_count = b = 0 ; b < h->nbins ; b++)
  {
    val = h->counts[b];
    if (val > max_count)
      max_count = val ;
  }

  if (!max_count)
    return(-1) ;

  min_count = nint(min_pct * (float)max_count) ;
  whalf = (wsize-1)/2 ;
  nbins = h->nbins ;

/*
   check to see if the value at b is bigger than anything else within
   a whalfxwhalf window on either side.
*/
  for (b = nbins-1 ; b >= 0 ; b--)
  {
    val = h->counts[b] ;
    peak = 1 ;
    for (bw = b-whalf ; bw <= b+whalf ; bw++)
    {
      if (bw < 0 || bw >= nbins)
        continue ;
      count = h->counts[bw] ;
      if (count > val || (count < min_count))
        peak = 0 ;
    }
    if (peak)
      return(b) ;
  }

  return(-1) ;
}


/*
 * Copyright (c) 1995 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

static const char rcsid[] =
    "@(#) $Header$ (LBL)";

#include "config.h"
#include <math.h>
#include "sys-time.h"
#include "media-timer.h"

MediaTimer* MediaTimer::instance_;

MediaTimer::MediaTimer()
{
	instance_ = this;
	offset_ = random();
}

MediaTimer::~MediaTimer()
{
	instance_ = 0;
}

/*
 * Default media timestamp -- convert unix system clock
 * into a 90Khz timestamp.  Grabbers override this virtual
 * method if they can provide their own time base.
 *
 * XXX
 * We save the corresponding unix time stamp to handle the
 * unix_ts() call the transmitter will make to get the correspondence
 * between the media timestamp & unix time.
 */
u_int32_t MediaTimer::media_ts()
{
	timeval tv;
	::gettimeofday(&tv, 0);
	u_int32_t u = tv.tv_usec;
	u = (u << 3) + u; /* x 9 */
	/* sec * 90Khz + (usec * 90Khz) / 1e6 */
	u = tv.tv_sec * 90000 + (u / 100);
	return (u + offset_);
}

/*
 * compute media time corresponding to the current unix time.
 * in this generic routine, this is the same as media_ts() but,
 * if a grabber has hardware or kernel timestamping, this routine
 * must compute the correspondence between the hardware timestamp
 * and the unix clock and appropriately offset the timestamp to
 * correspond to the current clock.  (This information if vital
 * for cross-media synchronization.)
 */
u_int32_t MediaTimer::ref_ts()
{
	return (media_ts());
}

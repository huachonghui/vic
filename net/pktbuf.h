/*
 * Copyright (c) 1996 The Regents of the University of California.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 * 	This product includes software developed by the MASH Research
 * 	Group at University of California, Berkeley.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#) $Header$ (LBL)
 */

#ifndef mash_pktbuf_h
#define mash_pktbuf_h

#ifdef WIN32
//#include <winsock.h>
#else
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#endif
//#include "config.h"
#include "vic_tcl.h"
//#include "timer.h"
//#include "inet.h"
//#include "module.h"

class pktbuf;

class Buffer {
public:
	virtual Buffer* copy() = 0;
	virtual void release();
};

/*
 * The base object for performing the outbound path of
 * the application level protocol.
 */
class BufferPool : public TclObject {
    public:
	BufferPool();
	void release(pktbuf*);
	/*
	 * Buffer allocation hooks.
	 */
	pktbuf* alloc(int layer = 0);
    private:
	static pktbuf* freebufs_;
	static int nbufs_;
};

/*XXX*/
#define MAXHDR 128
#define PKTBUF_PAD 256
#define PKTBUF_SIZE (MAXHDR + 1024 + PKTBUF_PAD)

class pktbuf : public Buffer {
public:
	pktbuf* next;
	int layer;
	int len;
	int ref;
	u_int8_t* dp;
	u_int8_t data[PKTBUF_SIZE];
	BufferPool* manager;
	inline void release() {
	        ref--;
		if (!ref)
		  manager->release(this);
	}
	inline void attach() {
	        ref++;
	}
	Buffer* copy();
};

#endif
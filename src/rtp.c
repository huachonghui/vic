/*
 * FILE:     rtp.c
 * AUTHOR:   Colin Perkins <c.perkins@cs.ucl.ac.uk>
 * MODIFIED: Orion Hodson <o.hodson@cs.ucl.ac.uk>
 *           Markus Germeier <mager@tzi.de>
 *           Bill Fenner <fenner@resea   rch.att.com>
 *
 * The routines in this file implement the Real-time Transport Protocol,
 * RTP, as specified in RFC1889 with current updates under discussion in
 * the IETF audio/video transport working group. Portions of the code are
 * derived from the algorithms published in that specification.
 *
 * $Revision$ 
 * $Date$
 * 
 * Copyright (c) 1998-99 University College London
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions 
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the Computer Science
 *      Department at University College London.
 * 4. Neither the name of the University nor of the Department may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "config_unix.h"
#include "config_win32.h"
#include "memory.h"
#include "debug.h"
#include "net_udp.h"
#include "crypt_random.h"
#include "drand48.h"
#include "gettimeofday.h"
#include "rtp.h"

#define SECS_BETWEEN_1900_1970 2208988800u

#define MAX_DROPOUT    3000
#define MAX_MISORDER   100
#define MIN_SEQUENTIAL 2

/*
 * Definitions for the RTP/RTCP packets on the wire...
 */

#define RTP_MAX_PACKET_LEN 1500
#define RTP_SEQ_MOD        0x10000
#define RTP_MAX_SDES_LEN   256

#define RTCP_SR   200
#define RTCP_RR   201
#define RTCP_SDES 202
#define RTCP_BYE  203
#define RTCP_APP  204

typedef struct {
#ifndef DIFF_BYTE_ORDER
	unsigned short  version:2;	/* packet type            */
	unsigned short  p:1;		/* padding flag           */
	unsigned short  count:5;	/* varies by payload type */
	unsigned short  pt:8;		/* payload type           */
#else
	unsigned short  count:5;	/* varies by payload type */
	unsigned short  p:1;		/* padding flag           */
	unsigned short  version:2;	/* packet type            */
	unsigned short  pt:8;		/* payload type           */
#endif
	u_int16         length;		/* packet length          */
} rtcp_common;

typedef struct {
	rtcp_common   common;	
	union {
		struct {
			rtcp_sr		sr;
			rtcp_rr       	rr[1];		/* variable-length list */
		} sr;
		struct {
			u_int32         ssrc;		/* source this RTCP packet is coming from */
			rtcp_rr       	rr[1];		/* variable-length list */
		} rr;
		struct rtcp_sdes_t {
			u_int32		ssrc;
			rtcp_sdes_item 	item[1];	/* list of SDES */
		} sdes;
		struct {
			u_int32         ssrc[1];	/* list of sources */
							/* can't express the trailing text... */
		} bye;
	} r;
} rtcp_t;

typedef struct _rtcp_rr_wrapper {
	struct _rtcp_rr_wrapper	*next;
	struct _rtcp_rr_wrapper	*prev;
        u_int32                  reporter_ssrc;
	rtcp_rr			*rr;
} rtcp_rr_wrapper;

/*
 * The RTP database contains source-specific information needed 
 * to make it all work. 
 */

typedef struct _source {
	struct _source	*next;
	struct _source	*prev;
	u_int32		 ssrc;
	char		*cname;
	char		*name;
	char		*email;
	char		*phone;
	char		*loc;
	char		*tool;
	char		*note;
	rtcp_sr		*sr;
	struct timeval	 last_sr;
	struct timeval	 last_active;
	int		 sender;
	int		 got_bye;	/* TRUE if we've received an RTCP bye from this source */
	u_int32		 base_seq;
	u_int16		 max_seq;
	u_int32		 bad_seq;
	u_int32		 cycles;
	int		 received;
	int		 received_prior;
	int		 expected_prior;
	int		 probation;
	u_int32		 jitter;
	u_int32		 transit;
} source;

/* The size of the hash table used to hold the source database. */
/* Should be large enough that we're unlikely to get collisions */
/* when sources are added, but not too large that we waste too  */
/* much memory. Sedgewick ("Algorithms", 2nd Ed, Addison-Wesley */
/* 1988) suggests that this should be around 1/10th the number  */
/* of entries that we expect to have in the database and should */
/* be a prime number. Everything continues to work if this is   */
/* too low, it just goes slower... for now we assume around 100 */
/* participants is a sensible limit so we set this to 11.       */   
#define RTP_DB_SIZE	11

/*
 * The "struct rtp" defines an RTP session.
 */

struct rtp {
	socket_udp	*rtp_socket;
	socket_udp	*rtcp_socket;
	char		*addr;
	u_int16		 rx_port;
	u_int16		 tx_port;
	int		 ttl;
	u_int32		 my_ssrc;
	source		*db[RTP_DB_SIZE];
        rtcp_rr_wrapper  rr[RTP_DB_SIZE][RTP_DB_SIZE]; 	/* Indexed by [hash(reporter)][hash(reportee)] */
	int		 wait_for_rtcp;			/* If TRUE, we don't accept RTP packets until we've got an RTCP packet */
	int		 invalid_rtp_count;
	int		 invalid_rtcp_count;
	int		 ssrc_count;
	int		 sender_count;
	int		 initial_rtcp;
	double		 avg_rtcp_size;
	int		 we_sent;
	double		 rtcp_bw;
	struct timeval	 last_rtcp_send_time;
	struct timeval	 next_rtcp_send_time;
	double		 rtcp_interval;
	int		 sdes_count_pri;
	int		 sdes_count_sec;
	int		 sdes_count_ter;
	u_int16		 rtp_seq;
	u_int32		 rtp_pcount;
	u_int32		 rtp_bcount;
        char            *encryption_key;
	void (*callback)(struct rtp *session, rtp_event *event);
};

static int ssrc_hash(u_int32 ssrc)
{
	/* Hash from an ssrc to a position in the source database.   */
	/* Assumes that ssrc values are uniformly distributed, which */
	/* should be true but probably isn't (Rosenberg has reported */
	/* that many implementations generate ssrc values which are  */
	/* not uniformly distributed over the space, and the H.323   */
	/* spec requires that they are non-uniformly distributed).   */
	/* This routine is written as a function rather than inline  */
	/* code to allow it to be made smart in future: probably we  */
	/* should run MD5 on the ssrc and derive a hash value from   */
	/* that, to ensure it's more uniformly distributed?          */
	return ssrc % RTP_DB_SIZE;
}

static void insert_rr(struct rtp *session, u_int32 reporter_ssrc, rtcp_rr *rr)
{
        /* Insert the reception report into the receiver report      */
        /* database. This database is a two dimensional table of     */
        /* rr_wrappers indexed by hashes of reporter_ssrc and        */
        /* reportee_src.  The rr_wrappers in the database are        */
        /* sentinels to reduce conditions in list operations.        */

        rtcp_rr_wrapper *cur, *start;

        start = &session->rr[ssrc_hash(reporter_ssrc)][ssrc_hash(rr->ssrc)];
        cur   = start->next;

        while (cur != start) {
                if (cur->reporter_ssrc == reporter_ssrc &&
                    cur->rr->ssrc      == rr->ssrc) {
                        /* Replace existing entry in the database  */
                        xfree(cur->rr);
                        cur->rr = rr;
                        return;
                }
                cur = cur->next;
        }
        
        /* No entry in the database so create one now.             */
        cur = (rtcp_rr_wrapper*)xmalloc(sizeof(rtcp_rr_wrapper));
        cur->reporter_ssrc = reporter_ssrc;
        cur->rr = rr;
        /* Fix links                                               */
        cur->next       = start->next;
        cur->next->prev = cur;
        cur->prev       = start;
        cur->prev->next = cur;

        debug_msg("Created new rr entry for 0x%08lx from source 0x%08lx\n", rr->ssrc, reporter_ssrc);
        return;
}

static void remove_rr(struct rtp *session, u_int32 ssrc)
{
	/* Remove any RRs from "s" which refer to "ssrc" as either   */
        /* reporter or reportee.                                     */
        rtcp_rr_wrapper *start, *cur, *tmp;
        int i;

        /* Remove rows, i.e. ssrc == reporter_ssrc                   */
        for(i = 0; i < RTP_DB_SIZE; i++) {
                start = &session->rr[ssrc_hash(ssrc)][i];
                cur   = start->next;
                while (cur != start) {
                        if (cur->reporter_ssrc == ssrc) {
                                tmp = cur;
                                cur = cur->prev;
                                tmp->prev->next = tmp->next;
                                tmp->next->prev = tmp->prev;
                                xfree(tmp->rr);
                                xfree(tmp);
                        }
                        cur = cur->next;
                }
        }

        /* Remove columns, i.e.  ssrc == reporter_ssrc */
        for(i = 0; i < RTP_DB_SIZE; i++) {
                start = &session->rr[i][ssrc_hash(ssrc)];
                cur   = start->next;
                while (cur != start) {
                        if (cur->rr->ssrc == ssrc) {
                                tmp = cur;
                                cur = cur->prev;
                                tmp->prev->next = tmp->next;
                                tmp->next->prev = tmp->prev;
                                xfree(tmp->rr);
                                xfree(tmp);
                        }
                        cur = cur->next;
                }
        }
}

static const rtcp_rr* get_rr(struct rtp *session, u_int32 reporter_ssrc, u_int32 reportee_ssrc)
{
        rtcp_rr_wrapper *cur, *start;

        start = &session->rr[ssrc_hash(reporter_ssrc)][ssrc_hash(reportee_ssrc)];
        cur   = start->next;
        while (cur != start) {
                if (cur->reporter_ssrc == reporter_ssrc &&
                    cur->rr->ssrc      == reportee_ssrc) {
                        return cur->rr;
                }
                cur = cur->next;
        }
        return NULL;
}

static void check_database(struct rtp *session)
{
#ifdef DEBUG
	/* This routine performs a sanity check on the database. */
	/* If the DEBUG symbol is not defined, it does nothing.  */
	/* This should not call any of the other routines which  */
	/* manipulate the database, to avoid common failures.    */
	source 	 	*s;
	int	 	 source_count;
	int		 chain;

	/* Check that we have a database entry for our ssrc... */
	/* We only do this check if ssrc_count > 0 since it is */
	/* performed during initialisation whilst creating the */
	/* source entry for my_ssrc.                           */
	if (session->ssrc_count > 0) {
		for (s = session->db[ssrc_hash(session->my_ssrc)]; s != NULL; s = s->next) {
			if (s->ssrc == session->my_ssrc) {
				break;
			}
		}
		assert(s != NULL);
	}

	source_count = 0;
	for (chain = 0; chain < RTP_DB_SIZE; chain++) {
		/* Check that the linked lists making up the chains in */
		/* the hash table are correctly linked together...     */
		for (s = session->db[chain]; s != NULL; s = s->next) {
			source_count++;
			if (s->prev == NULL) {
				assert(s == session->db[chain]);
			} else {
				assert(s->prev->next == s);
			}
			if (s->next != NULL) {
				assert(s->next->prev == s);
			}
			/* Check that the SR is for this source... */
			if (s->sr != NULL) {
				assert(s->sr->ssrc == s->ssrc);
			}
		}
	}
	/* Check that the number of entries in the hash table  */
	/* matches session->ssrc_count                         */
	assert(source_count == session->ssrc_count);
#else
        UNUSED(session);
#endif
}

static source *get_source(struct rtp *session, u_int32 ssrc)
{
	source *s;

	check_database(session);
	for (s = session->db[ssrc_hash(ssrc)]; s != NULL; s = s->next) {
		if (s->ssrc == ssrc) {
			return s;
		}
	}
	return NULL;
}

static source *create_source(struct rtp *session, u_int32 ssrc)
{
	/* Create a new source entry, and add it to the database.    */
	/* The database is a hash table, using the separate chaining */
	/* algorithm.                                                */
	rtp_event	 event;
	struct timeval	 event_ts;
	source		*s = get_source(session, ssrc);
	int		 h;

	if (s != NULL) {
		/* Source is already in the database... Mark it as */
		/* active and exit (this is the common case...)    */
		gettimeofday(&(s->last_active), NULL);
		return s;
	}
	check_database(session);
	/* This is a new source, we have to create it... */
	h = ssrc_hash(ssrc);
	s = (source *) xmalloc(sizeof(source));
	s->next           = session->db[h];
	s->prev           = NULL;
	s->ssrc           = ssrc;
	s->cname          = NULL;
	s->name           = NULL;
	s->email          = NULL;
	s->phone          = NULL;
	s->loc            = NULL;
	s->tool           = NULL;
	s->note           = NULL;
	s->sr             = NULL;
	s->got_bye        = FALSE;
	s->sender         = FALSE;
	s->base_seq       = 0;
	s->max_seq        = 0;
	s->bad_seq        = 0;
	s->cycles         = 0;
	s->received       = 0;
	s->received_prior = 0;
	s->expected_prior = 0;
	s->probation      = -1;
	s->jitter         = 0;
	s->transit        = 0;
	gettimeofday(&(s->last_active), NULL);
	/* Now, add it to the database... */
	if (session->db[h] != NULL) {
		session->db[h]->prev = s;
	}
	session->db[ssrc_hash(ssrc)] = s;
	session->ssrc_count++;
	check_database(session);

	debug_msg("Created database entry for ssrc 0x%08lx\n", ssrc);
        if (ssrc != session->my_ssrc) {
                /* Do not send during rtp_init since application cannot map the address */
                /* of the rtp session to anything since rtp_init has not returned yet.  */
                gettimeofday(&event_ts, NULL);
                event.ssrc = ssrc;
                event.type = SOURCE_CREATED;
                event.data = NULL;
                event.ts   = &event_ts;
                session->callback(session, &event);
        }

	return s;
}

static void delete_source(struct rtp *session, u_int32 ssrc)
{
	/* Remove a source from the RTP database... */
	source		*s = get_source(session, ssrc);
	int		 h = ssrc_hash(ssrc);
	rtp_event	 event;
	struct timeval	 event_ts;

	assert(s != NULL);	/* Deleting a source which doesn't exist is an error... */

	check_database(session);
	if (session->db[h] == s) {
		/* It's the first entry in this chain... */
		session->db[h] = s->next;
		if (s->next != NULL) {
			s->next->prev = NULL;
		}
	} else {
		assert(s->prev != NULL);	/* Else it would be the first in the chain... */
		s->prev->next = s->next;
		if (s->next != NULL) {
			s->next->prev = s->prev;
		}
	}
	/* Free the memory allocated to a source... */
	if (s->cname != NULL) xfree(s->cname);
	if (s->name  != NULL) xfree(s->name);
	if (s->email != NULL) xfree(s->email);
	if (s->phone != NULL) xfree(s->phone);
	if (s->loc   != NULL) xfree(s->loc);
	if (s->tool  != NULL) xfree(s->tool);
	if (s->note  != NULL) xfree(s->note);
	if (s->sr    != NULL) xfree(s->sr);

        remove_rr(session, ssrc);
	/* Done... reduce our source count, and signal to the application that this source is dead */
	session->ssrc_count--;
	gettimeofday(&event_ts, NULL);
	event.ssrc = ssrc;
	event.type = SOURCE_DELETED;
	event.data = NULL;
	event.ts   = &event_ts;
	session->callback(session, &event);
        xfree(s);
	check_database(session);
}

static void init_seq(source *s, u_int16 seq)
{
	/* Taken from draft-ietf-avt-rtp-new-01.txt */
	s->base_seq = seq;
	s->max_seq = seq;
	s->bad_seq = RTP_SEQ_MOD + 1;
	s->cycles = 0;
	s->received = 0;
	s->received_prior = 0;
	s->expected_prior = 0;
}

static int update_seq(source *s, u_int16 seq)
{
	/* Taken from draft-ietf-avt-rtp-new-01.txt */
	u_int16 udelta = seq - s->max_seq;

	/*
	 * Source is not valid until MIN_SEQUENTIAL packets with
	 * sequential sequence numbers have been received.
	 */
	if (s->probation) {
		  /* packet is in sequence */
		  if (seq == s->max_seq + 1) {
				s->probation--;
				s->max_seq = seq;
				if (s->probation == 0) {
					 init_seq(s, seq);
					 s->received++;
					 return 1;
				}
		  } else {
				s->probation = MIN_SEQUENTIAL - 1;
				s->max_seq = seq;
		  }
		  return 0;
	} else if (udelta < MAX_DROPOUT) {
		  /* in order, with permissible gap */
		  if (seq < s->max_seq) {
				/*
				 * Sequence number wrapped - count another 64K cycle.
				 */
				s->cycles += RTP_SEQ_MOD;
		  }
		  s->max_seq = seq;
	} else if (udelta <= RTP_SEQ_MOD - MAX_MISORDER) {
		  /* the sequence number made a very large jump */
		  if (seq == s->bad_seq) {
				/*
				 * Two sequential packets -- assume that the other side
				 * restarted without telling us so just re-sync
				 * (i.e., pretend this was the first packet).
				 */
				init_seq(s, seq);
		  } else {
				s->bad_seq = (seq + 1) & (RTP_SEQ_MOD-1);
				return 0;
		  }
	} else {
		  /* duplicate or reordered packet */
	}
	s->received++;
	return 1;
}

static double rtcp_interval(struct rtp *session, int reconsider)
{
	/* Minimum average time between RTCP packets from this site (in   */
	/* seconds).  This time prevents the reports from `clumping' when */
	/* sessions are small and the law of large numbers isn't helping  */
	/* to smooth out the traffic.  It also keeps the report interval  */
	/* from becoming ridiculously small during transient outages like */
	/* a network partition.                                           */
	double const RTCP_MIN_TIME = 5.0;
	/* Fraction of the RTCP bandwidth to be shared among active       */
	/* senders.  (This fraction was chosen so that in a typical       */
	/* session with one or two active senders, the computed report    */
	/* time would be roughly equal to the minimum report time so that */
	/* we don't unnecessarily slow down receiver reports.) The        */
	/* receiver fraction must be 1 - the sender fraction.             */
	double const RTCP_SENDER_BW_FRACTION = 0.25;
	double const RTCP_RCVR_BW_FRACTION = (1-RTCP_SENDER_BW_FRACTION);
	double t;				              /* interval */
	double rtcp_min_time = RTCP_MIN_TIME;
	int n;			        /* no. of members for computation */
	double rtcp_bw = session->rtcp_bw;

	if (reconsider) {
		rtcp_bw *= 1.21828;
	}

	/* Very first call at application start-up uses half the min      */
	/* delay for quicker notification while still allowing some time  */
	/* before reporting for randomization and to learn about other    */
	/* sources so the report interval will converge to the correct    */
	/* interval more quickly.                                         */
	if (session->initial_rtcp) {
		rtcp_min_time /= 2;
	}

	/* If there were active senders, give them at least a minimum     */
	/* share of the RTCP bandwidth.  Otherwise all participants share */
	/* the RTCP bandwidth equally.                                    */
	n = session->ssrc_count;
	if (session->sender_count > 0 && session->sender_count < session->ssrc_count * RTCP_SENDER_BW_FRACTION) {
		if (session->we_sent) {
			rtcp_bw *= RTCP_SENDER_BW_FRACTION;
			n = session->sender_count;
		} else {
			rtcp_bw *= RTCP_RCVR_BW_FRACTION;
			n -= session->sender_count;
		}
	}

	/* The effective number of sites times the average packet size is */
	/* the total number of octets sent when each site sends a report. */
	/* Dividing this by the effective bandwidth gives the time        */
	/* interval over which those packets must be sent in order to     */
	/* meet the bandwidth target, with a minimum enforced.  In that   */
	/* time interval we send one report so this time is also our      */
	/* average time between reports.                                  */
	t = session->avg_rtcp_size * n / rtcp_bw;
	if (t < rtcp_min_time) {
		t = rtcp_min_time;
	}
	session->rtcp_interval = t;

	/* To avoid traffic bursts from unintended synchronization with   */
	/* other sites, we then pick our actual next report interval as a */
	/* random number uniformly distributed between 0.5*t and 1.5*t.   */
	return t * (drand48() + 0.5);
}

static double tv_diff(struct timeval curr_time, struct timeval prev_time)
{
    /* Return curr_time - prev_time */
    double sec;
    double usec;

    sec = curr_time.tv_sec - prev_time.tv_sec ;
    usec = curr_time.tv_usec - prev_time.tv_usec ;

    assert(sec >= 0);
    if (usec < 0 ) {
	assert(sec > 0);
	sec--;
	usec += 1000000;
    }

    return (sec+(usec/1000000));
}

static void tv_add(struct timeval *ts, double offset)
{
	/* Add offset seconds to ts */
	double offset_sec, offset_usec;

	offset_usec = modf(offset, &offset_sec) * 1000000;
	ts->tv_sec  += (long) offset_sec;
	ts->tv_usec += (long) offset_usec;
	if (ts->tv_usec > 1000000) {
		ts->tv_sec++;
		ts->tv_usec -= 1000000;
	}
}

static int tv_gt(struct timeval a, struct timeval b)
{
	/* Returns (a>b) */
	if (a.tv_sec > b.tv_sec) {
		return TRUE;
	}
	if (a.tv_sec < b.tv_sec) {
		return FALSE;
	}
	assert(a.tv_sec == b.tv_sec);
	return a.tv_usec > b.tv_usec;
}

static char *get_cname(socket_udp *s)
{
        /* Set the CNAME. This is "user@hostname" or just "hostname" if the username cannot be found. */
        char                    *uname;
        char                    *hname;
        char                    *cname;
#ifndef WIN32
        struct passwd           *pwent;
#endif

        cname = (char *) xmalloc(MAXHOSTNAMELEN + 10);
        cname[0] = '\0';

        /* First, fill in the username... */
#ifdef WIN32
        uname = getenv("USER");
#else
        pwent = getpwuid(getuid());
        uname = pwent->pw_name;
#endif
        if (uname != NULL) {
                sprintf(cname, "%s@", uname);
        }

        /* Now the hostname. Must be dotted-quad IP address. */
        hname = udp_host_addr(s);
        strcpy(cname + strlen(cname), hname);
        xfree(hname);
        return cname;
}

struct rtp *rtp_init(char *addr, u_int16 rx_port, u_int16 tx_port, int ttl, double rtcp_bw, void (*callback)(struct rtp *session, rtp_event *e))
{
	struct rtp 	*session;
	int         	 i, j;
	char		*cname;

	assert(ttl >= 0 && ttl < 128);
	assert(rx_port % 2 == 0);
	assert(tx_port % 2 == 0);

	srand48(time(NULL));

	session = (struct rtp *) xmalloc(sizeof(struct rtp));
	session->addr               = xstrdup(addr);
	session->rx_port            = rx_port;
	session->tx_port            = tx_port;
	session->ttl                = ttl;
	session->rtp_socket         = udp_init(addr, rx_port, tx_port, ttl);
	session->rtcp_socket        = udp_init(addr, (u_int16) (rx_port+1), (u_int16) (tx_port+1), ttl);

	if (session->rtp_socket == NULL || session->rtcp_socket == NULL) {
		xfree(session);
		return NULL;
	}

	session->my_ssrc            = (u_int32) lrand48();
	session->wait_for_rtcp      = TRUE;
	session->callback           = callback;
	session->invalid_rtp_count  = 0;
	session->invalid_rtcp_count = 0;
	session->ssrc_count         = 0;
	session->sender_count       = 0;
	session->initial_rtcp       = TRUE;
	session->avg_rtcp_size      = 70.0;	/* Guess for a sensible starting point... */
	session->we_sent            = FALSE;
	session->rtcp_bw            = rtcp_bw;
	session->sdes_count_pri     = 0;
	session->sdes_count_sec     = 0;
	session->sdes_count_ter     = 0;
	session->rtp_seq            = (u_int16) lrand48();
	session->rtp_pcount         = 0;
	session->rtp_bcount         = 0;
	gettimeofday(&(session->last_rtcp_send_time), NULL);
	gettimeofday(&(session->next_rtcp_send_time), NULL);
        session->encryption_key      = NULL;

	/* Calculate when we're supposed to send our first RTCP packet... */
	tv_add(&(session->next_rtcp_send_time), rtcp_interval(session, FALSE));

	/* Initialise the source database... */
	for (i = 0; i < RTP_DB_SIZE; i++) {
		session->db[i] = NULL;
	}

        /* Initialize sentinels in rr table */
        for (i = 0; i < RTP_DB_SIZE; i++) {
                for (j = 0; j < RTP_DB_SIZE; j++) {
                        session->rr[i][j].next = &session->rr[i][j];
                        session->rr[i][j].prev = &session->rr[i][j];
                }
        }

	/* Create a database entry for ourselves... */
	create_source(session, session->my_ssrc);
	cname = get_cname(session->rtp_socket);
	rtp_set_sdes(session, session->my_ssrc, RTCP_SDES_CNAME, cname, strlen(cname));
	xfree(cname);	/* cname is copied by rtp_set_sdes()... */

	return session;
}

void rtp_weak_validation(struct rtp *session)
{
	/* Instructs the RTP code to accept RTP packets before we've received   */
	/* a corresponding RTCP packet. This is not enabled by default because: */
	/*  - RTP header validity checks are significantly weaker than those    */
	/*    for RTCP, so we may accept invalid packets and create sources     */
	/*    based on them (this is a real problem if encryption is used,      */
	/*    since a user sending to us with the wrong key generates enough    */
	/*    packets that we are likely to end up with many bogus sources).    */
	/*  - I work for the protocol police, and want to encourage people to   */
	/*    implement RTCP. :-)                                         [csp] */
	session->wait_for_rtcp = FALSE;
}

int rtp_set_my_ssrc(struct rtp *session, u_int32 ssrc)
{
        source *s;
        u_int32 h;
        /* Sets my_ssrc when called immediately after rtp_init. */
        /* Returns TRUE on success, FALSE otherwise.  Needed to */
        /* co-ordinate SSRC's between LAYERED SESSIONS, SHOULD  */
        /* NOT BE USED OTHERWISE.                               */
        if (session->ssrc_count != 1 && session->sender_count != 0) {
                return FALSE;
        }
        /* Remove existing source */
        h = ssrc_hash(session->my_ssrc);
        s = session->db[h];
        session->db[h] = NULL;
        /* Fill in new ssrc       */
        session->my_ssrc = ssrc;
        s->ssrc          = ssrc;
        h                = ssrc_hash(ssrc);
        /* Put source back        */
        session->db[h]   = s;
        return TRUE;
}

u_int32 rtp_my_ssrc(struct rtp *session)
{
	assert(session != NULL);
	return session->my_ssrc;
}

static int validate_rtp(rtp_packet *packet, int len)
{
	/* This function checks the header info to make sure that the packet */
	/* is valid. We return TRUE if the packet is valid, FALSE otherwise. */
	/* See Appendix A.1 of the RTP specification.                        */

	/* We only accept RTPv2 packets... */
	if (packet->v != 2) {
		debug_msg("rtp_header_validation: v != 2\n");
		return FALSE;
	}
	/* Check for valid payload types..... 72-76 are RTCP payload type numbers, with */
	/* the high bit missing so we report that someone is running on the wrong port. */
	if (packet->pt >= 72 && packet->pt <= 76) {
		debug_msg("rtp_header_validation: payload-type invalid");
		if (packet->m) {
			debug_msg(" (RTCP packet on RTP port?)");
		}
		debug_msg("\n");
		return FALSE;
	}
	/* Check that the length of the packet is sensible... */
	if (len < (12 + (4 * packet->cc))) {
		debug_msg("rtp_header_validation: packet length is smaller than the header\n");
		return FALSE;
	}
	/* Check that the length of any header extension is sensible... */
        if (packet->x) {
		if (packet->extn_len > (len - 12 - (4 * packet->cc) - packet->p?packet->data[packet->data_len-1]:0)) {
			debug_msg("rtp_header_validation: extension header is too big!\n");
			return FALSE;
		}
        }
	/* Check that the amount of padding specified is sensible. */
	/* Note: have to include the size of any extension header! */
	if (packet->p) {
		int	payload_len = len - 12 - (packet->cc * 4) - (packet->extn_len * 4) - packet->extn_len>0?4:0;
                if (packet->data[packet->data_len-1] > payload_len) {
                        debug_msg("rtp_header_validation: padding greater than payload length\n");
                        return FALSE;
                }
                if (packet->data[packet->data_len-1] < 1) {
			debug_msg("rtp_header_validation: padding zero\n");
			return FALSE;
		}
        }
	return TRUE;
}

static void rtp_recv_data(struct rtp *session, u_int32 curr_time)
{
	/* This routine processes incoming RTP packets */
	rtp_packet	*packet = (rtp_packet *) xmalloc(RTP_MAX_PACKET_LEN);
	u_int8		*buffer = ((u_int8 *) packet) + RTP_PACKET_HEADER_SIZE;
	int		 buflen;
	rtp_event	 event;
	struct timeval	 event_ts;
	int		 i, d, transit;
	source		*s;

	buflen = udp_recv(session->rtp_socket, buffer, RTP_MAX_PACKET_LEN - RTP_PACKET_HEADER_SIZE);
	if (buflen > 0) {
		/* Convert header fields to host byte order... */
		packet->seq      = ntohs(packet->seq);
		packet->ts       = ntohl(packet->ts);
		packet->ssrc     = ntohl(packet->ssrc);
		/* Setup internal pointers, etc... */
		if (packet->cc) {
			packet->csrc = (u_int32 *)(buffer + 12);
		} else {
			packet->csrc = NULL;
		}
		if (packet->x) {
			packet->extn     = buffer + 12 + (packet->cc * 4);
			packet->extn_len = (packet->extn[2] << 16) | packet->extn[3];
		} else {
			packet->extn     = NULL;
			packet->extn_len = 0;
		}
		packet->data     = buffer + 12 + (packet->cc * 4) + packet->extn_len;
		packet->data_len = buflen - packet->extn_len - (packet->cc * 4) - 12;
		if (validate_rtp(packet, buflen)) {
			if (session->wait_for_rtcp) {
				s = get_source(session, packet->ssrc);
			} else {
				s = create_source(session, packet->ssrc);
			}
			if (s != NULL) {
				if (s->probation == -1) {
					s->probation = MIN_SEQUENTIAL;
					s->max_seq   = packet->seq - 1;
				}
				if (update_seq(s, packet->seq)) {
					/* Process the packet we've just received... */
					if (packet->cc > 0) {
						for (i = 0; i < packet->cc; i++) {
							create_source(session, ntohl(packet->csrc[i]));
						}
					}
					/* Update the source database... */
					if (s->sender == FALSE) {
						s->sender = TRUE;
						session->sender_count++;
					}
					transit    = curr_time - packet->ts;
					d      	   = transit - s->transit;
					s->transit = transit;
					if (d < 0) {
						d = -d;
					}
					s->jitter += d - ((s->jitter + 8) / 16);

					/* Callback to the application to process the packet... */
					gettimeofday(&event_ts, NULL);
					event.ssrc = packet->ssrc;
					event.type = RX_RTP;
					event.data = (void *) packet;	/* The callback function MUST free this! */
					event.ts   = &event_ts;
					session->callback(session, &event);
					return;	/* we don't free "packet", that's done by the callback function... */
				} else {
					/* This source is still on probation... */
					debug_msg("RTP packet from probationary source ignored...\n");
				}
			} else {
				debug_msg("RTP packet from unknown source ignored\n");
			}
		} else {
			session->invalid_rtp_count++;
			debug_msg("Invalid RTP packet discarded\n");
		}
	}
	xfree(packet);
}

static int validate_rtcp(u_int8 *packet, int len)
{
	/* Validity check for a compound RTCP packet. This function returns */
	/* TRUE if the packet is okay, FALSE if the validity check fails.   */
        /*                                                                  */
	/* The following checks can be applied to RTCP packets [RFC1889]:   */
        /* o RTP version field must equal 2.                                */
        /* o The payload type field of the first RTCP packet in a compound  */
        /*   packet must be equal to SR or RR.                              */
        /* o The padding bit (P) should be zero for the first packet of a   */
        /*   compound RTCP packet because only the last should possibly     */
        /*   need padding.                                                  */
        /* o The length fields of the individual RTCP packets must total to */
        /*   the overall length of the compound RTCP packet as received.    */

	rtcp_t	*pkt  = (rtcp_t *) packet;
	rtcp_t	*end  = (rtcp_t *) (((char *) pkt) + len);
	rtcp_t	*r    = pkt;
	int	 l    = 0;
	int	 last = 0;

	/* All RTCP packets must be compound packets (RFC1889, section 6.1) */
	if (((ntohs(pkt->common.length) + 1) * 4) == len) {
		debug_msg("Bogus RTCP packet: not a compound packet\n");
		return FALSE;
	}

	/* Check the RTCP version, payload type and padding of the first in  */
	/* the compund RTCP packet...                                        */
	if (pkt->common.version != 2) {
		debug_msg("Bogus RTCP packet: version number != 2 in the first sub-packet\n");
		return FALSE;
	}
	if (pkt->common.p != 0) {
		debug_msg("Bogus RTCP packet: padding bit is set on first packet in compound\n");
		return FALSE;
	}
	if ((pkt->common.pt != RTCP_SR) && (pkt->common.pt != RTCP_RR)) {
		debug_msg("Bogus RTCP packet: compund packet does not start with SR or RR\n");
		return FALSE;
	}

	/* Check all following parts of the compund RTCP packet. The RTP version */
	/* number must be 2, and the padding bit must be zero on all apart from  */
	/* the last packet.                                                      */
	do {
		if (r->common.version != 2) {
			debug_msg("Bogus RTCP packet: version number != 2\n");
			return FALSE;
		}
		if (last == 1) {
			debug_msg("Bogus RTCP packet: padding bit set before last in compound\n");
			return FALSE;
		}
		if (r->common.p == 1) last = 1;
		l += (ntohs(r->common.length) + 1) * 4;
		r  = (rtcp_t *) (((u_int32 *) r) + ntohs(r->common.length) + 1);
	} while (r < end);

	/* Check that the length of the packets matches the length of the UDP */
	/* packet in which they were received...                              */
	if ((r != end) || (l != len))  {
		debug_msg("Bogus RTCP packet: RTCP packet length does not match UDP packet\n");
		return FALSE;
	}

	return TRUE;
}

static void process_report_blocks(struct rtp *session, rtcp_t *packet, u_int32 ssrc, rtcp_rr *rrp, struct timeval *event_ts)
{
	int i;
	rtp_event	 event;
	rtcp_rr		*rr;

	/* ...process RRs... */
	if (packet->common.count == 0) {
		event.ssrc = ssrc;
		event.type = RX_RR_EMPTY;
		event.data = NULL;
		event.ts   = event_ts;
		session->callback(session, &event);
	} else {
		for (i = 0; i < packet->common.count; i++, rrp++) {
			rr = (rtcp_rr *) xmalloc(sizeof(rtcp_rr));
			rr->ssrc          = ntohl(rrp->ssrc);
			rr->fract_lost    = rrp->fract_lost;	/* Endian conversion handled in the */
			rr->total_lost    = rrp->total_lost;	/* definition of the rtcp_rr type.  */
			rr->last_seq      = ntohl(rrp->last_seq);
			rr->jitter        = ntohl(rrp->jitter);
			rr->lsr           = ntohl(rrp->lsr);
			rr->dlsr          = ntohl(rrp->dlsr);

			/* Create a database entry for this SSRC, if one doesn't already exist... */
			create_source(session, rr->ssrc);

			/* Store the RR for later use... */
			insert_rr(session, ssrc, rr);

			/* Call the event handler... */
			event.ssrc = ssrc;
			event.type = RX_RR;
			event.data = (void *) rr;
			event.ts   = event_ts;
			session->callback(session, &event);
		}
	}
}

static void process_rtcp_sr(struct rtp *session, rtcp_t *packet, struct timeval *event_ts)
{
	u_int32		 ssrc;
	rtp_event	 event;
	rtcp_sr		*sr;
	source		*s;

	ssrc = ntohl(packet->r.sr.sr.ssrc);
	s = create_source(session, ssrc);
	if (s == NULL) {
		debug_msg("Source 0x%08x invalid, skipping...\n", ssrc);
		return;
	}

	/* Process the SR... */
	sr = (rtcp_sr *) xmalloc(sizeof(rtcp_sr));
	sr->ssrc          = ssrc;
	sr->ntp_sec       = ntohl(packet->r.sr.sr.ntp_sec);
	sr->ntp_frac      = ntohl(packet->r.sr.sr.ntp_frac);
	sr->rtp_ts        = ntohl(packet->r.sr.sr.rtp_ts);
	sr->sender_pcount = ntohl(packet->r.sr.sr.sender_pcount);
	sr->sender_bcount = ntohl(packet->r.sr.sr.sender_bcount);

	/* Store the SR for later retrieval... */
	if (s->sr != NULL) {
		xfree(s->sr);
	}
	s->sr = sr;
	s->last_sr = *event_ts;

	/* Call the event handler... */
	event.ssrc = ssrc;
	event.type = RX_SR;
	event.data = (void *) sr;
	event.ts   = event_ts;
	session->callback(session, &event);

	process_report_blocks(session, packet, ssrc, packet->r.sr.rr, event_ts);
}

static void process_rtcp_rr(struct rtp *session, rtcp_t *packet, struct timeval *event_ts)
{
	u_int32		 ssrc;
	source		*s;

	ssrc = ntohl(packet->r.rr.ssrc);
	s = create_source(session, ssrc);
	if (s == NULL) {
		debug_msg("Source 0x%08x invalid, skipping...\n", ssrc);
		return;
	}

	process_report_blocks(session, packet, ssrc, packet->r.rr.rr, event_ts);
}

static void process_rtcp_sdes(struct rtp *session, rtcp_t *packet, struct timeval *event_ts)
{
	int 			count = packet->common.count;
	struct rtcp_sdes_t 	*sd   = &packet->r.sdes;
	rtcp_sdes_item 		*rsp; 
	rtcp_sdes_item		*rspn;
	rtcp_sdes_item 		*end  = (rtcp_sdes_item *) ((u_int32 *)packet + packet->common.length + 1);
	source 			*s;
	rtp_event		 event;

	while (--count >= 0) {
		rsp = &sd->item[0];
		if (rsp >= end) {
			break;
		}
		sd->ssrc = ntohl(sd->ssrc);
		s = create_source(session, sd->ssrc);
		if (s == NULL) {
			debug_msg("Can't get valid source entry for 0x%08x, skipping...\n", sd->ssrc);
		} else {
			for (; rsp->type; rsp = rspn ) {
				rspn = (rtcp_sdes_item *)((char*)rsp+rsp->length+2);
				if (rspn >= end) {
					rsp = rspn;
					break;
				}
				if (rtp_set_sdes(session, sd->ssrc, rsp->type, rsp->data, rsp->length)) {
					event.ssrc = sd->ssrc;
					event.type = RX_SDES;
					event.data = (void *) rsp;
					event.ts   = event_ts;
					session->callback(session, &event);
				} else {
					debug_msg("Invalid sdes item for source 0x%08x, skipping...\n", sd->ssrc);
				}
			}
		}
		sd = (struct rtcp_sdes_t *) ((u_int32 *)sd + (((char *)rsp - (char *)sd) >> 2)+1);
	}
	if (count >= 0) {
		debug_msg("Invalid RTCP SDES packet, some items ignored.\n");
	}
}

static void process_rtcp_bye(struct rtp *session, rtcp_t *packet, struct timeval *event_ts)
{
	int		 i;
	u_int32		 ssrc;
	rtp_event	 event;
	source		*s;

	for (i = 0; i < packet->common.count; i++) {
		ssrc = ntohl(packet->r.bye.ssrc[i]);
		/* This is kind-of strange, since we create a source we are about to delete. */
		/* This is done to ensure that the source mentioned in the event which is    */
		/* passed to the user of the RTP library is valid, and simplify client code. */
		create_source(session, ssrc);
		/* Call the event handler... */
		event.ssrc = ssrc;
		event.type = RX_BYE;
		event.data = NULL;
		event.ts   = event_ts;
		session->callback(session, &event);
		/* Mark the source as ready for deletion. Sources are not deleted immediately */
		/* since some packets may be delayed and arrive after the BYE...              */
		s = get_source(session, ssrc);
		s->got_bye = TRUE;
	}
}

static void rtp_process_ctrl(struct rtp *session, u_int8 *buffer, int buflen)
{
	/* This routine processes incoming RTCP packets */
	rtp_event	 event;
	struct timeval	 event_ts;
	rtcp_t		*packet;

	gettimeofday(&event_ts, NULL);
	if (buflen > 0) {
		if (validate_rtcp(buffer, buflen)) {
			/* Signal that we're about to process a compound RTCP packet */
			event.ssrc = 0xdeadbeef;
			event.type = RX_RTCP_START;
			event.data = &buflen;
			event.ts   = &event_ts;
			session->callback(session, &event);

			packet  = (rtcp_t *) buffer;
			while (packet < (rtcp_t *) (buffer + buflen)) {
				switch (packet->common.pt) {
					case RTCP_SR:
						process_rtcp_sr(session, packet, &event_ts);
						break;
					case RTCP_RR:
						process_rtcp_rr(session, packet, &event_ts);
						break;
					case RTCP_SDES:
						process_rtcp_sdes(session, packet, &event_ts);
						break;
					case RTCP_BYE:
						process_rtcp_bye(session, packet, &event_ts);
						break;
					default: 
						debug_msg("RTCP packet with unknown type (%d) ignored.\n", packet->common.pt);
						break;
				}
				packet = (rtcp_t *) ((char *) packet + (4 * (ntohs(packet->common.length) + 1)));
			}
			/* The constants here are 1/16 and 15/16 (section 6.3.3 of draft-ietf-avt-rtp-new-02.txt) */
			session->avg_rtcp_size = (0.0625 * buflen) + (0.9375 * session->avg_rtcp_size);
			/* Signal that we've finished processing this packet */
			event.ssrc = 0xdeadbeef;
			event.type = RX_RTCP_FINISH;
			event.data = NULL;
			event.ts   = &event_ts;
			session->callback(session, &event);
		} else {
			debug_msg("Invalid RTCP packet discarded\n");
			session->invalid_rtcp_count++;
		}
	}
}

int rtp_recv(struct rtp *session, struct timeval *timeout, u_int32 curr_time)
{
	udp_fd_zero();
	udp_fd_set(session->rtp_socket);
	udp_fd_set(session->rtcp_socket);
	if (udp_select(timeout) > 0) {
		if (udp_fd_isset(session->rtp_socket)) {
			rtp_recv_data(session, curr_time);
		}
		if (udp_fd_isset(session->rtcp_socket)) {
                        u_int8		 buffer[RTP_MAX_PACKET_LEN];
                        int		 buflen;
                        buflen = udp_recv(session->rtcp_socket, buffer, RTP_MAX_PACKET_LEN);
			rtp_process_ctrl(session, buffer, buflen);
		}
                return TRUE;
	}
        return FALSE;
}

int rtp_add_csrc(struct rtp *session, u_int32 csrc)
{
	UNUSED(session);
	UNUSED(csrc);
	return FALSE;
}

int rtp_set_sdes(struct rtp *session, u_int32 ssrc, u_int8 type, char *value, int length)
{
	source	*s = get_source(session, ssrc);
	char	*v;

	if (s == NULL) {
		debug_msg("Invalid source 0x%08x\n", ssrc);
		return FALSE;
	}

	v = (char *) xmalloc(length + 1);
	memset(v, '\0', length + 1);
	memcpy(v, value, length);

	switch (type) {
		case RTCP_SDES_CNAME: 
			if (s->cname) xfree(s->cname);
			s->cname = v; 
			break;
		case RTCP_SDES_NAME:
			if (s->name) xfree(s->name);
			s->name = v; 
			break;
		case RTCP_SDES_EMAIL:
			if (s->email) xfree(s->email);
			s->email = v; 
			break;
		case RTCP_SDES_PHONE:
			if (s->phone) xfree(s->phone);
			s->phone = v; 
			break;
		case RTCP_SDES_LOC:
			if (s->loc) xfree(s->loc);
			s->loc = v; 
			break;
		case RTCP_SDES_TOOL:
			if (s->tool) xfree(s->tool);
			s->tool = v; 
			break;
		case RTCP_SDES_NOTE:
			if (s->note) xfree(s->note);
			s->note = v; 
			break;
		default :
			debug_msg("Unknown SDES item (type=%d, value=%s)\n", type, v);
                        xfree(v);
			return FALSE;
	}
	return TRUE;
}

const char *rtp_get_sdes(struct rtp *session, u_int32 ssrc, u_int8 type)
{
	source	*s = get_source(session, ssrc);

	if (s == NULL) {
		debug_msg("Invalid source 0x%08x\n", ssrc);
		return NULL;
	}

	switch (type) {
		case RTCP_SDES_CNAME: 
			return s->cname;
		case RTCP_SDES_NAME:
			return s->name;
		case RTCP_SDES_EMAIL:
			return s->email;
		case RTCP_SDES_PHONE:
			return s->phone;
		case RTCP_SDES_LOC:
			return s->loc;
		case RTCP_SDES_TOOL:
			return s->tool;
		case RTCP_SDES_NOTE:
			return s->note;
	}
	debug_msg("Unknown SDES item (type=%d)\n", type);
	return NULL;
}

const rtcp_sr *rtp_get_sr(struct rtp *session, u_int32 ssrc)
{
	/* Return the last SR received from this ssrc. The */
	/* caller MUST NOT free the memory returned to it. */
	source	*s = get_source(session, ssrc);

	if (s == NULL) {
		return NULL;
	} 
	return s->sr;
}

const rtcp_rr *rtp_get_rr(struct rtp *session, u_int32 reporter, u_int32 reportee)
{
        return get_rr(session, reporter, reportee);
}

int rtp_send_data(struct rtp *session, u_int32 ts, char pt, int m, int cc, u_int32 csrc[], 
                  char *data, int data_len, char *extn, int extn_len)
{
	int		 buffer_len, i, rc;
	u_int8		*buffer;
	rtp_packet	*packet;

	buffer_len = data_len + extn_len + 12 + (4 * cc);
	buffer     = (u_int8 *) xmalloc(buffer_len + RTP_PACKET_HEADER_SIZE);
	packet     = (rtp_packet *) buffer;

	/* These are internal pointers into the buffer... */
	packet->csrc = (u_int32 *) (buffer + RTP_PACKET_HEADER_SIZE + 12);
	packet->extn = (u_int8  *) (buffer + RTP_PACKET_HEADER_SIZE + 12 + (4 * cc));
	packet->data = (u_int8  *) (buffer + RTP_PACKET_HEADER_SIZE + 12 + (4 * cc) + extn_len);
	/* ...and the actual packet header... */
	packet->v    = 2;
	packet->p    = 0;
	packet->x    = (extn != NULL);
	packet->cc   = cc;
	packet->m    = m;
	packet->pt   = pt;
	packet->seq  = htons(session->rtp_seq++);
	packet->ts   = htonl(ts);
	packet->ssrc = htonl(rtp_my_ssrc(session));
	/* ...now the CSRC list... */
	for (i = 0; i < cc; i++) {
		packet->csrc[i] = htonl(csrc[i]);
	}
	/* ...a header extension? */
	memcpy(packet->extn, extn, extn_len);
	/* ...finally the data itself... */
	memcpy(packet->data, data, data_len);

	rc = udp_send(session->rtp_socket, buffer + RTP_PACKET_HEADER_SIZE, buffer_len);
	xfree(buffer);

	/* Update the RTCP statistics... */
	session->we_sent     = TRUE;
	session->rtp_pcount += 1;
	session->rtp_bcount += buffer_len;

	return rc;
}

static int format_report_blocks(rtcp_rr *rrp, int remaining_length, struct rtp *session)
{
	int nblocks = 0;
	int h;
	source *s;
	struct timeval now;

	gettimeofday(&now, NULL);

	for (h = 0; h < RTP_DB_SIZE; h++) {
		for (s = session->db[h]; s != NULL; s = s->next) {
			if ((nblocks == 31) || (remaining_length < 24)) {
				break; /* Insufficient space for more report blocks... */
			}
			if (s->sender) {
				/* Much of this is taken from A.3 of draft-ietf-avt-rtp-new-01.txt */
				int	extended_max      = s->cycles + s->max_seq;
       				int	expected          = extended_max - s->base_seq + 1;
       				int	lost              = expected - s->received;
				int	expected_interval = expected - s->expected_prior;
       				int	received_interval = s->received - s->received_prior;
       				int 	lost_interval     = expected_interval - received_interval;
				int	fraction;
				u_int32	lsr;
				u_int32 dlsr;

       				s->expected_prior = expected;
       				s->received_prior = s->received;
       				if (expected_interval == 0 || lost_interval <= 0) {
					fraction = 0;
       				} else {
					fraction = (lost_interval << 8) / expected_interval;
				}

				if (s->sr == NULL) {
					lsr = 0;
					dlsr = 0;
				} else {
					lsr = ((s->sr->ntp_sec & 0x0000ffff) << 16) | ((s->sr->ntp_frac & 0xffff0000) >> 16);
					dlsr = tv_diff(now, s->last_sr) * 65536.;
				}
				rrp->ssrc       = htonl(s->ssrc);
				rrp->fract_lost = fraction;
				rrp->total_lost = lost & 0x00ffffff;
				rrp->last_seq   = htonl(extended_max);
				rrp->jitter     = htonl(s->jitter / 16);
				rrp->lsr        = htonl(lsr);
				rrp->dlsr       = htonl(dlsr);
				rrp++;
				s->sender = FALSE;
				remaining_length -= 24;
				nblocks++;
				session->sender_count--;
				if (session->sender_count == 0) {
					break; /* No point continuing, since we've reported on all senders... */
				}
			}
		}
	}
	return nblocks;
}

static u_int8 *format_rtcp_sr(u_int8 *buffer, int buflen, struct rtp *session, u_int32 ts)
{
	/* Write an RTCP SR into buffer, returning a pointer to */
	/* the next byte after the header we have just written. */
	rtcp_t		*packet = (rtcp_t *) buffer;
	int		 remaining_length;
	struct timeval	 curr_time;
	u_int32		 ntp_sec, ntp_frac;

	assert(buflen >= 28);	/* ...else there isn't space for the header and sender report */

	packet->common.version = 2;
	packet->common.p       = 0;
	packet->common.count   = 0;
	packet->common.pt      = RTCP_SR;
	packet->common.length  = htons(1);

	gettimeofday(&curr_time, NULL);
	ntp_sec  = curr_time.tv_sec + SECS_BETWEEN_1900_1970;
	ntp_frac = (curr_time.tv_usec << 12) + (curr_time.tv_usec << 8) - ((curr_time.tv_usec * 3650) >> 6);

	packet->r.sr.sr.ssrc          = htonl(rtp_my_ssrc(session));
	packet->r.sr.sr.ntp_sec       = htonl(ntp_sec);
	packet->r.sr.sr.ntp_frac      = htonl(ntp_frac);
	packet->r.sr.sr.rtp_ts        = htonl(ts);
	packet->r.sr.sr.sender_pcount = htonl(session->rtp_pcount);
	packet->r.sr.sr.sender_bcount = htonl(session->rtp_bcount);

	/* Add report blocks, until we either run out of senders */
	/* to report upon or we run out of space in the buffer.  */
	remaining_length = buflen - 28;
	packet->common.count = format_report_blocks(packet->r.sr.rr, remaining_length, session);
	packet->common.length = htons((u_int16) (6 + (packet->common.count * 6)));
	return buffer + 28 + (24 * packet->common.count);
}

static u_int8 *format_rtcp_rr(u_int8 *buffer, int buflen, struct rtp *session)
{
	/* Write an RTCP RR into buffer, returning a pointer to */
	/* the next byte after the header we have just written. */
	rtcp_t		*packet = (rtcp_t *) buffer;
	int		 remaining_length;

	assert(buflen >= 8);	/* ...else there isn't space for the header */

	packet->common.version = 2;
	packet->common.p       = 0;
	packet->common.count   = 0;
	packet->common.pt      = RTCP_RR;
	packet->common.length  = htons(1);
	packet->r.rr.ssrc      = htonl(session->my_ssrc);

	/* Add report blocks, until we either run out of senders */
	/* to report upon or we run out of space in the buffer.  */
	remaining_length = buflen - 8;
	packet->common.count = format_report_blocks(packet->r.rr.rr, remaining_length, session);
	packet->common.length = htons((u_int16) (1 + (packet->common.count * 6)));
	return buffer + 8 + (24 * packet->common.count);
}

static int add_sdes_item(u_int8 *buf, int type, const char *val)
{
	/* Fill out an SDES item. It is assumed that the item is a NULL    */
	/* terminated string.                                              */
        rtcp_sdes_item *shdr = (rtcp_sdes_item *) buf;
        int             namelen;

        if (val == NULL) {
                debug_msg("Cannot format SDES item. type=%d val=%xp\n", type, val);
                return 0;
        }
        shdr->type = type;
        namelen = strlen(val);
        shdr->length = namelen;
        strcpy(shdr->data, val);
        return namelen + 2;
}

static u_int8 *format_rtcp_sdes(u_int8 *buffer, int buflen, u_int32 ssrc, struct rtp *session)
{
        /* From draft-ietf-avt-profile-new-00:                             */
        /* "Applications may use any of the SDES items described in the    */
        /* RTP specification. While CNAME information is sent every        */
        /* reporting interval, other items should be sent only every third */
        /* reporting interval, with NAME sent seven out of eight times     */
        /* within that slot and the remaining SDES items cyclically taking */
        /* up the eighth slot, as defined in Section 6.2.2 of the RTP      */
        /* specification. In other words, NAME is sent in RTCP packets 1,  */
        /* 4, 7, 10, 13, 16, 19, while, say, EMAIL is used in RTCP packet  */
        /* 22".                                                            */
	u_int8		*packet = buffer;
	rtcp_common	*common = (rtcp_common *) buffer;
	const char	*item;
	size_t		 remaining_len;
        int              pad;

	assert(buflen > (int) sizeof(rtcp_common));

	common->version = 2;
	common->p       = 0;
	common->count   = 1;
	common->pt      = RTCP_SDES;
	common->length  = 0;
	packet += sizeof(common);

	*((u_int32 *) packet) = htonl(ssrc);
	packet += 4;

	remaining_len = buflen - (packet - buffer);
	item = rtp_get_sdes(session, ssrc, RTCP_SDES_CNAME);
	if ((item != NULL) && ((strlen(item) + (size_t) 2) <= remaining_len)) {
		packet += add_sdes_item(packet, RTCP_SDES_CNAME, item);
	}

	remaining_len = buflen - (packet - buffer);
	item = rtp_get_sdes(session, ssrc, RTCP_SDES_NOTE);
	if ((item != NULL) && ((strlen(item) + (size_t) 2) <= remaining_len)) {
		packet += add_sdes_item(packet, RTCP_SDES_NOTE, item);
	}

	remaining_len = buflen - (packet - buffer);
	if ((session->sdes_count_pri % 3) == 0) {
		session->sdes_count_sec++;
		if ((session->sdes_count_sec % 8) == 0) {
			/* Note that the following is supposed to fall-through the cases */
			/* until one is found to send... The lack of break statements in */
			/* the switch is not a bug.                                      */
			switch (session->sdes_count_ter % 4) {
			case 0: item = rtp_get_sdes(session, ssrc, RTCP_SDES_TOOL);
				if ((item != NULL) && ((strlen(item) + (size_t) 2) <= remaining_len)) {
					packet += add_sdes_item(packet, RTCP_SDES_TOOL, item);
					break;
				}
			case 1: item = rtp_get_sdes(session, ssrc, RTCP_SDES_EMAIL);
				if ((item != NULL) && ((strlen(item) + (size_t) 2) <= remaining_len)) {
					packet += add_sdes_item(packet, RTCP_SDES_EMAIL, item);
					break;
				}
			case 2: item = rtp_get_sdes(session, ssrc, RTCP_SDES_PHONE);
				if ((item != NULL) && ((strlen(item) + (size_t) 2) <= remaining_len)) {
					packet += add_sdes_item(packet, RTCP_SDES_PHONE, item);
					break;
				}
			case 3: item = rtp_get_sdes(session, ssrc, RTCP_SDES_LOC);
				if ((item != NULL) && ((strlen(item) + (size_t) 2) <= remaining_len)) {
					packet += add_sdes_item(packet, RTCP_SDES_LOC, item);
					break;
				}
			}
			session->sdes_count_ter++;
		} else {
			item = rtp_get_sdes(session, ssrc, RTCP_SDES_NAME);
			if (item != NULL) {
				packet += add_sdes_item(packet, RTCP_SDES_NAME, item);
			}
		}
	}
	session->sdes_count_pri++;

	/* Pad to a multiple of 4 bytes... */
        pad = 4 - ((packet - buffer) & 0x3);
        while (pad--) {
               *packet++ = RTCP_SDES_END;
        }

	common->length = htons((u_int16) (((int) (packet - buffer) / 4) - 1));

	return packet;
}

static void send_rtcp(struct rtp *session, u_int32 ts)
{
	/* Construct and send an RTCP packet. The order in which packets are packed into a */
	/* compound packet is defined by section 6.1 of draft-ietf-avt-rtp-new-03.txt and  */
	/* we follow the recommended order.                                                */
	u_int8	 buffer[RTP_MAX_PACKET_LEN];
	u_int8	*ptr = buffer;

	/* The first RTCP packet in the compound packet MUST always be a report packet...  */
	if (session->we_sent) {
		ptr = format_rtcp_sr(ptr, RTP_MAX_PACKET_LEN - (ptr - buffer), session, ts);
	} else {
		ptr = format_rtcp_rr(ptr, RTP_MAX_PACKET_LEN - (ptr - buffer), session);
	}
	/* Following that, additional RR packets SHOULD follow if there are more than 31   */
	/* senders (such that the reports do not fit into the initial packet. We give up   */
	/* if there is insufficient space in the buffer: this is bad, since we always drop */
	/* the reports from the same sources (those at the end of the hash table).         */
	while ((session->sender_count > 0)  && ((RTP_MAX_PACKET_LEN - (ptr - buffer)) > 0)) {
		ptr = format_rtcp_rr(ptr, RTP_MAX_PACKET_LEN - (ptr - buffer), session);
	}

	/* Add the appropriate SDES items to the packet... */
	ptr = format_rtcp_sdes(ptr, RTP_MAX_PACKET_LEN - (ptr - buffer), rtp_my_ssrc(session), session);
	udp_send(session->rtcp_socket, buffer, ptr - buffer);

        /* Loop the data back to ourselves so local participant can */
        /* query own stats when using unicast or multicast with no  */
        /* loopback.                                                */
        rtp_process_ctrl(session, buffer, ptr - buffer);
}

void rtp_send_ctrl(struct rtp *session, u_int32 ts)
{
	/* Send an RTCP packet, if one is due... */
	struct timeval	 curr_time;

	gettimeofday(&curr_time, NULL);
	if (tv_gt(curr_time, session->next_rtcp_send_time)) {
		/* The RTCP transmission timer has expired. The following */
		/* implements draft-ietf-avt-rtp-new-02.txt section 6.3.6 */
		int		 h;
		source		*s;
		struct timeval	 new_send_time;
		double		 new_interval;

		new_interval  = rtcp_interval(session, TRUE);
		new_send_time = session->last_rtcp_send_time;
		tv_add(&new_send_time, new_interval);
		if (tv_gt(curr_time, new_send_time)) {
			send_rtcp(session, ts);
			session->initial_rtcp        = FALSE;
			session->we_sent             = FALSE;
			session->last_rtcp_send_time = curr_time;
			session->next_rtcp_send_time = curr_time; 
			tv_add(&(session->next_rtcp_send_time), new_interval);
			/* We're starting a new RTCP reporting interval, zero out */
			/* the per-interval statistics.                           */
			session->sender_count = 0;
			for (h = 0; h < RTP_DB_SIZE; h++) {
				for (s = session->db[h]; s != NULL; s = s->next) {
					s->sender = FALSE;
				}
			}
		} else {
			session->next_rtcp_send_time = session->last_rtcp_send_time; 
			tv_add(&(session->next_rtcp_send_time), new_interval);
		}
	} 
}

void rtp_update(struct rtp *session)
{
	/* Perform housekeeping on the source database... */
	int	 	 h;
	source	 	*s, *n;
	struct timeval	 curr_time;
	double		 delay;

	gettimeofday(&curr_time, NULL);

	for (h = 0; h < RTP_DB_SIZE; h++) {
		for (s = session->db[h]; s != NULL; s = n) {
			n = s->next;
			/* Expire sources which haven't been heard from for a long time.   */
			/* Section 6.2.1 of the RTP specification details the timers used. */
			delay = tv_diff(curr_time, s->last_active);
			
			/* Check if we've received a BYE packet from this source.    */
			/* If we have, and it was received more than 2 seconds ago   */
			/* then the source is deleted. The arbitrary 2 second delay  */
			/* is to ensure that all delayed packets are received before */
			/* the source is timed out.                                  */
			if (s->got_bye && (delay > 2.0)) {
				delete_source(session, s->ssrc);
			}
			/* If a source hasn't been heard from for more than 5 RTCP   */
			/* reporting intervals, we mark the source as inactive.      */
			/* TO BE DONE */
		}
	}
}

void rtp_send_bye(struct rtp *session)
{
	/* The function sends an RTCP BYE packet. It should implement BYE reconsideration, */
	/* at present it either: a) sends a BYE packet immediately (if there are less than */
	/* 50 members in the group), or b) returns without sending a BYE (if there are 50  */
	/* or more members). See draft-ietf-avt-rtp-new-01.txt (section 6.3.7).            */
	u_int8	 buffer[RTP_MAX_PACKET_LEN];
	u_int8	*ptr = buffer;
	rtcp_common	*common;

	if (session->ssrc_count < 50) {
		ptr = format_rtcp_rr(ptr, RTP_MAX_PACKET_LEN - (ptr - buffer), session);    
		common = (rtcp_common *) ptr;
		
		common->version = 2;
		common->p       = 0;
		common->count   = 1;
		common->pt      = RTCP_BYE;
		common->length  = ntohs(1);
		ptr += sizeof(common);
		
		*((u_int32 *) ptr) = htonl(session->my_ssrc);  
		ptr += sizeof(session->my_ssrc); 
		
		udp_send(session->rtcp_socket, buffer, ptr - buffer);
	}
}

void rtp_done(struct rtp *session)
{
	int i;
        source *s, *n;

        /* In delete_source, check database gets called and this assumes */
        /* first added and last removed is us.                           */
	for (i = 0; i < RTP_DB_SIZE; i++) {
                s = session->db[i];
                while (s != NULL) {
                        n = s->next;
                        if (s->ssrc != session->my_ssrc) {
                                delete_source(session,session->db[i]->ssrc);
                        }
                        s = n;
                }
	}

	delete_source(session, session->my_ssrc);

        if (session->encryption_key != NULL) {
                xfree(session->encryption_key);
        }

	udp_exit(session->rtp_socket);
	udp_exit(session->rtcp_socket);
	xfree(session->addr);
	xfree(session);
}

int rtp_set_encryption_key(struct rtp* session, const char *key)
{
        UNUSED(session);
        UNUSED(key);

        if (session->encryption_key != NULL) {
                xfree(session->encryption_key);
        }
        session->encryption_key = xstrdup(key);
        debug_msg("rtp_set_encyption_key not yet supported.\n");
        return FALSE;
}

const char *rtp_get_encryption_key(struct rtp* session)
{
        return session->encryption_key;
}

char *rtp_get_addr(struct rtp *session)
{
	return session->addr;
}

u_int16 rtp_get_rx_port(struct rtp *session)
{
	return session->rx_port;
}

u_int16 rtp_get_tx_port(struct rtp *session)
{
	return session->tx_port;
}

int rtp_get_ttl(struct rtp *session)
{
	return session->ttl;
}


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <assert.h>
#ifdef _WIN32
#include <time.h>
#else
#include <sys/time.h>
#endif

#include "inet.h"
#include "net.h"
#include "rtp.h"
#include "vic_tcl.h"
#include "transmitter.h"
#include "pktbuf-rtp.h"
#include "module.h"

#include "databuffer.h"
#include "x264encoder.h"

#include <iostream>

static Transmitter *tx;
static RTP_BufferPool *pool;
static u_int32_t ts;
static unsigned char frame_seq;

class H264Encoder:public TransmitterModule
{
  public:
    H264Encoder();
    ~H264Encoder();

    void setq(int q);
    int command(int argc, const char *const *argv);

    void size(int w, int h);
    int consume(const VideoFrame *);
    static void rtp_callback(void *data, int size, int packet_number);

  protected:
    int fps, kbps, gop;
    bool state;
    //UCHAR* bitstream;

    x264Encoder *enc;
    DataBuffer *fIn;
    DataBuffer *fOut;
    timeval timeStamp;
    time_t prevTime;
    unsigned short prevTimeMS;

    FILE *fptr;
};

static class H264EncoderMatcher:public Matcher
{
  public:
    H264EncoderMatcher():Matcher("module")
    {
    }
    TclObject *match(const char *fmt)
    {
	if (strcasecmp(fmt, "h264") == 0)
	    return (new H264Encoder);
	return (0);
    }
}

encoder_matcher_h264;

H264Encoder::H264Encoder():TransmitterModule(FT_YUV_CIF)
{
    enc = new x264Encoder();
    state = false;
    fptr = NULL;
    frame_seq = 0;
    fps = 20;
    kbps = 512;
    gop = 20;
}

H264Encoder::~H264Encoder()
{
    delete enc;
    delete fIn;
    delete fOut;
    //fclose(fptr);
}

void H264Encoder::size(int w, int h)
{
    debug_msg("H264: WxH %dx%d\n", w, h);
    Module::size(w, h);
    fIn = new DataBuffer(w * h * 3 >> 1);
    fOut = new DataBuffer(w * h * 3 >> 1);
}

int H264Encoder::command(int argc, const char *const *argv)
{
    if (argc == 3) {
	if (strcmp(argv[1], "q") == 0) {
	    // mpeg4.quality = atoi(argv[2]);
	    // mpeg4.set_max_quantizer(mpeg4.quality);
	    gop = atoi(argv[2]);
	    //if(enc->isInitialized())
	    //      enc->setGOP(gop);
	    //h264.set_gop(gop);
	    return (TCL_OK);
	}
	else if (strcmp(argv[1], "fps") == 0) {
	    fps = atoi(argv[2]);
	    //std::cout << "H264: fps " << fps << "\n";
	    return (TCL_OK);
	}
	else if (strcmp(argv[1], "kbps") == 0) {
	    kbps = atoi(argv[2]);
	    if (kbps < 64)
		kbps = 64;
	    //std::cout << "H264: kbps " << kbps << "\n";
	    return (TCL_OK);
	}
	else if (strcmp(argv[1], "hq") == 0) {
	    int enable_hq = atoi(argv[2]);
	    // printf("enable h264 high quality encoding\n");
	    //h264.enable_hq_encoding = bool(enable_hq);
	    return (TCL_OK);
	}
    }
    return (TransmitterModule::command(argc, argv));
    return 0;
}

int H264Encoder::consume(const VideoFrame * vf)
{
    pktbuf *pb;
    rtphdr *rh;
    //int n,ps, len;
    ts = vf->ts_;
    tx = tx_;
    pool = pool_;
    bool first = true;

    int i_nal, i, sent_size;
    int frame_size = 0;

    tx->flush();
    if (!state) {
	state = true;
	size(vf->width_, vf->height_);
	debug_msg("init x264 encoder with kbps:%d, fps:%d", kbps, fps);
	enc->init(vf->width_, vf->height_, kbps, fps);
	enc->setGOP(gop);
	frame_size = vf->width_ * vf->height_;
	//fptr = fopen("out.m4v", "w");
	gettimeofday(&timeStamp, NULL);
	prevTime = timeStamp.tv_sec;
	prevTimeMS = timeStamp.tv_sec;
    }

    frame_size = vf->width_ * vf->height_;
    char *data = fIn->getData();
    memcpy(data, vf->bp_, frame_size * 3 >> 1);
    enc->encodeFrame(fIn);

    i_nal = enc->numNAL();

    sent_size = 0;
    unsigned char f_seq = 0;
    unsigned char f_total_pkt = 0;
    //TODO: send out i_nal packets
    for (i = 0; i < i_nal; i++) {

	enc->getNALPacket(i, fOut);

	sent_size += fOut->getDataSize();
	data = fOut->getData();
	//DEBUG
	//fwrite(data, fOut->getFrameSize(), 1, fptr);

	int nalSize = fOut->getDataSize();
	if (i == i_nal - 1 && f_total_pkt == 0)
	    f_total_pkt = f_seq + (nalSize / 1000) + 2;
	int offset = 0;
	while (nalSize > 0) {
	    pb = pool_->alloc(vf->ts_, RTP_PT_H264);
	    rh = (rtphdr *) pb->data;
	    if (nalSize > 1000) {
		memcpy(&pb->data[14], data + offset, 1000);
		//DEBUG
		//fwrite(data+offset, 1000, 1, fptr);
		pb->len = 1000 + 14;
		offset += 1000;
		nalSize -= 1000;
	    }
	    else {
		if (i == i_nal - 1) {
		    //Last Packet
		    rh->rh_flags |= htons(RTP_M);	// set M bit
		}
		memcpy(&pb->data[14], data + offset, nalSize);
		//DEBUG
		//printf("encode a frame...\n");
		//fwrite(data+offset, nalSize, 1, fptr);

		pb->len = nalSize + 14;
		nalSize = 0;
	    }
	    //printf("send out %d\n", pb->len);
	    tx->send(pb);
	    f_seq++;
	}
    }
    frame_seq++;

    return (kbps*1024) / (fps*8);
}
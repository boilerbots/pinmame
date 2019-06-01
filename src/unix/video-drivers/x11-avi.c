
#ifdef AVICAPTURE

#include <math.h>
#include <X11/Xlib.h>
#include "xmame.h"
#include "x11.h"
#include "input.h"
#include "keyboard.h"
 
//typedef unsigned char uint8_t;
//typedef unsigned short uint16_t;
//typedef unsigned int uint32_t;
//typedef unsigned long long uint64_t;

#include <libavutil/common.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

const int BUFFSIZE=1000000;
FILE * video_outf;
AVCodec * avc;
AVCodecContext * avctx;
AVPicture inpic, outpic;
char * output_buffer;
AVFrame * pic;
AVPacket * avpkt;
static int frame_count = 0;
int frame_halver=2; // save each frame=1,  save every other frame=2

void init_dumper( int width, int height )
{  
  double fps = Machine->drv->frames_per_second / (double)frame_halver;

  printf("init_dumper: width=%d  height=%d\n", width, height);
  avcodec_register_all();
  //av_log_set_level (99);

  avc = avcodec_find_encoder( AV_CODEC_ID_MPEG2VIDEO );
  if (avc == NULL)
  {
  	  printf ("cannot find MPEG encoder\n");
     exit (1);
  }

  avctx = avcodec_alloc_context3(avc);
    
#if 1
  /* sample parameters */
  //avctx->me_method = ME_LOG;
  avctx->pix_fmt = AV_PIX_FMT_YUV420P;
  avctx->bit_rate = 2500000;
  avctx->width = width;
  avctx->height = height;
  avctx->time_base.num = 1;
  avctx->time_base.den = fps;
  avctx->gop_size=10;
  avctx->max_b_frames=1;
  //avctx->draw_horiz_band = NULL;
  //avctx->idct_algo = FF_IDCT_AUTO;
#endif

  int ret = avcodec_open2( avctx, avc, NULL );
  if (ret)
    {
      printf("FAILED TO OPEN CODEC, ret=%d, errno=%d\n", ret, errno);
      exit( 1 );
    }
  
  int size=height*width;
  
  pic = av_frame_alloc();
  avpkt = av_packet_alloc();
  
  output_buffer=(char *)malloc(BUFFSIZE); /* Find where this value comes from */
  
  av_image_alloc( &outpic.data, &outpic.linesize, width, height, AV_PIX_FMT_YUV420P, 32);

#if 0
  outpic.data[0]=(unsigned char *)malloc(size*3/2); /* YUV 420 Planar */
  outpic.data[1]=outpic.data[0]+size;
  outpic.data[2]=outpic.data[1]+size/4;
  outpic.data[3]=NULL;
  outpic.linesize[0]=width;
  outpic.linesize[1]=outpic.linesize[2]=width/2;
  outpic.linesize[3]=0;
#endif
  
#if 1
  av_image_alloc( &inpic.data, &inpic.linesize, width, height, AV_PIX_FMT_RGB24, 32);
#else
  inpic.data[0]=(unsigned char *)malloc(size*3); /* RGB24 packed in 1 plane */
  inpic.data[1]=inpic.data[2]=inpic.data[3]=NULL;
  inpic.linesize[0]=width*3;
  inpic.linesize[1]=inpic.linesize[2]=inpic.linesize[3]=0;
#endif

  video_outf = fopen("video.outf","wb");
  if (video_outf == NULL)
  {
    printf ("failed to open output video file\n");
    exit (1);
  }

  pic->format = avctx->pix_fmt;
  pic->width = avctx->width;
  pic->height = avctx->height;

  ret = av_frame_get_buffer(pic, 32);
  if (ret)
    {
      printf("could not allocate a frame buffer.\n");
      exit( 1 );
    }
  
}

void frame_dump ( struct mame_bitmap * bitmap )
{
  static unsigned int *dumpbig = NULL;
  unsigned char *dumpd;
  int y;
  int xoff, yoff, xsize, ysize;
  int outsz;
  static int framecnt=0;
  static unsigned char * myoutframe;

  framecnt++;
  if ((framecnt % frame_halver) != 0)
    return; // skip this frame

#if 0
  xoff = Machine->visible_area.min_x;
  yoff = Machine->visible_area.min_y;
  xsize= Machine->visible_area.max_x-xoff+1;
  ysize = Machine->visible_area.max_y-yoff+1;
#endif

  //printf("w=%d h=%d  rowpixels=%d  rowbytes=%d\n", bitmap->width, bitmap->height, bitmap->rowpixels, bitmap->rowbytes);

  xsize = avctx->width;
  ysize = avctx->height;
  xoff=0;
  yoff=0; 
	if (!dumpbig)
	{
		int dstsize = bitmap->width *bitmap->height * sizeof (unsigned int);
		dumpbig = malloc ( dstsize );
		myoutframe = malloc( dstsize );
  }

  dumpd = (unsigned char*)dumpbig;

	/* Blit into dumpbig */
#define INDIRECT current_palette->lookup
#define DEST dumpbig
#define DEST_WIDTH (bitmap->width)
#define SRC_PIXEL unsigned short
#define DEST_PIXEL unsigned int
#define PACK_BITS
#include "blit.h"
#undef PACK_BITS
#undef SRC_PIXEL
#undef DEST_PIXEL
#undef INDIRECT

	/* Now make some corrections. */
	for (y=0; y < ysize; y++)
   {
   	 int offs = bitmap->width*(y+yoff)*4;
#if 0
     int x;

     for(x=0; x < xsize; x++)
	   {
	   	 unsigned char c;
       c = dumpd[offs+x*3+2];
       dumpd[offs+x*3+2] = dumpd[offs+x*3];
       dumpd[offs+x*3] = c;
	   }
#endif

		memcpy( &myoutframe[xsize*y*3], &dumpd[offs+3*xoff], xsize*3 );
	}

	/* dumpd now contains a nice RGB (or somethiing) frame.. */
  //inpic.data[0] = myoutframe;

  //img_convert(&outpic, AV_PIX_FMT_YUV420P, &inpic, AV_PIX_FMT_RGB24, xsize, ysize); 
  struct SwsContext *ctx;
  ctx = sws_getContext(xsize, ysize, AV_PIX_FMT_BGR24, xsize, ysize, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
  avpicture_fill(&inpic, myoutframe, AV_PIX_FMT_BGR24, xsize, ysize);
  //avpicture_fill(&inpic, dumpbig, AV_PIX_FMT_BGR24, xsize, ysize);

  sws_scale(ctx, inpic.data, inpic.linesize, 0, ysize, outpic.data, outpic.linesize); 
  
  av_frame_make_writable(pic);

  pic->pts = frame_count++;
  memcpy(pic->data[0], outpic.data[0], pic->linesize[0] * pic->height);
  memcpy(pic->data[1], outpic.data[1], pic->linesize[1] * pic->height / 2);
  memcpy(pic->data[2], outpic.data[2], pic->linesize[2] * pic->height / 2);
#if 0
  pic->data[0]=outpic.data[0];  /* Points to data portion of outpic     */
  pic->data[1]=outpic.data[1];  /* Since encode_video takes an AVFrame, */
  pic->data[2]=outpic.data[2];  /* and img_convert takes an AVPicture   */
  pic->data[3]=outpic.data[3];
  
  pic->linesize[0]=outpic.linesize[0]; /* This doesn't change */
  pic->linesize[1]=outpic.linesize[1];
  pic->linesize[2]=outpic.linesize[2];
  pic->linesize[3]=outpic.linesize[3];
#endif

  int ret = avcodec_send_frame(avctx, pic);
  if (ret < 0)
  {
    fprintf(stderr, "Error sending frame for encoding.\n");
    exit(1);
  }


  while (ret >= 0) 
  {
    ret = avcodec_receive_packet(avctx, avpkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        return;
    else if (ret < 0) {
        fprintf(stderr, "error during encoding\n");
        exit(1);
    }
    printf("encoded frame %3"PRId64" (size=%5d)\n", avpkt->pts, avpkt->size);
    fwrite(avpkt->data, 1, avpkt->size, video_outf);
    av_packet_unref(avpkt);
  }

  //outsz = avcodec_encode_video2 (avctx, &avpkt, pic, &got_pkt);
  //fwrite(output_buffer, 1, got_pkt, video_outf);
}


void done_dumper()
{
  printf("killing dumper...\n");
  avcodec_close(avctx);
  // there might have been something with buffers and b frames that 
  // i should have taken care of at this point.. oh well

  if (video_outf)
  {
     fwrite("\0x00\0x00\0x01\0xb7", 1, 4, video_outf); // mpeg end sequence..
     fclose(video_outf);
      video_outf = NULL;
  }

  avcodec_free_context(&avctx);
  av_freep(&pic);
  av_freep(&avpkt);
}


#endif /* AVICAPTURE */


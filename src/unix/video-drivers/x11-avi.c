
#ifdef AVICAPTURE

#include <math.h>
#include <X11/Xlib.h>
#include "xmame.h"
#include "x11.h"
#include "input.h"
#include "keyboard.h"
 
#include <libavutil/common.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

FILE * video_outf;
AVCodec * avc;
AVCodecContext * avctx;
struct SwsContext *ctx;
AVPicture inpic, outpic;
AVFrame * pic;
AVPacket * avpkt;
static int frame_count = 0;
int frame_halver=2; // save each frame=1,  save every other frame=2
static unsigned int *dumpbig = NULL;
static unsigned char *myoutframe;
static int framecnt=0;

void init_dumper( int width, int height )
{  
  double fps = Machine->drv->frames_per_second / (double)frame_halver;
  framecnt=0;

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
  ctx = sws_getContext(width, height, AV_PIX_FMT_BGR24, width, height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

  /* sample parameters */
  avctx->pix_fmt = AV_PIX_FMT_YUV420P;
  avctx->bit_rate = 2500000;
  avctx->width = width;
  avctx->height = height;
  avctx->time_base.num = 1;
  avctx->time_base.den = fps;
  avctx->gop_size=10;
  avctx->max_b_frames=1;

  int ret = avcodec_open2( avctx, avc, NULL );
  if (ret)
    {
      printf("FAILED TO OPEN CODEC, ret=%d, errno=%d\n", ret, errno);
      exit( 1 );
    }
  
  
  pic = av_frame_alloc();
  avpkt = av_packet_alloc();
  
  av_image_alloc( &outpic.data, &outpic.linesize, width, height, AV_PIX_FMT_YUV420P, 32);
  av_image_alloc( &inpic.data, &inpic.linesize, width, height, AV_PIX_FMT_RGB24, 32);

  char fname[32];
  static short file_count = 0;
  sprintf(fname, "video%d.mpg", file_count++);  //open a new file everytime video changes
  video_outf = fopen(fname,"wb");
  if (video_outf == NULL)
  {
    printf ("failed to open output video file\n");
    exit (1);
  }

  pic->format = avctx->pix_fmt;
  pic->width = avctx->width;
  pic->height = avctx->height;
  
  pic->data[0]=outpic.data[0];  /* Points to data portion of outpic     */
  pic->data[1]=outpic.data[1];  /* Since encode_video takes an AVFrame, */
  pic->data[2]=outpic.data[2];  /* and img_convert takes an AVPicture   */
  pic->data[3]=outpic.data[3];
  
  pic->linesize[0]=outpic.linesize[0]; /* This doesn't change */
  pic->linesize[1]=outpic.linesize[1];
  pic->linesize[2]=outpic.linesize[2];
  pic->linesize[3]=outpic.linesize[3];

}

void frame_dump ( struct mame_bitmap * bitmap )
{
  unsigned char *dumpd;
  int y;
  int xoff, yoff, xsize, ysize;
  int outsz;

  framecnt++;
  if ((framecnt % frame_halver) != 0)
    return; // skip this frame

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

	// Seems that the bitmap data needs to be shifted
	for (y=0; y < ysize; y++)
  {
    int offs = bitmap->width*(y+yoff)*4;
    memcpy( &myoutframe[xsize*y*3], &dumpd[offs+3*xoff], xsize*3 );
	}


  avpicture_fill(&inpic, myoutframe, AV_PIX_FMT_BGR24, xsize, ysize);
  // Convert RGB to YUV
  sws_scale(ctx, inpic.data, inpic.linesize, 0, ysize, outpic.data, outpic.linesize); 
  
  av_frame_make_writable(pic);

  pic->pts = frame_count++;

  avpkt->data = NULL;
  avpkt->size = 0;
  av_init_packet(avpkt);

  int got_pkt;
  int ret = avcodec_encode_video2 (avctx, avpkt, pic, &got_pkt);
  if (ret < 0)
  {
    fprintf (stderr, "avcodec_encode_video2: FAILED error=%d\n", ret);
    exit (1);
  }
  if (got_pkt > 0)
  {
    printf("encoded frame %3"PRId64" (size=%5d)\n", avpkt->pts, avpkt->size);
    fwrite(avpkt->data, 1, avpkt->size, video_outf);
    av_packet_unref(avpkt);
  }
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
  sws_freeContext(ctx);
  av_freep(&pic);
  av_freep(&avpkt);

  free(dumpbig);
  free(myoutframe);
  dumpbig = NULL;
  myoutframe = NULL;
}


#endif /* AVICAPTURE */


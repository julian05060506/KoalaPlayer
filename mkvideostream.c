/*
 * Copyright (c) 2005 Francois Revol
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libavutil/time.h"
#include "libavutil/intreadwrite.h"

#include "libavformat/avformat.h"
#define MUXER_ES
#define AVP_BUFFSIZE 4096
#define INITIAL_BUFFER_SIZE 32768
#define  MAX_PKT_SIZE   1024*1024



typedef struct {
	AVIOContext *in_put_pb;
	uint8_t* read_buffer; //in put buffer cache
	AVFormatContext *ctx;
	AVPacket *pkt;
	int video_stream;
	int audio_stream;
	AVFormatContext *oc; //raw audio data to es use it
	AVStream * ast; //raw audio data to es use it
	AVCodecContext *ac;
	AVCodecContext *vc;
	uint8_t *audio_input_buffer; //raw audio data to es use it

	int a_time_base;
	int v_time_base;
	int write_h264_sps_pps;
	int write_h264_startcode;
	int mp4_vol;//mpeg4 video header
	uint8_t *pPkt_buf;
	int *pPkt_buf_size;

	//get in put data callback
	int (*read_packet)(void *opaque, uint8_t *buf, int buf_size);
	int64_t (*seek)(void *opaque, int64_t offset, int whence);
	void *opaque;
	
	
}koala_handle;

static int probe_buf_write(void *opaque, uint8_t *buf, int buf_size)
{
	koala_handle *pHandle = (koala_handle *)opaque;
	if ((*pHandle->pPkt_buf_size) < buf_size){
		printf("%s:%d buf_size is %d pPkt_buf_size is %d\n",__FILE__,__LINE__,buf_size,*pHandle->pPkt_buf_size);
		
	}

    memcpy(pHandle->pPkt_buf,buf,buf_size);
	*(pHandle->pPkt_buf_size) = buf_size;
    return 0;
}


void regist_input_file_func(koala_handle *pHandle,void *opaque,int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
								int64_t (*seek)(void *opaque, int64_t offset, int whence)){
	pHandle->read_packet = read_packet;
	pHandle->seek = seek;
	pHandle->opaque = opaque;
	return;
	
								
}
int init_open(koala_handle *pHandle){
	int ret;
	AVInputFormat *in_fmt = NULL;
	pHandle->read_buffer = av_malloc(INITIAL_BUFFER_SIZE);
	if (pHandle->read_buffer == NULL)
		return -1;
	if (pHandle->read_packet == NULL){
		printf("%s:%d no read func\n",__FILE__,__LINE__);
		// TODO: open with filename
		return -1;
	}
	pHandle->ctx = avformat_alloc_context();
	av_register_all();

	pHandle->in_put_pb = avio_alloc_context(pHandle->read_buffer, INITIAL_BUFFER_SIZE,0, pHandle->opaque, pHandle->read_packet, NULL, pHandle->seek);
	if (pHandle->seek)
		pHandle->in_put_pb->seekable = 1;
	else
		pHandle->in_put_pb->seekable = 0;
	ret = av_probe_input_buffer(pHandle->in_put_pb, &in_fmt, NULL,NULL, 0, 0);
	if (ret < 0) {
		printf("%s:%d\n",__FILE__,__LINE__);
	    goto fail;
    }
	printf("in_fmt->name %s\n",in_fmt->name);
	pHandle->ctx->pb = pHandle->in_put_pb;
    ret = avformat_open_input(&pHandle->ctx, NULL, in_fmt, NULL);
    if (ret < 0){
		printf("%s:%d ret is %d\n",__FILE__,__LINE__,ret);
        goto fail;
    }
	ret = avformat_find_stream_info(pHandle->ctx, NULL);
	if (ret < 0) {
		fprintf(stderr, "avformat_find_stream_info: error %d\n", ret);
		return -1;
	}
	pHandle->pkt = (AVPacket *)av_malloc(sizeof(AVPacket));
	av_init_packet(pHandle->pkt);
 	return 0;
	
fail:
	if (pHandle->read_buffer){
		//av_free(pHandle->read_buffer);
		//pHandle->read_buffer = NULL;
	}
	if (pHandle->ctx){
		avformat_free_context(pHandle->ctx);
	    pHandle->ctx = NULL;
	}
	return -1;
	
	
}
void close_demux(koala_handle *pHandle){
	if (pHandle->oc){
		avio_flush(pHandle->oc->pb);
		avformat_free_context(pHandle->oc);
	}
	avformat_close_input(&pHandle->ctx);
	av_free(pHandle->pkt);
	if (pHandle->audio_input_buffer)
		av_free(pHandle->audio_input_buffer);
	av_free(pHandle);

}
int select_default_stream(koala_handle *pHandle){
	int i;
	for(i = 0;i<pHandle->ctx->nb_streams;i++){
		if (pHandle->ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
			pHandle->video_stream = i;
		if (pHandle->ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
			pHandle->audio_stream = i;
		if (pHandle->video_stream >= 0 && pHandle->audio_stream >= 0)
			break;
	}
	return 0;
}

int open_audio(koala_handle *pHandle){
//	AVCodec *acodec;
//	char filename[32];
	int err;
	pHandle->audio_input_buffer = av_malloc(AVP_BUFFSIZE);
	pHandle->ac = pHandle->ctx->streams[pHandle->audio_stream]->codec;
//	acodec = avcodec_find_decoder(pHandle->ac->codec_id);
//	printf("%s\n",acodec->name);
//	sprintf(filename,"audio.%s",acodec->name);
	if (pHandle->ac->codec_id == CODEC_ID_AAC){
	    pHandle->oc = avformat_alloc_context();
	    if (!pHandle->oc) {
	        fprintf(stderr, "Memory error\n");
	        return 1;
	    }
		pHandle->oc->oformat = av_guess_format("adts", NULL, NULL);
		pHandle->oc->pb = avio_alloc_context(pHandle->audio_input_buffer, AVP_BUFFSIZE,0, pHandle, NULL, probe_buf_write, NULL);
		if (pHandle->oc->pb == NULL){
			printf("%s:%d\n",__FILE__,__LINE__);
			return -1;
		}
    	pHandle->oc->pb->seekable = 0;
	//	printf("a_oformat->audio_codec is %d aac is %d\n",pHandle->oc->oformat->audio_codec,CODEC_ID_AAC);
		
		pHandle->ast = avformat_new_stream(pHandle->oc, NULL);
		if (pHandle->ast == NULL){
			printf("%s:%d\n",__FILE__,__LINE__);
			return -1;
		}
		err = avcodec_copy_context(pHandle->ast->codec,pHandle->ac);
		if (err < 0){
			printf("%s:%d\n",__FILE__,__LINE__);
			return -1;
		}
		err = avformat_write_header(pHandle->oc, NULL);
		if (err < 0){
			printf("%s:%d\n",__FILE__,__LINE__);
			return -1;
		}	
	}
	pHandle->a_time_base = pHandle->ctx->streams[pHandle->audio_stream]->time_base.den/pHandle->ctx->streams[pHandle->audio_stream]->time_base.num;
	return 0;
}

int open_video(koala_handle *pHandle){
//	AVCodec *vcodec;
//	char filename[32];
	//AVCodecContext *vc;
	int err = 0;
	pHandle->vc = pHandle->ctx->streams[pHandle->video_stream]->codec;
//	vcodec = avcodec_find_decoder(pHandle->vc->codec_id);
//	printf("%s\n",vcodec->name);
//	sprintf(filename,"video.%s",vcodec->name);
//	printf("%s\n",filename);
//	pHandle->vfd  = open(filename, O_WRONLY | O_CREAT, 0644);
	pHandle->v_time_base = pHandle->ctx->streams[pHandle->video_stream]->time_base.den/pHandle->ctx->streams[pHandle->video_stream]->time_base.num;

	if ((pHandle->vc->codec_id == CODEC_ID_H264)
	&& pHandle->vc->extradata != NULL 
	&&(pHandle->vc->extradata[0] == 1)){
	    uint8_t *dummy_p;
	    int dummy_int;
		AVBitStreamFilterContext *bsfc= av_bitstream_filter_init("h264_mp4toannexb");
		if (!bsfc) {
			av_log(NULL, AV_LOG_ERROR, "Cannot open the h264_mp4toannexb BSF!\n");
			return -1;
		}
	    err = av_bitstream_filter_filter(bsfc, pHandle->vc, NULL, &dummy_p, &dummy_int, NULL, 0, 0);
		if (err < 0)
			printf("av_bitstream_filter_filter error\n");
	    av_bitstream_filter_close(bsfc);
		pHandle->write_h264_sps_pps = 1;
		pHandle->write_h264_startcode =1;
	}else 
	if (pHandle->vc->codec_id == CODEC_ID_MPEG4){
		pHandle->mp4_vol = 1;
	}
	return err;
}
int demux_read_packet(koala_handle *pHandle,uint8_t *pBuffer,int *pSize,int * pStream,int64_t *pPts){
	int err;
	int keyframe;
	uint8_t startcode[4] = {0,0,0,1};
	err = av_read_frame(pHandle->ctx, pHandle->pkt);
	if (err < 0)
		return -1;
	pHandle->pPkt_buf = pBuffer;
	pHandle->pPkt_buf_size  = pSize;
	*pStream = pHandle->pkt->stream_index;

	if (pHandle->pkt->stream_index == pHandle->video_stream){
		if (pHandle->pkt->flags &AV_PKT_FLAG_KEY)
			keyframe = 1;
		else
			keyframe = 0;
		pHandle->pkt->pts = pHandle->pkt->pts*1000 /pHandle->v_time_base;//ms
		*pPts = pHandle->pkt->pts;
		*pSize = 0;

		if (pHandle->vc->codec_id == CODEC_ID_MPEG4 && keyframe &&pHandle->mp4_vol){
			memcpy(pBuffer+(*pSize),pHandle->vc->extradata ,pHandle->vc->extradata_size);
			*pSize += pHandle->vc->extradata_size;
		}

		if (pHandle->write_h264_sps_pps && keyframe){
			memcpy(pBuffer + (*pSize),pHandle->vc->extradata ,pHandle->vc->extradata_size);
			*pSize += pHandle->vc->extradata_size;
			memcpy(pBuffer + (*pSize),startcode,4);
			*pSize +=4;
			memcpy(pBuffer + (*pSize),pHandle->pkt->data+4,pHandle->pkt->size-4);
			*pSize += (pHandle->pkt->size-4);
		}else{
			if (pHandle->write_h264_startcode){
				memcpy(pBuffer + (*pSize),startcode,4);
				*pSize += 4;			
				memcpy(pBuffer + (*pSize),pHandle->pkt->data+4,pHandle->pkt->size-4);
				*pSize += (pHandle->pkt->size-4);				
			}else{
				memcpy(pBuffer + (*pSize),pHandle->pkt->data,pHandle->pkt->size);
				*pSize += pHandle->pkt->size;
			}
		}

	}
	else if (pHandle->pkt->stream_index == pHandle->audio_stream){
		if (pHandle->ac->codec_id == CODEC_ID_AAC &&((AV_RB16(pHandle->pkt->data) & 0xfff0) != 0xfff0)){
			pHandle->pkt->stream_index = pHandle->ast->index;
			av_write_frame(pHandle->oc,pHandle->pkt);
			pHandle->pkt->stream_index = pHandle->audio_stream;
		}else{
			*pSize = 0;
			memcpy(pBuffer + (*pSize),pHandle->pkt->data,pHandle->pkt->size);
			*pSize += pHandle->pkt->size;		
			//err = write(pHandle->afd, pHandle->pkt->data, pHandle->pkt->size);
		}
		*pPts = pHandle->pkt->pts*1000/pHandle->a_time_base;
	}
	av_free_packet(pHandle->pkt);

	return 0;

}

static void init_handle(koala_handle *pHandle){
	memset(pHandle,0,sizeof(koala_handle));
	pHandle->audio_stream = -1;
	pHandle->video_stream = -1;
//	pHandle->first_pts = -1;
}
koala_handle * koala_get_demux_handle(){
	koala_handle *pHandle = av_malloc(sizeof(koala_handle));
	init_handle(pHandle);
	return pHandle;

}
#if 0
int koala_demux_open(koala_handle * pHandle){
	int err;
	err = init_open(pHandle);
	if (err < 0){
		printf("%s:%d\n",__FILE__,__LINE__);
		return 0;
	}
	select_default_stream(pHandle);
	if (pHandle->video_stream >= 0 )
		open_video(pHandle);
	if (pHandle->audio_stream >= 0)
		open_audio(pHandle);

}
#endif
static int read_data(void *opaque, uint8_t *buf, int buf_size){
	int ret;
	int fd = *(int *)opaque;
	ret = read(fd,buf,buf_size);
	return ret;
}

static int64_t seek(void *opaque, int64_t offset, int whence){
	int ret;
	int fd = *(int *)opaque;
	ret = lseek(fd,offset,whence);
	return ret;
}

int main(int argc, char **argv)
{
	
	int err;
	int size;
	uint8_t *pkt_buf = malloc(MAX_PKT_SIZE);
	koala_handle *pHandle;
	int stream_index;
	int64_t pts;
	int ifd;
	if (argc <2){
		printf("%s filename\n",argv[0]);
		return 1;
	}
	ifd = open(argv[1], O_RDONLY);
	if (ifd < 0){
		printf("%s:%d\n",__FILE__,__LINE__);
		return 0;
	}
	pHandle = koala_get_demux_handle();

	regist_input_file_func(pHandle,&ifd,read_data,seek);

	err = init_open(pHandle);
	if (err < 0){
		printf("%s:%d\n",__FILE__,__LINE__);
		return 0;
	}

	// TODO: get stream info and seclect v &a
	
	select_default_stream(pHandle);

	if (pHandle->video_stream >= 0 )
		open_video(pHandle);

	if (pHandle->audio_stream >= 0)
		open_audio(pHandle);

	while(1){
		size = MAX_PKT_SIZE;
		err = demux_read_packet(pHandle,pkt_buf,&size,&stream_index,&pts);
		if (err < 0){
			// TODO: flush
			break;
		}
		if (stream_index == pHandle->video_stream)
			printf("V size is %d,pts is %lld\n",size,pts);
		else if (stream_index == pHandle->audio_stream)
			printf("A size is %d,pts is %lld\n",size,pts);
	}	
	close_demux(pHandle);
	if (ifd)
		close(ifd);
	return 0;
}

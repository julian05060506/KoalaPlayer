
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>
#include "koala_demuxer.h"

#define  MAX_PKT_SIZE   1024*1024


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
	int NbAudio,NbVideo;
	int v_index, a_index;
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
	get_nb_stream(pHandle,&NbAudio,&NbVideo);
	if (NbAudio > 0)
		a_index = open_audio(pHandle,0);
	if (NbVideo > 0)
		v_index = open_video(pHandle,0);
	while(1){
		size = MAX_PKT_SIZE;
		err = demux_read_packet(pHandle,pkt_buf,&size,&stream_index,&pts);
		if (err < 0){
			// TODO: flush ?
			break;
		}
		if (stream_index == v_index)
#if (__WORDSIZE == 64)
			printf("V size is %d,pts is %ld\n",size,pts);
#else
			printf("V size is %d,pts is %lld\n",size,pts);
#endif
		else if (stream_index == a_index)
#if (__WORDSIZE == 64)
			printf("A size is %d,pts is %ld\n",size,pts);
#else
			printf("A size is %d,pts is %lld\n",size,pts);
#endif
		else
			printf("Other\n");
	}
	close_demux(pHandle);
	if (ifd)
		close(ifd);
	free(pkt_buf);
	return 0;
}


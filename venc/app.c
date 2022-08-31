#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "sample_comm.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <netinet/if_ether.h>
#include <net/if.h>

#include <linux/if_ether.h>
#include <linux/sockios.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern int udpfd;
struct sockaddr_in g_server;

void init_udp_sender(void)
{
	udpfd = socket(AF_INET, SOCK_DGRAM, 0); // UDP

	g_server.sin_family = AF_INET;
	g_server.sin_port = htons(54321);
	int rtn = inet_pton(AF_INET, "192.168.31.211", (struct in_addr *)&g_server.sin_addr.s_addr);
	// int rtn = inet_pton(AF_INET, "192.168.10.2", (struct in_addr *)&g_server.sin_addr.s_addr);
	printf("udp up\n");
	sendto(udpfd, "abcde", 5, 0, (struct sockaddr *)&g_server, sizeof(g_server));
}

int main(int argc, char *argv[])
{
	static pthread_t udpPid;
	HI_S32 s32Ret;
	MPP_VERSION_S mppVersion;

	HI_MPI_SYS_GetVersion(&mppVersion);

	printf("MPP Ver  %s\n", mppVersion.aVersion);

	init_udp_sender();

	// pthread_create(&udpPid, 0, rtp_send_thread, NULL);
	// 初始化rtsp服务器
	init_rtsp_server();

	// 开启vi-vp-venc
	s32Ret = SAMPLE_VENC_720P_CLASSIC();

	return HI_FAILURE;

	if (HI_SUCCESS == s32Ret)
		printf("program exit normally!\n");
	else
		printf("program exit abnormally!\n");
	while (1)
	{
		usleep(1000);
	}

	return s32Ret;
}

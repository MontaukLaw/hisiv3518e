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
#include "sample_comm.h"

char g_rtp_playload[20];
int g_audio_rate = 8000;
RTSP_CLIENT g_rtspClients[MAX_RTSP_CLIENT];
static pthread_t gs_RtpPid;
int g_count = 0;
int udpfd;
struct list_head RTPbuf_head = LIST_HEAD_INIT(RTPbuf_head);

static char *dupulicate_str(char const *str)
{
    if (str == NULL)
        return NULL;
    size_t len = strlen(str) + 1;
    char *copy = malloc(len);

    return copy;
}

static init_rtsp_client(RTSP_CLIENT *client, int socket, int clientIdx, int *sessionid, struct sockaddr_in clientAddr)
{
    memset(client, 0, sizeof(RTSP_CLIENT));
    client->index = clientIdx;
    client->socket = socket;
    client->status = RTSP_CONNECTED; // RTSP_SENDING;
    *sessionid = *sessionid + 1;
    client->sessionid = *sessionid;
    strcpy(client->IP, inet_ntoa(clientAddr.sin_addr));
}

static char const *create_data_header(void)
{
    static char buf[200];
    time_t tt = time(NULL);
    strftime(buf, sizeof buf, "Date: %a, %b %d %Y %H:%M:%S GMT\r\n", gmtime(&tt));
    return buf;
}

static char *get_local_ip(int sock)
{
    struct ifreq ifreq;
    struct sockaddr_in *sin;
    char *LocalIP = malloc(20);
    strcpy(ifreq.ifr_name, "wlan");
    if (!(ioctl(sock, SIOCGIFADDR, &ifreq)))
    {
        sin = (struct sockaddr_in *)&ifreq.ifr_addr;
        sin->sin_family = AF_INET;
        strcpy(LocalIP, inet_ntoa(sin->sin_addr));
        // inet_ntop(AF_INET, &sin->sin_addr,LocalIP, 16);
    }
    printf("--------------------------------------------%s\n", LocalIP);
    return LocalIP;
}

int describe_answer(char *cseq, int sock, char *urlSuffix, char *recvbuf)
{
    if (sock != 0)
    {
        char sdpMsg[1024];
        char buf[2048];
        memset(buf, 0, 2048);
        memset(sdpMsg, 0, 1024);
        char *localip;
        localip = get_local_ip(sock);

        char *pTemp = buf;
        pTemp += sprintf(pTemp, "RTSP/1.0 200 OK\r\nCSeq: %s\r\n", cseq);
        pTemp += sprintf(pTemp, "%s", create_data_header());
        pTemp += sprintf(pTemp, "Content-Type: application/sdp\r\n");

        char *pTemp2 = sdpMsg;
        pTemp2 += sprintf(pTemp2, "v=0\r\n");
        pTemp2 += sprintf(pTemp2, "o=StreamingServer 3331435948 1116907222000 IN IP4 %s\r\n", localip);
        pTemp2 += sprintf(pTemp2, "s=H.264\r\n");
        pTemp2 += sprintf(pTemp2, "c=IN IP4 0.0.0.0\r\n");
        pTemp2 += sprintf(pTemp2, "t=0 0\r\n");
        pTemp2 += sprintf(pTemp2, "a=control:*\r\n");

        /*H264 TrackID=0 RTP_PT 96*/
        pTemp2 += sprintf(pTemp2, "m=video 0 RTP/AVP 96\r\n");
        pTemp2 += sprintf(pTemp2, "a=control:trackID=0\r\n");
        pTemp2 += sprintf(pTemp2, "a=rtpmap:96 H264/90000\r\n");
        pTemp2 += sprintf(pTemp2, "a=fmtp:96 packetization-mode=1; sprop-parameter-sets=%s\r\n", "AAABBCCC");

        /*G726*/
        pTemp2 += sprintf(pTemp2, "m=audio 0 RTP/AVP 97\r\n");
        pTemp2 += sprintf(pTemp2, "a=control:trackID=1\r\n");
        if (strcmp(g_rtp_playload, "AAC") == 0)
        {
            pTemp2 += sprintf(pTemp2, "a=rtpmap:97 MPEG4-GENERIC/%d/2\r\n", 16000);
            pTemp2 += sprintf(pTemp2, "a=fmtp:97 streamtype=5;profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3;config=1410\r\n");
        }
        else
        {
            pTemp2 += sprintf(pTemp2, "a=rtpmap:97 G726-32/%d/1\r\n", 8000);
            pTemp2 += sprintf(pTemp2, "a=fmtp:97 packetization-mode=1\r\n");
        }

        pTemp += sprintf(pTemp, "Content-length: %d\r\n", strlen(sdpMsg));
        pTemp += sprintf(pTemp, "Content-Base: rtsp://%s/%s/\r\n\r\n", localip, urlSuffix);

        // printf("mem ready\n");
        strcat(pTemp, sdpMsg);
        free(localip);
        // printf("Describe ready sent\n");
        int re = send(sock, buf, strlen(buf), 0);
        if (re <= 0)
        {
            return FALSE;
        }
        else
        {
            printf(">>>>>%s\n", buf);
        }
    }

    return TRUE;
}

int play_answer(char *cseq, int sock, int SessionId, char *urlPre, char *recvbuf)
{
    if (sock != 0)
    {
        char buf[1024];
        memset(buf, 0, 1024);
        char *pTemp = buf;
        char *localip;
        localip = get_local_ip(sock);
        pTemp += sprintf(pTemp, "RTSP/1.0 200 OK\r\nCSeq: %s\r\n%sRange: npt=0.000-\r\nSession: %d\r\nRTP-Info: url=rtsp://%s/%s;seq=0\r\n\r\n",
                         cseq, create_data_header(), SessionId, localip, urlPre);

        free(localip);

        int reg = send(sock, buf, strlen(buf), 0);
        if (reg <= 0)
        {
            return FALSE;
        }
        else
        {
            printf(">>>>>%s", buf);
            udpfd = socket(AF_INET, SOCK_DGRAM, 0); // UDP
            struct sockaddr_in server;
            server.sin_family = AF_INET;
            server.sin_port = htons(g_rtspClients[0].rtpport[0]);
            server.sin_addr.s_addr = inet_addr(g_rtspClients[0].IP);
            connect(udpfd, (struct sockaddr *)&server, sizeof(server));
            printf("udp up\n");
        }
        return TRUE;
    }
    return FALSE;
}

static void parse_transport_header(char const *buf,
                                   StreamingMode *streamingMode,
                                   char **streamingModeString,
                                   char **destinationAddressStr,
                                   u_int8_t *destinationTTL,
                                   portNumBits *clientRTPPortNum,  // if UDP
                                   portNumBits *clientRTCPPortNum, // if UDP
                                   unsigned char *rtpChannelId,    // if TCP
                                   unsigned char *rtcpChannelId    // if TCP
)
{
    // Initialize the result parameters to default values:
    *streamingMode = RTP_UDP;
    *streamingModeString = NULL;
    *destinationAddressStr = NULL;
    *destinationTTL = 255;
    *clientRTPPortNum = 0;
    *clientRTCPPortNum = 1;
    *rtpChannelId = *rtcpChannelId = 0xFF;

    portNumBits p1, p2;
    unsigned ttl, rtpCid, rtcpCid;

    // First, find "Transport:"
    while (1)
    {
        if (*buf == '\0')
            return; // not found
        if (strncasecmp(buf, "Transport: ", 11) == 0)
            break;
        ++buf;
    }

    // Then, run through each of the fields, looking for ones we handle:
    char const *fields = buf + 11;
    char *field = dupulicate_str(fields);

    while (sscanf(fields, "%[^;]", field) == 1)
    {
        if (strcmp(field, "RTP/AVP/TCP") == 0)
        {
            *streamingMode = RTP_TCP;
        }
        else if (strcmp(field, "RAW/RAW/UDP") == 0 ||
                 strcmp(field, "MP2T/H2221/UDP") == 0)
        {
            *streamingMode = RAW_UDP;
            //*streamingModeString = strDup(field);
        }
        else if (strncasecmp(field, "destination=", 12) == 0)
        {
            // delete[] destinationAddressStr;
            free(destinationAddressStr);
            // destinationAddressStr = strDup(field+12);
        }
        else if (sscanf(field, "ttl%u", &ttl) == 1)
        {
            destinationTTL = (u_int8_t)ttl;
        }
        else if (sscanf(field, "client_port=%hu-%hu", &p1, &p2) == 2)
        {
            *clientRTPPortNum = p1;
            *clientRTCPPortNum = p2;
        }
        else if (sscanf(field, "client_port=%hu", &p1) == 1)
        {
            *clientRTPPortNum = p1;
            *clientRTCPPortNum = streamingMode == RAW_UDP ? 0 : p1 + 1;
        }
        else if (sscanf(field, "interleaved=%u-%u", &rtpCid, &rtcpCid) == 2)
        {
            *rtpChannelId = (unsigned char)rtpCid;
            *rtcpChannelId = (unsigned char)rtcpCid;
        }

        fields += strlen(field);
        while (*fields == ';')
            ++fields; // skip over separating ';' chars
        if (*fields == '\0' || *fields == '\r' || *fields == '\n')
            break;
    }
    free(field);
}

int setup_answer(char *cseq, int sock, int SessionId, char *urlSuffix, char *recvbuf, int *rtpport, int *rtcpport)
{
    if (sock != 0)
    {
        char buf[1024];
        memset(buf, 0, 1024);

        StreamingMode streamingMode;
        char *streamingModeString; // set when RAW_UDP streaming is specified
        char *clientsDestinationAddressStr;
        u_int8_t clientsDestinationTTL;
        portNumBits clientRTPPortNum, clientRTCPPortNum;
        unsigned char rtpChannelId, rtcpChannelId;
        parse_transport_header(recvbuf, &streamingMode, &streamingModeString,
                               &clientsDestinationAddressStr, &clientsDestinationTTL,
                               &clientRTPPortNum, &clientRTCPPortNum,
                               &rtpChannelId, &rtcpChannelId);

        // Port clientRTPPort(clientRTPPortNum);
        // Port clientRTCPPort(clientRTCPPortNum);
        *rtpport = clientRTPPortNum;
        *rtcpport = clientRTCPPortNum;

        char *pTemp = buf;
        char *localip;
        localip = get_local_ip(sock);
        pTemp += sprintf(pTemp, "RTSP/1.0 200 OK\r\nCSeq: %s\r\n%sTransport: RTP/AVP;unicast;destination=%s;client_port=%d-%d;server_port=%d-%d\r\nSession: %d\r\n\r\n",
                         cseq, create_data_header(), localip,
                         ntohs(htons(clientRTPPortNum)),
                         ntohs(htons(clientRTCPPortNum)),
                         ntohs(2000),
                         ntohs(2001),
                         SessionId);

        free(localip);
        int reg = send(sock, buf, strlen(buf), 0);
        if (reg <= 0)
        {
            return FALSE;
        }
        else
        {
            printf(">>>>>%s", buf);
        }
        return TRUE;
    }
    return FALSE;
}

int option_answer(char *cseq, int sock)
{
    if (sock != 0)
    {
        char buf[1024];
        memset(buf, 0, 1024);
        char *pTemp = buf;
        pTemp += sprintf(pTemp, "RTSP/1.0 200 OK\r\nCSeq: %s\r\n%sPublic: %s\r\n\r\n",
                         cseq, create_data_header(), "OPTIONS,DESCRIBE,SETUP,PLAY,PAUSE,TEARDOWN");

        int reg = send(sock, buf, strlen(buf), 0);
        if (reg <= 0)
        {
            return FALSE;
        }
        else
        {
            printf(">>>>>%s\n", buf);
        }
        return TRUE;
    }
    return FALSE;
}

int pause_answer(char *cseq, int sock, char *recvbuf)
{
    if (sock != 0)
    {
        char buf[1024];
        memset(buf, 0, 1024);
        char *pTemp = buf;
        pTemp += sprintf(pTemp, "RTSP/1.0 200 OK\r\nCSeq: %s\r\n%s\r\n\r\n",
                         cseq, create_data_header());

        int reg = send(sock, buf, strlen(buf), 0);
        if (reg <= 0)
        {
            return FALSE;
        }
        else
        {
            printf(">>>>>%s", buf);
        }
        return TRUE;
    }
    return FALSE;
}

int teardown_answer(char *cseq, int sock, int SessionId, char *recvbuf)
{
    if (sock != 0)
    {
        char buf[1024];
        memset(buf, 0, 1024);
        char *pTemp = buf;
        pTemp += sprintf(pTemp, "RTSP/1.0 200 OK\r\nCSeq: %s\r\n%sSession: %d\r\n\r\n",
                         cseq, create_data_header(), SessionId);

        int reg = send(sock, buf, strlen(buf), 0);
        if (reg <= 0)
        {
            return FALSE;
        }
        else
        {
            printf(">>>>>%s", buf);
            close(udpfd);
        }
        return TRUE;
    }
    return FALSE;
}

void *rtsp_client_msg_handler(void *pParam)
{
    pthread_detach(pthread_self());
    int nRes;
    char pRecvBuf[RTSP_RECV_SIZE];
    RTSP_CLIENT *pClient = (RTSP_CLIENT *)pParam;
    memset(pRecvBuf, 0, sizeof(pRecvBuf));
    printf("RTSP:-----Create Client %s\n", pClient->IP);
    while (pClient->status != RTSP_IDLE)
    {
        nRes = recv(pClient->socket, pRecvBuf, RTSP_RECV_SIZE, 0);
        // printf("-------------------%d\n",nRes);
        if (nRes < 1)
        {
            // usleep(1000);
            printf("RTSP:Recv Error--- %d\n", nRes);
            g_rtspClients[pClient->index].status = RTSP_IDLE;
            g_rtspClients[pClient->index].seqnum = 0;
            g_rtspClients[pClient->index].tsvid = 0;
            g_rtspClients[pClient->index].tsaud = 0;
            close(pClient->socket);
            break;
        }

        char cmdName[PARAM_STRING_MAX];
        char urlPreSuffix[PARAM_STRING_MAX];
        char urlSuffix[PARAM_STRING_MAX];
        char cseq[PARAM_STRING_MAX];

        parse_request_string(pRecvBuf, nRes, cmdName, sizeof(cmdName), urlPreSuffix, sizeof(urlPreSuffix),
                             urlSuffix, sizeof(urlSuffix), cseq, sizeof(cseq));

        char *p = pRecvBuf;

        printf("<<<<<%s\n", p);

        // printf("\--------------------------\n");
        // printf("%s %s\n",urlPreSuffix,urlSuffix);

        if (strstr(cmdName, "OPTIONS"))
        {
            option_answer(cseq, pClient->socket);
        }
        else if (strstr(cmdName, "DESCRIBE"))
        {
            describe_answer(cseq, pClient->socket, urlSuffix, p);
            // printf("-----------------------------DescribeAnswer %s %s\n",
            //	urlPreSuffix,urlSuffix);
        }
        else if (strstr(cmdName, "SETUP"))
        {
            int rtpport, rtcpport;
            int trackID = 0;
            setup_answer(cseq, pClient->socket, pClient->sessionid, urlPreSuffix, p, &rtpport, &rtcpport);

            sscanf(urlSuffix, "trackID=%u", &trackID);
            // printf("----------------------------------------------TrackId %d\n",trackID);
            if (trackID < 0 || trackID >= 2)
                trackID = 0;
            g_rtspClients[pClient->index].rtpport[trackID] = rtpport;
            g_rtspClients[pClient->index].rtcpport = rtcpport;
            g_rtspClients[pClient->index].reqchn = atoi(urlPreSuffix);
            if (strlen(urlPreSuffix) < 100)
                strcpy(g_rtspClients[pClient->index].urlPre, urlPreSuffix);
            // printf("-----------------------------SetupAnswer %s-%d-%d\n",
            //	urlPreSuffix,g_rtspClients[pClient->index].reqchn,rtpport);
        }
        else if (strstr(cmdName, "PLAY"))
        {
            play_answer(cseq, pClient->socket, pClient->sessionid, g_rtspClients[pClient->index].urlPre, p);
            g_rtspClients[pClient->index].status = RTSP_SENDING;
            printf("Start Play\n", pClient->index);
            // printf("-----------------------------PlayAnswer %d %d\n",pClient->index);
            // usleep(100);
        }
        else if (strstr(cmdName, "PAUSE"))
        {
            pause_answer(cseq, pClient->socket, p);
        }
        else if (strstr(cmdName, "TEARDOWN"))
        {
            teardown_answer(cseq, pClient->socket, pClient->sessionid, p);
            g_rtspClients[pClient->index].status = RTSP_IDLE;
            g_rtspClients[pClient->index].seqnum = 0;
            g_rtspClients[pClient->index].tsvid = 0;
            g_rtspClients[pClient->index].tsaud = 0;
            close(pClient->socket);
        }
        // if(exitok){ exitok++;return NULL; }
    }

    printf("RTSP:-----Exit Client %s\n", pClient->IP);
    return NULL;
}

void *rtsp_server_connector(void *pParam)
{
    int s32Socket;
    struct sockaddr_in servaddr;
    int s32CSocket;
    int s32Rtn;
    int s32Socket_opt_value = 1;
    int nAddrLen;
    struct sockaddr_in addrAccept;
    int bResult;

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(RTSP_SERVER_PORT);

    s32Socket = socket(AF_INET, SOCK_STREAM, 0);

    if (setsockopt(s32Socket, SOL_SOCKET, SO_REUSEADDR, &s32Socket_opt_value, sizeof(int)) == -1)
    {
        return (void *)(-1);
    }
    s32Rtn = bind(s32Socket, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in));
    if (s32Rtn < 0)
    {
        return (void *)(-2);
    }

    s32Rtn = listen(s32Socket, 50);
    if (s32Rtn < 0)
    {
        return (void *)(-2);
    }

    nAddrLen = sizeof(struct sockaddr_in);
    int nSessionId = 1000;
    while ((s32CSocket = accept(s32Socket, (struct sockaddr *)&addrAccept, &nAddrLen)) >= 0)
    {
        printf("<<<<RTSP Client %s Connected...\n", inet_ntoa(addrAccept.sin_addr));

        int nMaxBuf = 10 * 1024;
        if (setsockopt(s32CSocket, SOL_SOCKET, SO_SNDBUF, (char *)&nMaxBuf, sizeof(nMaxBuf)) == -1)
            printf("RTSP:!!!!!! Enalarge socket sending buffer error !!!!!!\n");
        int i;
        int bAdd = FALSE;
        for (i = 0; i < MAX_RTSP_CLIENT; i++)
        {
            if (g_rtspClients[i].status == RTSP_IDLE)
            {
                init_rtsp_client(&g_rtspClients[i], s32CSocket, i, &nSessionId, addrAccept);

#if 0
                memset(&g_rtspClients[i], 0, sizeof(RTSP_CLIENT));
                g_rtspClients[i].index = i;
                g_rtspClients[i].socket = s32CSocket;
                g_rtspClients[i].status = RTSP_CONNECTED; // RTSP_SENDING;
                g_rtspClients[i].sessionid = nSessionId++;
                strcpy(g_rtspClients[i].IP, inet_ntoa(addrAccept.sin_addr));
#endif

                pthread_t threadIdlsn = 0;
                struct sched_param sched;
                sched.sched_priority = 1;
                // to return ACKecho
                pthread_create(&threadIdlsn, NULL, rtsp_client_msg_handler, &g_rtspClients[i]);
                // pthread_setschedparam(threadIdlsn,SCHED_RR,&sched);

                bAdd = TRUE;
                break;
            }
        }
        if (bAdd == FALSE)
        {
            init_rtsp_client(&g_rtspClients[i], s32CSocket, i, &nSessionId, addrAccept);

#if 0
            memset(&g_rtspClients[0], 0, sizeof(RTSP_CLIENT));
            g_rtspClients[0].index = 0;
            g_rtspClients[0].socket = s32CSocket;
            g_rtspClients[0].status = RTSP_CONNECTED; // RTSP_SENDING;
            g_rtspClients[0].sessionid = nSessionId++;
            strcpy(g_rtspClients[0].IP, inet_ntoa(addrAccept.sin_addr));
#endif
            pthread_t threadIdlsn = 0;
            struct sched_param sched;
            sched.sched_priority = 1;
            // to return ACKecho
            pthread_create(&threadIdlsn, NULL, rtsp_client_msg_handler, &g_rtspClients[0]);
            // pthread_setschedparam(threadIdlsn,SCHED_RR,&sched);
            bAdd = TRUE;
        }
        // if(exitok){ exitok++;return NULL; }
    }
    if (s32CSocket < 0)
    {
        // HI_OUT_Printf(0, "RTSP listening on port %d,accept err, %d\n", RTSP_SERVER_PORT, s32CSocket);
    }

    printf("----- INIT_RTSP_Listen() Exit !! \n");

    return NULL;
}

extern struct sockaddr_in g_server;

void send_udp(char *buf, int bufSize)
{

    // server.sin_family = AF_INET;
    // server.sin_port = htons(54321);
    // int rtn = inet_pton(AF_INET, "192.168.10.2", (struct in_addr *)&server.sin_addr.s_addr);
    // sendto(udpfd, "abcde", 5, 0, (struct sockaddr *)&server, sizeof(server));
    sendto(udpfd, buf, bufSize, 0, (struct sockaddr *)&g_server, sizeof(g_server));
}

HI_S32 send_udp_package(char *buffer, int buflen)
{
    HI_S32 i;
    int is = 0;
    int nChanNum = 0;

    for (is = 0; is < MAX_RTSP_CLIENT; is++)
    {
        if (g_rtspClients[is].status != RTSP_SENDING)
        {
            continue;
        }
        int heart = g_rtspClients[is].seqnum % 10000;

        char *nalu_payload;
        int nAvFrmLen = 0;
        int nIsIFrm = 0;
        int nNaluType = 0;
        char sendbuf[500 * 1024 + 32];

        nAvFrmLen = buflen;
        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_port = htons(g_rtspClients[is].rtpport[0]);
        server.sin_addr.s_addr = inet_addr(g_rtspClients[is].IP);
        // server.sin_port = 54321;
        // int rtn = inet_pton(AF_INET, "192.168.10.2", (struct in_addr *)&server.sin_addr);
        // printf("rtn = %d\n", rtn);
        // inet_addr("192.168.10.2");
        int bytes = 0;
        unsigned int timestamp_increse = 0;

        timestamp_increse = (unsigned int)(90000.0 / 25);

        rtp_hdr = (RTP_FIXED_HEADER *)&sendbuf[0];

        rtp_hdr->payload = RTP_H264;
        rtp_hdr->version = 2;
        rtp_hdr->marker = 0;
        rtp_hdr->ssrc = htonl(10);

        if (nAvFrmLen <= nalu_sent_len)
        {
            rtp_hdr->marker = 1;
            rtp_hdr->seq_no = htons(g_rtspClients[is].seqnum++);
            nalu_hdr = (NALU_HEADER *)&sendbuf[12];
            nalu_hdr->F = 0;
            nalu_hdr->NRI = nIsIFrm;
            nalu_hdr->TYPE = nNaluType;
            nalu_payload = &sendbuf[13];
            memcpy(nalu_payload, buffer, nAvFrmLen);
            g_rtspClients[is].tsvid = g_rtspClients[is].tsvid + timestamp_increse;
            rtp_hdr->timestamp = htonl(g_rtspClients[is].tsvid);
            bytes = nAvFrmLen + 13;
            sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server, sizeof(server));
        }
        else if (nAvFrmLen > nalu_sent_len)
        {
            int k = 0, l = 0;
            k = nAvFrmLen / nalu_sent_len;
            l = nAvFrmLen % nalu_sent_len;
            int t = 0;

            g_rtspClients[is].tsvid = g_rtspClients[is].tsvid + timestamp_increse;
            rtp_hdr->timestamp = htonl(g_rtspClients[is].tsvid);
            while (t <= k)
            {
                rtp_hdr->seq_no = htons(g_rtspClients[is].seqnum++);
                if (t == 0)
                {
                    rtp_hdr->marker = 0;
                    fu_ind = (FU_INDICATOR *)&sendbuf[12];
                    fu_ind->F = 0;
                    fu_ind->NRI = nIsIFrm;
                    fu_ind->TYPE = 28;

                    fu_hdr = (FU_HEADER *)&sendbuf[13];
                    fu_hdr->E = 0;
                    fu_hdr->R = 0;
                    fu_hdr->S = 1;
                    fu_hdr->TYPE = nNaluType;

                    nalu_payload = &sendbuf[14];
                    memcpy(nalu_payload, buffer, nalu_sent_len);

                    bytes = nalu_sent_len + 14;
                    // send_udp(sendbuf, bytes);
                    sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server, sizeof(server));
                    t++;
                }
                else if (k == t)
                {
                    rtp_hdr->marker = 1;
                    fu_ind = (FU_INDICATOR *)&sendbuf[12];
                    fu_ind->F = 0;
                    fu_ind->NRI = nIsIFrm;
                    fu_ind->TYPE = 28;

                    fu_hdr = (FU_HEADER *)&sendbuf[13];
                    fu_hdr->R = 0;
                    fu_hdr->S = 0;
                    fu_hdr->TYPE = nNaluType;
                    fu_hdr->E = 1;
                    nalu_payload = &sendbuf[14];
                    memcpy(nalu_payload, buffer + t * nalu_sent_len, l);
                    bytes = l + 14;
                    // send_udp(sendbuf, bytes);
                    sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server, sizeof(server));
                    t++;
                }
                else if (t < k && t != 0)
                {

                    rtp_hdr->marker = 0;

                    fu_ind = (FU_INDICATOR *)&sendbuf[12];
                    fu_ind->F = 0;
                    fu_ind->NRI = nIsIFrm;
                    fu_ind->TYPE = 28;
                    fu_hdr = (FU_HEADER *)&sendbuf[13];
                    // fu_hdr->E=0;
                    fu_hdr->R = 0;
                    fu_hdr->S = 0;
                    fu_hdr->E = 0;
                    fu_hdr->TYPE = nNaluType;
                    nalu_payload = &sendbuf[14];
                    memcpy(nalu_payload, buffer + t * nalu_sent_len, nalu_sent_len);
                    bytes = nalu_sent_len + 14;
                    // send_udp(sendbuf, bytes);
                    sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server, sizeof(server));
                    t++;
                }
            }
        }
    }

    //------------------------------------------------------------
}
#define SEND_UDP sendto(udpfd, rawUdpSendBuf, bytes, 0, (struct sockaddr *)&g_server, sizeof(g_server));
#define RAW_UDP_PACK_LEN 1400
uint8_t rawUdpSendBuf[RAW_UDP_PACK_LEN];

HI_S32 send_h264_raw_udp_package(char *buffer, int buflen)
{
    static int frameCounter = 0;
    // sendto(udpfd, buffer, buflen, 0, (struct sockaddr *)&g_server, sizeof(g_server));
    int udpPacketLen = buflen;
    // printf("buflen = %d\n", buflen);
    if (buflen == 19)
    {
        // printf("frame number in 1 sec: %d\n", frameCounter);
        frameCounter = 0;
    }
    frameCounter++;

    int bytes = 0;

    if (udpPacketLen < RAW_UDP_PACK_LEN)
    {
        sendto(udpfd, buffer, buflen, 0, (struct sockaddr *)&g_server, sizeof(g_server));
    }
    else if (udpPacketLen > RAW_UDP_PACK_LEN)
    {
        int pktTotalNumber = 0, tailBytes = 0;
        pktTotalNumber = udpPacketLen / nalu_sent_len;
        tailBytes = udpPacketLen % nalu_sent_len;
        int pktSent = 0;
        while (pktSent <= pktTotalNumber)
        {
            if (pktSent == 0) // first packet
            {
                memcpy(rawUdpSendBuf, buffer, RAW_UDP_PACK_LEN);
                bytes = RAW_UDP_PACK_LEN;
                SEND_UDP
                pktSent++;
            }
            else if (pktSent == pktTotalNumber) // last packet
            {
                memcpy(rawUdpSendBuf, buffer + pktSent * RAW_UDP_PACK_LEN, tailBytes);
                bytes = tailBytes;
                SEND_UDP
                pktSent++;
            }
            else if (pktSent < pktTotalNumber && pktSent != 0) // all packet
            {
                memcpy(rawUdpSendBuf, buffer + pktSent * RAW_UDP_PACK_LEN, RAW_UDP_PACK_LEN);
                bytes = RAW_UDP_PACK_LEN;
                SEND_UDP
                pktSent++;
            }
        }
    }
}

HI_VOID *rtp_send_thread(HI_VOID *p)
{
    static uint8_t counter = 0;
    while (1)
    {
        if (!list_empty(&RTPbuf_head))
        {
            counter++;

            // 取出头item进行发送
            RTPbuf_s *p = get_first_item(&RTPbuf_head, RTPbuf_s, list);

            send_h264_raw_udp_package(p->buf, p->len);
            // send_udp_package(p->buf, p->len);
            list_del(&(p->list));
            free(p->buf);
            free(p);
            p = NULL;
            g_count--;
            // printf("g_count = %d\n",g_count);

            if (counter > 30)
            {
                // exit(0);
            }
        }
        usleep(5000);
    }
}

void init_rtsp_server(void)
{
    int i;
    pthread_t threadId = 0;

    memset(&g_rtp_playload, 0, sizeof(g_rtp_playload));
    strcpy(&g_rtp_playload, "G726-32");
    g_audio_rate = 8000;
    memset(&g_rtspClients, 0, sizeof(RTSP_CLIENT) * MAX_RTSP_CLIENT);

    // pthread_create(&g_SendDataThreadId, NULL, SendDataThread, NULL);

    struct sched_param thdsched;
    thdsched.sched_priority = 2;
    // to listen visiting
    pthread_create(&threadId, NULL, rtsp_server_connector, NULL);
    // pthread_setschedparam(threadId,SCHED_RR,&thdsched);
    printf("RTSP:-----Init Rtsp server\n");

    pthread_create(&gs_RtpPid, 0, rtp_send_thread, NULL);

    // exitok++;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include "sample_comm.h"

VIDEO_NORM_E gs_enNorm = VIDEO_ENCODING_MODE_PAL;
HI_U32 g_u32BlkCnt = 4;
extern int g_count;
extern struct list_head RTPbuf_head;

void add_pkt_list(VENC_STREAM_S *pstStream)
{
    HI_S32 packIdx, packLength = 0;

    for (packIdx = 0; packIdx < pstStream->u32PackCount; packIdx++)
    {
        RTPbuf_s *p = (RTPbuf_s *)malloc(sizeof(RTPbuf_s));
        INIT_LIST_HEAD(&(p->list));

        packLength = pstStream->pstPack[packIdx].u32Len - pstStream->pstPack[packIdx].u32Offset;
        p->buf = (char *)malloc(packLength);
        p->len = packLength;
        memcpy(p->buf, pstStream->pstPack[packIdx].pu8Addr + pstStream->pstPack[packIdx].u32Offset,
               packLength);

        list_add_tail(&(p->list), &RTPbuf_head);
        g_count++;
        // printf("count = %d\n",g_count);
    }
}

HI_S32 add_raw_pack_to_udp_list(VENC_STREAM_S *pstStream)
{
    HI_S32 packIdx;
    for (packIdx = 0; packIdx < pstStream->u32PackCount; packIdx++)
    {

        // fwrite(pstStream->pstPack[packIdx].pu8Addr + pstStream->pstPack[packIdx].u32Offset,
        // pstStream->pstPack[packIdx].u32Len - pstStream->pstPack[packIdx].u32Offset, 1, fpH264File);

        // fflush(fpH264File);
    }
}

HI_S32 add_stream_pack_to_list(VENC_STREAM_S *pstStream)
{

    HI_S32 packIdx, clientIdx, packLength = 0;

    add_pkt_list(pstStream);

    for (clientIdx = 0; clientIdx < MAX_RTSP_CLIENT; clientIdx++) // have at least a connect
    {
        if (g_rtspClients[clientIdx].status == RTSP_SENDING)
        {
            // add_pkt_list(pstStream);
        }
    }

    return HI_SUCCESS;
}

/******************************************************************************
 * function :  H.264@1080p@30fps+H.264@VGA@30fps
 ******************************************************************************/
HI_S32 SAMPLE_VENC_720P_CLASSIC(HI_VOID)
{
    PAYLOAD_TYPE_E enPayLoad[3] = {PT_H264, PT_H264, PT_H264};
    // PIC_SIZE_E enSize[3] = {PIC_HD720, PIC_VGA, PIC_QVGA};
    PIC_SIZE_E enSize[3] = {PIC_HD720, PIC_VGA, PIC_QVGA}; // PIC_QVGA

    HI_U32 u32Profile = 0;

    VB_CONF_S stVbConf;
    SAMPLE_VI_CONFIG_S stViConfig = {0};

    VPSS_GRP VpssGrp;
    VPSS_CHN VpssChn;
    VPSS_GRP_ATTR_S stVpssGrpAttr;
    VPSS_CHN_ATTR_S stVpssChnAttr;
    VPSS_EXT_CHN_ATTR_S stVpssExtChnAttr;
    VPSS_CHN_MODE_S stVpssChnMode;
    VPSS_CHN_MODE_S stVpssMode;

    VENC_CHN VencChn;
    SAMPLE_RC_E enRcMode = SAMPLE_RC_FIXQP;

    HI_S32 s32ChnNum = 1;

    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    SIZE_S stSize;

    /******************************************
     step  1: init sys variable
    ******************************************/
    memset(&stVbConf, 0, sizeof(VB_CONF_S));
    printf("s32ChnNum = %d \n", s32ChnNum);

    stVbConf.u32MaxPoolCnt = 128;

    /*video buffer*/
    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm, enSize[0],
                                                  SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);

    printf("u32BlkSize = %d\n", u32BlkSize);

    stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt = g_u32BlkCnt;

    /******************************************
     step 2: mpp system init.
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        goto END_VENC_720P_CLASSIC_0;
    }

    /******************************************
     step 3: start vi dev & chn to capture
    ******************************************/
    stViConfig.enViMode = APTINA_AR0130_DC_720P_30FPS; // SENSOR_TYPE;
    stViConfig.enRotate = ROTATE_NONE;
    stViConfig.enNorm = VIDEO_ENCODING_MODE_AUTO;
    stViConfig.enViChnSet = VI_CHN_SET_NORMAL; // VI_CHN_SET_MIRROR; // VI_CHN_SET_NORMAL;
    stViConfig.enWDRMode = WDR_MODE_NONE;

    s32Ret = SAMPLE_COMM_VI_StartVi(&stViConfig);

    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed! \n");
        goto END_VENC_720P_CLASSIC_1;
    }

    /******************************************
     step 4: start vpss and vi bind vpss
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, enSize[0], &stSize);
    printf("stSize: %d x %d \n", stSize.u32Height, stSize.u32Width);

    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_VENC_720P_CLASSIC_1;
    }

    VpssGrp = 0;
    stVpssGrpAttr.u32MaxW = stSize.u32Width;
    stVpssGrpAttr.u32MaxH = stSize.u32Height;
    stVpssGrpAttr.bIeEn = HI_FALSE;
    stVpssGrpAttr.bNrEn = HI_TRUE;
    stVpssGrpAttr.bHistEn = HI_FALSE;
    stVpssGrpAttr.bDciEn = HI_FALSE;
    stVpssGrpAttr.enDieMode = VPSS_DIE_MODE_NODIE;
    stVpssGrpAttr.enPixFmt = PIXEL_FORMAT_YUV_SEMIPLANAR_420;

    s32Ret = SAMPLE_COMM_VPSS_StartGroup(VpssGrp, &stVpssGrpAttr);

    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Vpss failed!\n");
        goto END_VENC_720P_CLASSIC_2;
    }

    s32Ret = SAMPLE_COMM_VI_BindVpss(stViConfig.enViMode);

    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Vi bind Vpss failed!\n");
        goto END_VENC_720P_CLASSIC_3;
    }

    VpssChn = 0;
    stVpssChnMode.enChnMode = VPSS_CHN_MODE_USER;
    stVpssChnMode.bDouble = HI_FALSE;
    stVpssChnMode.enPixelFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    // stVpssChnMode.u32Width = 64;
    // stVpssChnMode.u32Height = 64;
    stVpssChnMode.u32Width = stSize.u32Width;
    stVpssChnMode.u32Height = stSize.u32Height;

    SAMPLE_PRT("stVpssChnMode: u32Width: %d x u32Height: %d\n", stSize.u32Width, stSize.u32Height);
    stVpssChnMode.enCompressMode = COMPRESS_MODE_SEG;

    memset(&stVpssChnAttr, 0, sizeof(stVpssChnAttr));
    stVpssChnAttr.s32SrcFrameRate = -1;
    stVpssChnAttr.s32DstFrameRate = -1;
    enRcMode = SAMPLE_RC_FIXQP; // SAMPLE_RC_VBR;
    s32Ret = SAMPLE_COMM_VPSS_EnableChn(VpssGrp, VpssChn, &stVpssChnAttr, &stVpssChnMode, HI_NULL);

    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("enable chn %d failed!\n", VpssChn);
        goto END_VENC_720P_CLASSIC_4;
    }

    VpssChn = 4;
    memset(&stVpssExtChnAttr, 0, sizeof(stVpssExtChnAttr));

    stVpssExtChnAttr.s32SrcFrameRate = -1;
    stVpssExtChnAttr.s32DstFrameRate = -1;
    stVpssExtChnAttr.u32Height = 360;
    stVpssExtChnAttr.u32Width = 640;
    // stVpssChnMode.u32Width = 640;
    // stVpssChnMode.u32Height = 360;
    stVpssExtChnAttr.enPixelFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    stVpssExtChnAttr.enCompressMode = COMPRESS_MODE_SEG;
    stVpssExtChnAttr.s32BindChn = 0;

    s32Ret = SAMPLE_COMM_VPSS_EnableChn(VpssGrp, VpssChn, HI_NULL, HI_NULL, &stVpssExtChnAttr);
    // s32Ret = SAMPLE_COMM_VPSS_EnableChn(VpssGrp, VpssChn, &stVpssChnAttr, &stVpssChnMode, HI_NULL);

    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Enable vpss chn %d failed!\n", VpssChn);
        goto END_VENC_720P_CLASSIC_4;
    }

    /******************************************
    step 5: start stream venc
    ******************************************/
    /*** enSize[0] **/

    VpssGrp = 0;
    VencChn = 0;
    SAMPLE_PRT("using vencChn \n");
    s32Ret = SAMPLE_COMM_VENC_Start(VencChn, enPayLoad[0], gs_enNorm, enSize[0], enRcMode, u32Profile);

    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Venc failed!\n");
        goto END_VENC_720P_CLASSIC_5;
    }

    s32Ret = SAMPLE_COMM_VENC_BindVpss(VencChn, VpssGrp, VpssChn);

    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Venc failed!\n");
        goto END_VENC_720P_CLASSIC_5;
    }

    /******************************************
     step 6: stream venc process -- get stream, then save it to file.
    ******************************************/
    s32Ret = SAMPLE_COMM_VENC_StartGetStream(s32ChnNum);

    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Venc failed!\n");
        goto END_VENC_720P_CLASSIC_5;
    }

    printf("please press twice ENTER to exit this sample\n");

    getchar();
    getchar();

    /******************************************
     step 7: exit process
    ******************************************/
    SAMPLE_COMM_VENC_StopGetStream();

END_VENC_720P_CLASSIC_5:
    VpssGrp = 0;
    VpssChn = 0;
    VencChn = 0;
    SAMPLE_COMM_VENC_UnBindVpss(VencChn, VpssGrp, VpssChn);
    SAMPLE_COMM_VENC_Stop(VencChn);
    SAMPLE_COMM_VI_UnBindVpss(stViConfig.enViMode);
END_VENC_720P_CLASSIC_4: // vpss stop
    VpssGrp = 0;
    VpssChn = 0;
    SAMPLE_COMM_VPSS_DisableChn(VpssGrp, VpssChn);
END_VENC_720P_CLASSIC_3: // vpss stop
    SAMPLE_COMM_VI_UnBindVpss(stViConfig.enViMode);
END_VENC_720P_CLASSIC_2: // vpss stop
    SAMPLE_COMM_VPSS_StopGroup(VpssGrp);
END_VENC_720P_CLASSIC_1: // vi stop
    SAMPLE_COMM_VI_StopVi(&stViConfig);
END_VENC_720P_CLASSIC_0: // system exit
    SAMPLE_COMM_SYS_Exit();

    return s32Ret;
}
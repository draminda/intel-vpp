


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include "vpl/mfxjpeg.h"
#include "vpl/mfxvideo.h"
#include "vpl/mfxdispatcher.h"
#include "va/va.h"
#include "va/va_drm.h"
#include "mfxvideo.h"

using namespace std;

#define MAJOR_API_VERSION_REQUIRED 2
#define MINOR_API_VERSION_REQUIRED 2
#define VPLVERSION(major, minor) (major << 16 | minor)
#define WAIT_100_MILLISECONDS 100
#define OUTPUT_FILE                "out.mp4"
#define OUTPUT_WIDTH               640
#define OUTPUT_HEIGHT              480
#define MAX_PATH              260
#define MAX_WIDTH             3840
#define MAX_HEIGHT            2160
#define IS_ARG_EQ(a, b)       (!strcmp((a), (b)))
#define ALIGN16(value)           (((value + 15) >> 4) << 4)
#define ALIGN32(X)               (((mfxU32)((X) + 31)) & (~(mfxU32)31))
#define VPLVERSION(major, minor) (major << 16 | minor)

enum ExampleParams { PARAM_IMPL = 0, PARAM_INFILE, PARAM_INRES, PARAM_COUNT };
enum ParamGroup {
    PARAMS_CREATESESSION = 0,
    PARAMS_DECODE,
    PARAMS_ENCODE,
    PARAMS_VPP,
    PARAMS_TRANSCODE
};
typedef struct _Params {
    mfxIMPL impl;
#if (MFX_VERSION >= 2000)
    mfxVariant implValue;
#endif
    char *infileName;
    char *inmodelName;
    mfxU16 srcWidth;
    mfxU16 srcHeight;
} Params;

void VERIFY(bool x,const char* y)  {     
    if (!(x)) {            
        printf("%s\n",y);          
    }
}
char * ValidateFileName(char *in) {
    if (in) {
        if (strnlen(in, MAX_PATH) > MAX_PATH)
            return NULL;
    }

    return in;
}
bool ValidateSize(char *in, mfxU16 *vsize, mfxU32 vmax) {
    if (in) {
        *vsize = static_cast<mfxU16>(strtol(in, NULL, 10));
        if (*vsize <= vmax)
            return true;
    }

    *vsize = 0;
    return false;
}

bool ParseArgsAndValidate(int argc, char *argv[], Params *params, ParamGroup group) {
    int idx;
    char *s;

    // init all params to 0
    *params      = {};
    params->impl = MFX_IMPL_SOFTWARE;
#if (MFX_VERSION >= 2000)
    params->implValue.Type     = MFX_VARIANT_TYPE_U32;
    params->implValue.Data.U32 = MFX_IMPL_TYPE_SOFTWARE;
#endif

    for (idx = 1; idx < argc;) {
        // all switches must start with '-'
        if (argv[idx][0] != '-') {
            printf("ERROR - invalid argument: %s\n", argv[idx]);
            return false;
        }

        // switch string, starting after the '-'
        s = &argv[idx][1];
        idx++;

        // search for match
        if (IS_ARG_EQ(s, "i")) {
            params->infileName = ValidateFileName(argv[idx++]);
            if (!params->infileName) {
                return false;
            }
        }
        else if (IS_ARG_EQ(s, "m")) {
            params->inmodelName = ValidateFileName(argv[idx++]);
            if (!params->inmodelName) {
                return false;
            }
        }
        else if (IS_ARG_EQ(s, "w")) {
            if (!ValidateSize(argv[idx++], &params->srcWidth, MAX_WIDTH))
                return false;
        }
        else if (IS_ARG_EQ(s, "h")) {
            if (!ValidateSize(argv[idx++], &params->srcHeight, MAX_HEIGHT))
                return false;
        }
        else if (IS_ARG_EQ(s, "hw")) {
            params->impl = MFX_IMPL_HARDWARE;
#if (MFX_VERSION >= 2000)
            params->implValue.Data.U32 = MFX_IMPL_TYPE_HARDWARE;
#endif
        }
        else if (IS_ARG_EQ(s, "sw")) {
            params->impl = MFX_IMPL_SOFTWARE;
#if (MFX_VERSION >= 2000)
            params->implValue.Data.U32 = MFX_IMPL_TYPE_SOFTWARE;
#endif
        }
    }

    // input file required by all except createsession
    if ((group != PARAMS_CREATESESSION) && (!params->infileName)) {
        printf("ERROR - input file name (-i) is required\n");
        return false;
    }

    // VPP and encode samples require an input resolution
    if ((PARAMS_VPP == group) || (PARAMS_ENCODE == group)) {
        if ((!params->srcWidth) || (!params->srcHeight)) {
            printf("ERROR - source width/height required\n");
            return false;
        }
    }

    return true;
}
void ShowImplementationInfo(mfxLoader loader, mfxU32 implnum) {
    mfxImplDescription *idesc = nullptr;
    mfxStatus sts;
    // Loads info about implementation at specified list location
    sts = MFXEnumImplementations(loader, implnum, MFX_IMPLCAPS_IMPLDESCSTRUCTURE, (mfxHDL *)&idesc);
    if (!idesc || (sts != MFX_ERR_NONE))
        return;

    printf("Implementation details:\n");
    printf("  ApiVersion:           %hu.%hu  \n", idesc->ApiVersion.Major, idesc->ApiVersion.Minor);
    printf("  Implementation type:  %s\n", (idesc->Impl == MFX_IMPL_TYPE_SOFTWARE) ? "SW" : "HW");
    printf("  AccelerationMode via: ");
    switch (idesc->AccelerationMode) {
        case MFX_ACCEL_MODE_NA:
            printf("NA \n");
            break;
        case MFX_ACCEL_MODE_VIA_D3D9:
            printf("D3D9\n");
            break;
        case MFX_ACCEL_MODE_VIA_D3D11:
            printf("D3D11\n");
            break;
        case MFX_ACCEL_MODE_VIA_VAAPI:
            printf("VAAPI\n");
            break;
        case MFX_ACCEL_MODE_VIA_VAAPI_DRM_MODESET:
            printf("VAAPI_DRM_MODESET\n");
            break;
        case MFX_ACCEL_MODE_VIA_VAAPI_GLX:
            printf("VAAPI_GLX\n");
            break;
        case MFX_ACCEL_MODE_VIA_VAAPI_X11:
            printf("VAAPI_X11\n");
            break;
        case MFX_ACCEL_MODE_VIA_VAAPI_WAYLAND:
            printf("VAAPI_WAYLAND\n");
            break;
        case MFX_ACCEL_MODE_VIA_HDDLUNITE:
            printf("HDDLUNITE\n");
            break;
        default:
            printf("unknown\n");
            break;
    }
    MFXDispReleaseImplDescription(loader, idesc);

#if (MFX_VERSION >= 2004)
    // Show implementation path, added in 2.4 API
    mfxHDL implPath = nullptr;
    sts             = MFXEnumImplementations(loader, implnum, MFX_IMPLCAPS_IMPLPATH, &implPath);
    if (!implPath || (sts != MFX_ERR_NONE))
        return;

    printf("  Path: %s\n\n", reinterpret_cast<mfxChar *>(implPath));
    MFXDispReleaseImplDescription(loader, implPath);
#endif
}
void *InitAcceleratorHandle(mfxSession session) {
    mfxIMPL impl;
    mfxStatus sts = MFXQueryIMPL(session, &impl);
    if (sts != MFX_ERR_NONE)
        return NULL;

#ifdef LIBVA_SUPPORT
    if ((impl & MFX_IMPL_VIA_VAAPI) == MFX_IMPL_VIA_VAAPI) {
        VADisplay va_dpy = NULL;
        int fd;
        // initialize VAAPI context and set session handle (req in Linux)
        fd = open("/dev/dri/renderD128", O_RDWR);
        if (fd >= 0) {
            va_dpy = vaGetDisplayDRM(fd);
            if (va_dpy) {
                int major_version = 0, minor_version = 0;
                if (VA_STATUS_SUCCESS == vaInitialize(va_dpy, &major_version, &minor_version)) {
                    MFXVideoCORE_SetHandle(session,
                                           static_cast<mfxHandleType>(MFX_HANDLE_VA_DISPLAY),
                                           va_dpy);
                }
            }
        }
        return va_dpy;
    }
#endif

    return NULL;
}

void FreeAcceleratorHandle(void *accelHandle) {
#ifdef LIBVA_SUPPORT
    vaTerminate((VADisplay)accelHandle);
#endif
}
void PrepareFrameInfo(mfxFrameInfo *fi, mfxU32 format, mfxU16 w, mfxU16 h) {
    // Video processing input data format
    fi->FourCC        = format;
    fi->ChromaFormat  = MFX_CHROMAFORMAT_YUV420;
    fi->CropX         = 0;
    fi->CropY         = 0;
    fi->CropW         = w;
    fi->CropH         = h;
    fi->PicStruct     = MFX_PICSTRUCT_PROGRESSIVE;
    fi->FrameRateExtN = 30;
    fi->FrameRateExtD = 1;
    // width must be a multiple of 16
    // height must be a multiple of 16 in case of frame picture and a multiple of
    // 32 in case of field picture
    fi->Width = ALIGN16(fi->CropW);
    fi->Height =
        (MFX_PICSTRUCT_PROGRESSIVE == fi->PicStruct) ? ALIGN16(fi->CropH) : ALIGN32(fi->CropH);
}

mfxStatus ReadRawFrame(mfxFrameSurface1 *surface, FILE *f) {
    mfxU16 w, h, i, pitch;
    size_t bytes_read;
    mfxU8 *ptr;
    mfxFrameInfo *info = &surface->Info;
    mfxFrameData *data = &surface->Data;

    w = info->Width;
    h = info->Height;

    switch (info->FourCC) {
        case MFX_FOURCC_I420:
            // read luminance plane (Y)
            pitch = data->Pitch;
            ptr   = data->Y;
            for (i = 0; i < h; i++) {
                bytes_read = (mfxU32)fread(ptr + i * pitch, 1, w, f);
                if (w != bytes_read)
                    return MFX_ERR_MORE_DATA;
            }

            // read chrominance (U, V)
            pitch /= 2;
            h /= 2;
            w /= 2;
            ptr = data->U;
            for (i = 0; i < h; i++) {
                bytes_read = (mfxU32)fread(ptr + i * pitch, 1, w, f);
                if (w != bytes_read)
                    return MFX_ERR_MORE_DATA;
            }

            ptr = data->V;
            for (i = 0; i < h; i++) {
                bytes_read = (mfxU32)fread(ptr + i * pitch, 1, w, f);
                if (w != bytes_read)
                    return MFX_ERR_MORE_DATA;
            }
            break;
        case MFX_FOURCC_NV12:
            // Y
            pitch = data->Pitch;
            for (i = 0; i < h; i++) {
                bytes_read = fread(data->Y + i * pitch, 1, w, f);
                if (w != bytes_read)
                    return MFX_ERR_MORE_DATA;
            }
            // UV
            h /= 2;
            for (i = 0; i < h; i++) {
                bytes_read = fread(data->UV + i * pitch, 1, w, f);
                if (w != bytes_read)
                    return MFX_ERR_MORE_DATA;
            }
            break;
        case MFX_FOURCC_RGB4:
            // Y
            pitch = data->Pitch;
            for (i = 0; i < h; i++) {
                bytes_read = fread(data->B + i * pitch, 1, pitch, f);
                if (pitch != bytes_read)
                    return MFX_ERR_MORE_DATA;
            }
            break;
        default:
            printf("Unsupported FourCC code, skip LoadRawFrame\n");
            break;
    }

    return MFX_ERR_NONE;
}
mfxStatus ReadRawFrame_InternalMem(mfxFrameSurface1 *surface, FILE *f) {
    bool is_more_data = false;

    // Map makes surface writable by CPU for all implementations
    mfxStatus sts = surface->FrameInterface->Map(surface, MFX_MAP_WRITE);
    if (sts != MFX_ERR_NONE) {
        printf("mfxFrameSurfaceInterface->Map failed (%d)\n", sts);
        return sts;
    }

    sts = ReadRawFrame(surface, f);
    if (sts != MFX_ERR_NONE) {
        if (sts == MFX_ERR_MORE_DATA)
            is_more_data = true;
        else
            return sts;
    }

    // Unmap/release returns local device access for all implementations
    sts = surface->FrameInterface->Unmap(surface);
    if (sts != MFX_ERR_NONE) {
        printf("mfxFrameSurfaceInterface->Unmap failed (%d)\n", sts);
        return sts;
    }

    return (is_more_data == true) ? MFX_ERR_MORE_DATA : MFX_ERR_NONE;
}
// Write raw I420 frame to file
mfxStatus WriteRawFrame(mfxFrameSurface1 *surface, FILE *f) {
    mfxU16 w, h, i, pitch;
    mfxFrameInfo *info = &surface->Info;
    mfxFrameData *data = &surface->Data;

    w = info->Width;
    h = info->Height;

    // write the output to disk
    switch (info->FourCC) {
        case MFX_FOURCC_I420:
            // Y
            pitch = data->Pitch;
            for (i = 0; i < h; i++) {
                fwrite(data->Y + i * pitch, 1, w, f);
            }
            // U
            pitch /= 2;
            h /= 2;
            w /= 2;
            for (i = 0; i < h; i++) {
                fwrite(data->U + i * pitch, 1, w, f);
            }
            // V
            for (i = 0; i < h; i++) {
                fwrite(data->V + i * pitch, 1, w, f);
            }
            break;
        case MFX_FOURCC_NV12:
            // Y
            pitch = data->Pitch;
            for (i = 0; i < h; i++) {
                fwrite(data->Y + i * pitch, 1, w, f);
            }
            // UV
            h /= 2;
            for (i = 0; i < h; i++) {
                fwrite(data->UV + i * pitch, 1, w, f);
            }
            break;
        case MFX_FOURCC_RGB4:
            // Y
            pitch = data->Pitch;
            for (i = 0; i < h; i++) {
                fwrite(data->B + i * pitch, 1, pitch, f);
            }
            break;
        default:
            return MFX_ERR_UNSUPPORTED;
            break;
    }

    return MFX_ERR_NONE;
}
mfxStatus WriteRawFrame_InternalMem(mfxFrameSurface1 *surface, FILE *f) {
    mfxStatus sts = surface->FrameInterface->Map(surface, MFX_MAP_READ);
    if (sts != MFX_ERR_NONE) {
        printf("mfxFrameSurfaceInterface->Map failed (%d)\n", sts);
        return sts;
    }

    sts = WriteRawFrame(surface, f);
    if (sts != MFX_ERR_NONE) {
        printf("Error in WriteRawFrame\n");
        return sts;
    }

    sts = surface->FrameInterface->Unmap(surface);
    if (sts != MFX_ERR_NONE) {
        printf("mfxFrameSurfaceInterface->Unmap failed (%d)\n", sts);
        return sts;
    }

    sts = surface->FrameInterface->Release(surface);
    if (sts != MFX_ERR_NONE) {
        printf("mfxFrameSurfaceInterface->Release failed (%d)\n", sts);
        return sts;
    }

    return sts;
}


int main(int argc, char *argv[]) {


    cout<<"helloe world \n";
    bool isDraining                 = false;
    bool isStillGoing               = true;
    FILE *sink                      = NULL;
    FILE *source                    = NULL;
    mfxFrameSurface1 *vppInSurface  = NULL;
    mfxFrameSurface1 *vppOutSurface = NULL;
    mfxSession session              = NULL;
    mfxSyncPoint syncp              = {};
    mfxU32 framenum                 = 0;
    mfxStatus sts                   = MFX_ERR_NONE;
    mfxStatus sts_r                 = MFX_ERR_NONE;
    Params cliParams                = {};
    void *accelHandle               = NULL;
    mfxVideoParam VPPParams         = {};
    mfxVariant cfgVal[3] ;
    mfxConfig cfg[3];

    if (ParseArgsAndValidate(argc, argv, &cliParams, PARAMS_VPP) == false) {
        
        return 1; // return 1 as error code
    }
    
    mfxLoader loader = MFXLoad();
    VERIFY(NULL != loader, "MFXLoad failed -- is implementation in path?");
    cfg[0] = MFXCreateConfig(loader);
    cfg[1] = MFXCreateConfig(loader);
    cfg[2] = MFXCreateConfig(loader);
    VERIFY(NULL != cfg[0]|| NULL != cfg[1] || NULL != cfg[2], "MFXCreateConfig failed");

    source = fopen("big_buck_bunny_720p_1mb.mp4", "rb");
    sink = fopen(OUTPUT_FILE, "wb");
    
    cfgVal[1].Type = MFX_VARIANT_TYPE_U32;
    cfgVal[1].Data.U32 = MFX_EXTBUFF_VPP_SCALING;
    cfgVal[2].Type = MFX_VARIANT_TYPE_U32;
    cfgVal[2].Data.U32 = VPLVERSION(MAJOR_API_VERSION_REQUIRED, MINOR_API_VERSION_REQUIRED);
    
    cfgVal[0].Type = MFX_VARIANT_TYPE_U32;
    cfgVal[0].Data.U32 =  MFX_IMPL_TYPE_SOFTWARE ;
    
    sts = MFXSetConfigFilterProperty(cfg[0], (mfxU8 *)"mfxImplDescription.Impl", cfgVal[0]);
    VERIFY(MFX_ERR_NONE == sts, "MFXSetConfigFilterProperty failed for Impl");
    sts = MFXSetConfigFilterProperty(cfg[1],(mfxU8 *)"mfxImplDescription.mfxVPPDescription.filter.FilterFourCC",cfgVal[1]);
    VERIFY(MFX_ERR_NONE == sts, "MFXSetConfigFilterProperty mfxImplDescription.mfxVPPDescription.filter.FilterFourCC failed");
    sts = MFXSetConfigFilterProperty(cfg[2],(mfxU8 *)"mfxImplDescription.ApiVersion.Version",cfgVal[2]);
    VERIFY(MFX_ERR_NONE == sts, "MFXSetConfigFilterProperty mfxImplDescription.ApiVersion.Version failed");
    

    sts = MFXCreateSession(loader, 0, &session);
    printf(" s = %u\n\n",sts);
    VERIFY(MFX_ERR_NONE == sts, "MFXCreateSession failed" );
    ShowImplementationInfo(loader, 0);
    accelHandle = InitAcceleratorHandle(session);


    PrepareFrameInfo(&VPPParams.vpp.In,MFX_FOURCC_I420,cliParams.srcWidth,cliParams.srcHeight);
    PrepareFrameInfo(&VPPParams.vpp.Out, MFX_FOURCC_BGRA, OUTPUT_WIDTH, OUTPUT_HEIGHT);

    VPPParams.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    // Initialize VPP
    sts = MFXVideoVPP_Init(session, &VPPParams);
    VERIFY(MFX_ERR_NONE == sts, "Could not initialize VPP");

    printf("Processing %s -> %s\n", cliParams.infileName, OUTPUT_FILE);

    while (isStillGoing == true) {
        // Load a new frame if not draining
        if (isDraining == false) {
            sts = MFXMemory_GetSurfaceForVPPIn(session, &vppInSurface);
            VERIFY(MFX_ERR_NONE == sts, "Unknown error in MFXMemory_GetSurfaceForVPPIn");

            sts = ReadRawFrame_InternalMem(vppInSurface, source);
            if (sts == MFX_ERR_MORE_DATA)
                isDraining = true;
            else
                VERIFY(MFX_ERR_NONE == sts, "Unknown error reading input");

            sts = MFXMemory_GetSurfaceForVPPOut(session, &vppOutSurface);
            VERIFY(MFX_ERR_NONE == sts, "Unknown error in MFXMemory_GetSurfaceForVPPIn");
        }

        sts = MFXVideoVPP_RunFrameVPPAsync(session,
                                           (isDraining == true) ? NULL : vppInSurface,
                                           vppOutSurface,
                                           NULL,
                                           &syncp);

        if (!isDraining) {
            sts_r = vppInSurface->FrameInterface->Release(vppInSurface);
            VERIFY(MFX_ERR_NONE == sts_r, "mfxFrameSurfaceInterface->Release failed");
        }

        switch (sts) {
            case MFX_ERR_NONE:
                do {
                    sts = vppOutSurface->FrameInterface->Synchronize(vppOutSurface,
                                                                     WAIT_100_MILLISECONDS);
                    if (MFX_ERR_NONE == sts) {
                        sts = WriteRawFrame_InternalMem(vppOutSurface, sink);
                        VERIFY(MFX_ERR_NONE == sts, "Could not write vpp output");

                        framenum++;
                    }
                } while (sts == MFX_WRN_IN_EXECUTION);
                break;
            case MFX_ERR_MORE_DATA:
                // Need more input frames before VPP can produce an output
                if (isDraining)
                    isStillGoing = false;
                break;
            case MFX_ERR_MORE_SURFACE:
                // Need more surfaces at output for additional output frames available.
                // This applies to external memory allocations and should not be
                // expected for a simple internal allocation case like this
                break;
            case MFX_ERR_DEVICE_LOST:
                // For non-CPU implementations,
                // Cleanup if device is lost
                break;
            case MFX_WRN_DEVICE_BUSY:
                // For non-CPU implementations,
                // Wait a few milliseconds then try again
                break;
            default:
                printf("unknown status %d\n", sts);
                isStillGoing = false;
                break;
        }
    }


    printf("Processed %d frames\n", framenum);

    // Clean up resources - It is recommended to close components first, before
    // releasing allocated surfaces, since some surfaces may still be locked by
    // internal resources.
    if (source)
        fclose(source);

    if (sink)
        fclose(sink);

    MFXVideoVPP_Close(session);
    MFXClose(session);

    if (accelHandle)
        FreeAcceleratorHandle(accelHandle);

    if (loader)
        MFXUnload(loader);
        
    return 0;
}




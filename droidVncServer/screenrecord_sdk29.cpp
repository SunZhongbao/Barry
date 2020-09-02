/*
 * Copyright 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "common.h"
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <termios.h>
#include <unistd.h>

#define LOG_TAG "ScreenRecord"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS
//#define LOG_NDEBUG 0
#include <utils/Log.h>

#include <binder/IPCThreadState.h>
#include <utils/Errors.h>
#include <utils/Timers.h>
#include <utils/Trace.h>

#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#include <ui/DisplayInfo.h>
#include <media/openmax/OMX_IVCommon.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaCodec.h>
#undef KEY_LANGUAGE
#include <media/stagefright/MediaCodecConstants.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaMuxer.h>
#include <media/stagefright/PersistentSurface.h>
#include <media/ICrypto.h>
#include <media/MediaCodecBuffer.h>

#include "screenrecord.h"
#include "Overlay.h"
#include "FrameOutput.h"

using android::ABuffer;
using android::ALooper;
using android::AMessage;
using android::AString;
using android::DisplayInfo;
using android::FrameOutput;
using android::IBinder;
using android::IGraphicBufferProducer;
using android::ISurfaceComposer;
using android::MediaCodec;
using android::MediaCodecBuffer;
using android::MediaMuxer;
using android::Overlay;
using android::PersistentSurface;
using android::ProcessState;
using android::Rect;
using android::String8;
using android::SurfaceComposerClient;
using android::Vector;
using android::sp;
using android::status_t;

using android::DISPLAY_ORIENTATION_0;
using android::DISPLAY_ORIENTATION_180;
using android::DISPLAY_ORIENTATION_90;
using android::INVALID_OPERATION;
using android::NAME_NOT_FOUND;
using android::NO_ERROR;
using android::UNKNOWN_ERROR;

static const uint32_t kMinBitRate = 100000;         // 0.1Mbps
static const uint32_t kMaxBitRate = 200 * 1000000;  // 200Mbps
static const uint32_t kMaxTimeLimitSec = 180;       // 3 minutes
static const uint32_t kFallbackWidth = 1280;        // 720p
static const uint32_t kFallbackHeight = 720;
static const char* kMimeTypeAvc = "video/avc";

// Command-line parameters.
static bool gVerbose = false;           // chatty on stdout
static bool gRotate = false;            // rotate 90 degrees
static bool gPersistentSurface = false; // use persistent surface
static AString gCodecName = "";         // codec name override
static bool gSizeSpecified = false;     // was size explicitly requested?
static bool gWantInfoScreen = false;    // do we want initial info screen?
static bool gWantFrameTime = false;     // do we want times on each frame?
uint32_t gVideoWidth = 480;
uint32_t gVideoHeight = 640;
static uint32_t gBitRate = 4000000;     // 4Mbps
static uint32_t gTimeLimitSec = kMaxTimeLimitSec;
static uint32_t gBframes = 0;

// Set by signal handler to stop recording.
volatile int gStopRequested;

// Previous signal handler state, restored after first hit.
static struct sigaction gOrigSigactionINT;
static struct sigaction gOrigSigactionHUP;
static struct sigaction gOrigSigactionPIPE;

/*
 * Catch keyboard interrupt signals.  On receipt, the "stop requested"
 * flag is raised, and the original handler is restored (so that, if
 * we get stuck finishing, a second Ctrl-C will kill the process).
 */
static void signalCatcher(int signum)
{
    gStopRequested = true;
    switch (signum) {
    case SIGINT:
    case SIGHUP:
    case SIGPIPE:
        sigaction(SIGINT, &gOrigSigactionINT, NULL);
        sigaction(SIGHUP, &gOrigSigactionHUP, NULL);
        sigaction(SIGHUP, &gOrigSigactionPIPE, NULL);
        break;
    default:
        abort();
        break;
    }
}

/*
 * Configures signal handlers.  The previous handlers are saved.
 *
 * If the command is run from an interactive adb shell, we get SIGINT
 * when Ctrl-C is hit.  If we're run from the host, the local adb process
 * gets the signal, and we get a SIGHUP when the terminal disconnects.
 */
static status_t configureSignals() {
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = signalCatcher;
    if (sigaction(SIGINT, &act, &gOrigSigactionINT) != 0) {
        status_t err = -errno;
        fprintf(stderr, "Unable to configure SIGINT handler: %s\n",
                strerror(errno));
        return err;
    }
    if (sigaction(SIGHUP, &act, &gOrigSigactionHUP) != 0) {
        status_t err = -errno;
        fprintf(stderr, "Unable to configure SIGHUP handler: %s\n",
                strerror(errno));
        return err;
    }
    if (sigaction(SIGPIPE, &act, &gOrigSigactionPIPE) != 0) {
        status_t err = -errno;
        fprintf(stderr, "Unable to configure SIGPIPE handler: %s\n",
                strerror(errno));
        return err;
    }
    return NO_ERROR;
}

/*
 * Returns "true" if the device is rotated 90 degrees.
 */
static bool isDeviceRotated(int orientation) {
    return orientation != DISPLAY_ORIENTATION_0 &&
            orientation != DISPLAY_ORIENTATION_180;
}

/*
 * Configures and starts the MediaCodec encoder.  Obtains an input surface
 * from the codec.
 */
static status_t prepareEncoder(float displayFps, sp<MediaCodec>* pCodec,
        sp<IGraphicBufferProducer>* pBufferProducer) {
    status_t err;

    if (gVerbose) {
        printf("Configuring recorder for %dx%d %s at %.2fMbps\n",
                gVideoWidth, gVideoHeight, kMimeTypeAvc, gBitRate / 1000000.0);
        fflush(stdout);
    }

    sp<AMessage> format = new AMessage;
    format->setInt32(KEY_WIDTH, gVideoWidth);
    format->setInt32(KEY_HEIGHT, gVideoHeight);
    format->setString(KEY_MIME, kMimeTypeAvc);
    format->setInt32(KEY_COLOR_FORMAT, OMX_COLOR_FormatAndroidOpaque);
    format->setInt32(KEY_BIT_RATE, gBitRate);
    format->setFloat(KEY_FRAME_RATE, displayFps);
    format->setInt32(KEY_I_FRAME_INTERVAL, 10);
    format->setInt32(KEY_MAX_B_FRAMES, gBframes);
    if (gBframes > 0) {
        format->setInt32(KEY_PROFILE, AVCProfileMain);
        format->setInt32(KEY_LEVEL, AVCLevel41);
    }

    sp<android::ALooper> looper = new android::ALooper;
    looper->setName("screenrecord_looper");
    looper->start();
    ALOGV("Creating codec");
    sp<MediaCodec> codec;
    if (gCodecName.empty()) {
        codec = MediaCodec::CreateByType(looper, kMimeTypeAvc, true);
        if (codec == NULL) {
            fprintf(stderr, "ERROR: unable to create %s codec instance\n",
                    kMimeTypeAvc);
            return UNKNOWN_ERROR;
        }
    } else {
        codec = MediaCodec::CreateByComponentName(looper, gCodecName);
        if (codec == NULL) {
            fprintf(stderr, "ERROR: unable to create %s codec instance\n",
                    gCodecName.c_str());
            return UNKNOWN_ERROR;
        }
    }

    err = codec->configure(format, NULL, NULL,
            MediaCodec::CONFIGURE_FLAG_ENCODE);
    if (err != NO_ERROR) {
        fprintf(stderr, "ERROR: unable to configure %s codec at %dx%d (err=%d)\n",
                kMimeTypeAvc, gVideoWidth, gVideoHeight, err);
        codec->release();
        return err;
    }

    ALOGV("Creating encoder input surface");
    sp<IGraphicBufferProducer> bufferProducer;
    if (gPersistentSurface) {
        sp<PersistentSurface> surface = MediaCodec::CreatePersistentInputSurface();
        bufferProducer = surface->getBufferProducer();
        err = codec->setInputSurface(surface);
    } else {
        err = codec->createInputSurface(&bufferProducer);
    }
    if (err != NO_ERROR) {
        fprintf(stderr,
            "ERROR: unable to %s encoder input surface (err=%d)\n",
            gPersistentSurface ? "set" : "create",
            err);
        codec->release();
        return err;
    }

    ALOGV("Starting codec");
    err = codec->start();
    if (err != NO_ERROR) {
        fprintf(stderr, "ERROR: unable to start codec (err=%d)\n", err);
        codec->release();
        return err;
    }

    ALOGV("Codec prepared");
    *pCodec = codec;
    *pBufferProducer = bufferProducer;
    return 0;
}

/*
 * Sets the display projection, based on the display dimensions, video size,
 * and device orientation.
 */
static status_t setDisplayProjection(
        SurfaceComposerClient::Transaction& t,
        const sp<IBinder>& dpy,
        const DisplayInfo& mainDpyInfo) {

    // Set the region of the layer stack we're interested in, which in our
    // case is "all of it".
    Rect layerStackRect(mainDpyInfo.viewportW, mainDpyInfo.viewportH);

    // We need to preserve the aspect ratio of the display.
    float displayAspect = (float) mainDpyInfo.viewportH / (float) mainDpyInfo.viewportW;


    // Set the way we map the output onto the display surface (which will
    // be e.g. 1280x720 for a 720p video).  The rect is interpreted
    // post-rotation, so if the display is rotated 90 degrees we need to
    // "pre-rotate" it by flipping width/height, so that the orientation
    // adjustment changes it back.
    //
    // We might want to encode a portrait display as landscape to use more
    // of the screen real estate.  (If players respect a 90-degree rotation
    // hint, we can essentially get a 720x1280 video instead of 1280x720.)
    // In that case, we swap the configured video width/height and then
    // supply a rotation value to the display projection.
    uint32_t videoWidth, videoHeight;
    uint32_t outWidth, outHeight;
    if (!gRotate) {
        videoWidth = gVideoWidth;
        videoHeight = gVideoHeight;
    } else {
        videoWidth = gVideoHeight;
        videoHeight = gVideoWidth;
    }
    if (videoHeight > (uint32_t)(videoWidth * displayAspect)) {
        // limited by narrow width; reduce height
        outWidth = videoWidth;
        outHeight = (uint32_t)(videoWidth * displayAspect);
    } else {
        // limited by short height; restrict width
        outHeight = videoHeight;
        outWidth = (uint32_t)(videoHeight / displayAspect);
    }
    uint32_t offX, offY;
    offX = (videoWidth - outWidth) / 2;
    offY = (videoHeight - outHeight) / 2;
    Rect displayRect(offX, offY, offX + outWidth, offY + outHeight);

    if (gVerbose) {
        if (gRotate) {
            printf("Rotated content area is %ux%u at offset x=%d y=%d\n",
                    outHeight, outWidth, offY, offX);
            fflush(stdout);
        } else {
            printf("Content area is %ux%u at offset x=%d y=%d\n",
                    outWidth, outHeight, offX, offY);
            fflush(stdout);
        }
    }

    t.setDisplayProjection(dpy,
            gRotate ? DISPLAY_ORIENTATION_90 : DISPLAY_ORIENTATION_0,
            layerStackRect, displayRect);
    return NO_ERROR;
}

/*
 * Configures the virtual display.  When this completes, virtual display
 * frames will start arriving from the buffer producer.
 */
static status_t prepareVirtualDisplay(const DisplayInfo& mainDpyInfo,
        const sp<IGraphicBufferProducer>& bufferProducer,
        sp<IBinder>* pDisplayHandle) {
    sp<IBinder> dpy = SurfaceComposerClient::createDisplay(
            String8("ScreenRecorder"), false /*secure*/);

    SurfaceComposerClient::Transaction t;
    t.setDisplaySurface(dpy, bufferProducer);
    setDisplayProjection(t, dpy, mainDpyInfo);
    t.setDisplayLayerStack(dpy, 0);    // default stack
    t.apply();

    *pDisplayHandle = dpy;

    return NO_ERROR;
}
sp<FrameOutput> frameOutput;
sp<IGraphicBufferProducer> encoderInputSurface;
sp<IBinder> dpy;

int pipefd[2];
pid_t cpid;
char buf;

int
open_screen_record()
{
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    cpid = fork();
    if (cpid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (cpid == 0) {    /* 子プロセスがパイプから読み込む */
        close(pipefd[0]);  /* 使用しない write 側はクローズする */
        dup2(pipefd[1], 1);
        close(pipefd[1]);

        char cmdline[1024];
        snprintf(cmdline, sizeof(cmdline), "screenrecord --size %dx%d --output-format raw-frames -", gVideoWidth, gVideoHeight);
        system(cmdline);


        write(STDOUT_FILENO, "\n", 1);
        _exit(EXIT_SUCCESS);

    } else {            /* 親プロセスは argv[1] をパイプへ書き込む */
        close(pipefd[1]);          /* 使用しない read 側はクローズする */
        return(pipefd[0]);
    }
}

/*
 * Main "do work" start point.
 *
 * Configures codec, muxer, and virtual display, then starts moving bits
 * around.
 */
sp<Overlay> overlay;
static status_t recordScreen() {
    status_t err;

    // Configure signal handler.
    err = configureSignals();
    if (err != NO_ERROR) return err;

    // Start Binder thread pool.  MediaCodec needs to be able to receive
    // messages from mediaserver.
    sp<ProcessState> self = ProcessState::self();
    self->startThreadPool();

    // Get main display parameters.
    const sp<IBinder> mainDpy = SurfaceComposerClient::getInternalDisplayToken();
    if (mainDpy == nullptr) {
        fprintf(stderr, "ERROR: no display\n");
        return NAME_NOT_FOUND;
    }

    DisplayInfo mainDpyInfo;
    err = SurfaceComposerClient::getDisplayInfo(mainDpy, &mainDpyInfo);
    if (err != NO_ERROR) {
        fprintf(stderr, "ERROR: unable to get display characteristics\n");
        return err;
    }
    if (gVerbose) {
        printf("Main display is %dx%d @%.2ffps (orientation=%u)\n",
                mainDpyInfo.viewportW, mainDpyInfo.viewportH, mainDpyInfo.fps,
                mainDpyInfo.orientation);
        fflush(stdout);
    }

    bool rotated = isDeviceRotated(mainDpyInfo.orientation);
    if (gVideoWidth == 0) {
        gVideoWidth = rotated ? mainDpyInfo.h : mainDpyInfo.w;
        if (gVideoHeight == 0) {
            gVideoHeight = rotated ? mainDpyInfo.w : mainDpyInfo.h;
        }
        extern uint16_t g_scaling;
        gVideoWidth = (gVideoWidth * g_scaling / 100 + 7) / 8 * 8;
        gVideoHeight = (gVideoHeight * g_scaling / 100 + 7) / 8 * 8;
    } else {
        gVideoWidth = (mainDpyInfo.w * gVideoHeight / mainDpyInfo.h + 7) / 8 * 8;
    }

    fprintf(stderr, "%s:%d: gVideoWidth is %d\n", __FILE__, __LINE__, gVideoWidth);


    open_screen_record();
    screenFormat &format = screenformat;

    format.bitsPerPixel = 32;
    format.width = gVideoWidth;
    format.pad = 0;
    format.height =     gVideoHeight;
    format.size = format.bitsPerPixel * format.width * format.height / CHAR_BIT;
    format.redShift = 0;
    format.redMax = 8;
    format.greenShift = 8;
    format.greenMax = 8;
    format.blueShift = 16;
    format.blueMax = 8;
    format.alphaShift = 24;
    format.alphaMax = 8;
    return err;
}

/*
 * Parses a string of the form "1280x720".
 *
 * Returns true on success.
 */
static bool parseWidthHeight(const char* widthHeight, uint32_t* pWidth,
        uint32_t* pHeight) {
    long width, height;
    char* end;

    // Must specify base 10, or "0x0" gets parsed differently.
    width = strtol(widthHeight, &end, 10);
    if (end == widthHeight || *end != 'x' || *(end+1) == '\0') {
        // invalid chars in width, or missing 'x', or missing height
        return false;
    }
    height = strtol(end + 1, &end, 10);
    if (*end != '\0') {
        // invalid chars in height
        return false;
    }

    *pWidth = width;
    *pHeight = height;
    return true;
}

/*
 * Accepts a string with a bare number ("4000000") or with a single-character
 * unit ("4m").
 *
 * Returns an error if parsing fails.
 */
static status_t parseValueWithUnit(const char* str, uint32_t* pValue) {
    long value;
    char* endptr;

    value = strtol(str, &endptr, 10);
    if (*endptr == '\0') {
        // bare number
        *pValue = value;
        return NO_ERROR;
    } else if (toupper(*endptr) == 'M' && *(endptr+1) == '\0') {
        *pValue = value * 1000000;  // check for overflow?
        return NO_ERROR;
    } else {
        fprintf(stderr, "Unrecognized value: %s\n", str);
        return UNKNOWN_ERROR;
    }
}

/*
 * Dumps usage on stderr.
 */
static void usage() {
    fprintf(stderr,
        "Usage: screenrecord [options] <filename>\n"
        "\n"
        "Android screenrecord v%d.%d.  Records the device's display to a .mp4 file.\n"
        "\n"
        "Options:\n"
        "--size WIDTHxHEIGHT\n"
        "    Set the video size, e.g. \"1280x720\".  Default is the device's main\n"
        "    display resolution (if supported), 1280x720 if not.  For best results,\n"
        "    use a size supported by the AVC encoder.\n"
        "--bit-rate RATE\n"
        "    Set the video bit rate, in bits per second.  Value may be specified as\n"
        "    bits or megabits, e.g. '4000000' is equivalent to '4M'.  Default %dMbps.\n"
        "--bugreport\n"
        "    Add additional information, such as a timestamp overlay, that is helpful\n"
        "    in videos captured to illustrate bugs.\n"
        "--time-limit TIME\n"
        "    Set the maximum recording time, in seconds.  Default / maximum is %d.\n"
        "--verbose\n"
        "    Display interesting information on stdout.\n"
        "--help\n"
        "    Show this message.\n"
        "\n"
        "Recording continues until Ctrl-C is hit or the time limit is reached.\n"
        "\n",
        kVersionMajor, kVersionMinor, gBitRate / 1000000, gTimeLimitSec
        );
}

screenFormat screenformat;

/*
 * Parses args and kicks things off.
 */
int initScreenRecord(void)
{
    status_t err = recordScreen();
    ALOGD(err == NO_ERROR ? "success" : "failed");
    return (int) err;
}

int closeScreenRecord()
{
    encoderInputSurface = NULL;
    SurfaceComposerClient::destroyDisplay(dpy);
    if (overlay != NULL) overlay->stop();
    return 0;
}

static unsigned char * buff_3;
static unsigned char * buff_4;
unsigned char *readBufferFlinger(void)
{
    int n_pixels = gVideoWidth * gVideoHeight;

    if (buff_3 == NULL) {
        buff_3 = (unsigned char *)malloc(3 * n_pixels);
    }

    if (buff_4 == NULL) {
        buff_4 = (unsigned char *)malloc(4 * n_pixels);
    }

    if (buff_3 == NULL || buff_4 == NULL) {
        fprintf(stderr, "%s:%d: oom?\n", __FILE__, __LINE__);
        exit(1);
    }
    while (!gStopRequested) {
        // Poll for frames, the same way we do for MediaCodec.  We do
        // all of the work on the main thread.
        //
        // Ideally we'd sleep indefinitely and wake when the
        // stop was requested, but this will do for now.  (It almost
        // works because wait() wakes when a signal hits, but we
        // need to handle the edge cases.)
        int n_total_read = 0;
        int n_1_read;
        while ((n_1_read = read(pipefd[0], buff_3 + n_total_read, 3 * n_pixels - n_total_read)) > 0) {
            n_total_read += n_1_read;
            if (n_total_read >= 3 * n_pixels) {
                break;
            }
        }

        unsigned char* p3 = buff_3;
        unsigned char* p4 = buff_4;
        for (int i = 0; i < n_pixels; i++) {
            *p4++ = *p3++;
            *p4++ = *p3++;
            *p4++ = *p3++;
            *p4++ = 255;
        }

        return buff_4;
    }
    return NULL;
}

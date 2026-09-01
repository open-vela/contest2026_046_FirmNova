#include "rvt_display_port.h"
#include "rvt_map_api.h"
#include "rvt_wifi_display_osal.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef CONFIG_VIDEO_FB
#include <nuttx/video/fb.h>
#endif

#include <sys/videoio.h>

#ifndef RVT_R528_HAS_V4L2_MJPEG
#define RVT_R528_HAS_V4L2_MJPEG 0
#endif

#ifndef RVT_R528_HAS_CEDARC_MJPEG
#define RVT_R528_HAS_CEDARC_MJPEG 0
#endif

#ifndef RVT_R528_HAS_CEDARC_H264
#define RVT_R528_HAS_CEDARC_H264 0
#endif

#define RVT_R528_HAS_CEDARC (RVT_R528_HAS_CEDARC_MJPEG || RVT_R528_HAS_CEDARC_H264)

#if RVT_R528_HAS_V4L2_MJPEG || RVT_R528_HAS_CEDARC
#include <videoOutPort.h>
#endif

#if RVT_R528_HAS_CEDARC
#include <memoryAdapter.h>
#include <veAdapter.h>
#include <vdecoder.h>
#endif

#define RVT_R528_DEFAULT_WIDTH   800
#define RVT_R528_DEFAULT_HEIGHT  480
#define RVT_R528_FB_DEV         "/dev/fb0"
#define RVT_R528_CEDARC_FPS      30
#define RVT_R528_CEDARC_STRIDE   16
#define RVT_R528_CEDARC_SMOOTH   3
#ifndef CONFIG_RIVOTEK_WIFI_DISPLAY_R528_V4L2_DEV
#define CONFIG_RIVOTEK_WIFI_DISPLAY_R528_V4L2_DEV "/dev/video0"
#endif
#define RVT_R528_MAX_FRAME_SIZE  (512 * 1024)
#define RVT_R528_CODEC_BUFS      3
#define RVT_R528_POLL_TIMEOUT_MS 200

static struct rvt_display_caps cached_caps;
static int caps_valid;
static int warned_submit;
static int warned_no_decoder;
static int display_port_ready;
static int display_active_notified;

#if RVT_R528_HAS_V4L2_MJPEG || RVT_R528_HAS_CEDARC
static dispOutPort *video_out;
static videoParam video_param;
static int display_ready;

static void r528_notify_display_active_once(void)
{
    if (display_active_notified)
        return;

    display_active_notified = 1;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "r528 display: first frame shown");
    rvt_map_handle_display_active();
}

/*
 * 函数名: r528_display_open
 * 入参: 无
 * 返回值: 0 表示显示层初始化成功，负值表示失败
 */
static int r528_display_open(void)
{
    struct rvt_display_caps caps;
    VoutRect rect;
    int ret;

    if (display_ready)
        return 0;

    if (rvt_display_port_get_caps(&caps) != 0)
        return -1;

    video_out = CreateVideoOutport(0);
    if (!video_out) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "r528 display: CreateVideoOutport(0) failed");
        return -ENOMEM;
    }

    rect.x = 0;
    rect.y = 0;
    rect.width = caps.width;
    rect.height = caps.height;

    ret = video_out->init(video_out, 1, ROTATION_ANGLE_0, &rect);
    if (ret != 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "r528 display: video out init failed ret=%d rect=%dx%d",
                 ret, rect.width, rect.height);
        DestroyVideoOutport(video_out);
        video_out = NULL;
        return -1;
    }

    video_out->setRoute(video_out, VIDEO_SRC_FROM_FILE);
    video_out->SetZorder(video_out, VIDEO_ZORDER_TOP);
    display_ready = 1;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "r528 display: video out ready screen=0 rect=%ux%u route=file",
             rect.width, rect.height);
    return 0;
}

/*
 * 函数名: r528_display_close
 * 入参: 无
 * 返回值: 无
 */
static void r528_display_close(void)
{
    if (!video_out)
        return;

    if (display_ready)
        video_out->setEnable(video_out, 0);

    video_out->deinit(video_out);
    DestroyVideoOutport(video_out);
    video_out = NULL;
    display_ready = 0;
}
#endif

#if RVT_R528_HAS_V4L2_MJPEG
struct r528_v4l2_buffer {
    void *addr;
    size_t length;
    struct v4l2_buffer buf;
    int queued;
};

struct r528_v4l2_context {
    enum v4l2_buf_type type;
    struct v4l2_format fmt;
    struct r528_v4l2_buffer buffers[RVT_R528_CODEC_BUFS];
    int count;
};

static int decoder_fd = -1;
static struct r528_v4l2_context output_ctx;
static struct r528_v4l2_context capture_ctx;
static int output_stream_on;
static int capture_stream_on;
static int decoder_ready;

/*
 * 函数名: r528_v4l2_is_mplane
 * 入参: type V4L2 队列类型
 * 返回值: 1 表示多平面队列，0 表示单平面队列
 */
static int r528_v4l2_is_mplane(enum v4l2_buf_type type)
{
    return type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ||
           type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
}

/*
 * 函数名: r528_v4l2_set_pix
 * 入参: fmt V4L2 格式，type 队列类型，fourcc 像素格式，width 宽度，height 高度，sizeimage 缓冲大小
 * 返回值: 无
 */
static void r528_v4l2_set_pix(struct v4l2_format *fmt,
        enum v4l2_buf_type type, uint32_t fourcc,
        int width, int height, uint32_t sizeimage)
{
    memset(fmt, 0, sizeof(*fmt));
    fmt->type = type;
    if (r528_v4l2_is_mplane(type)) {
        fmt->fmt.pix_mp.width = width;
        fmt->fmt.pix_mp.height = height;
        fmt->fmt.pix_mp.pixelformat = fourcc;
        fmt->fmt.pix_mp.field = V4L2_FIELD_NONE;
        fmt->fmt.pix_mp.colorspace = V4L2_COLORSPACE_JPEG;
        fmt->fmt.pix_mp.num_planes = 1;
        fmt->fmt.pix_mp.plane_fmt[0].sizeimage = sizeimage;
    } else {
        fmt->fmt.pix.width = width;
        fmt->fmt.pix.height = height;
        fmt->fmt.pix.pixelformat = fourcc;
        fmt->fmt.pix.field = V4L2_FIELD_NONE;
        fmt->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
        fmt->fmt.pix.sizeimage = sizeimage;
    }
}

/*
 * 函数名: r528_v4l2_width
 * 入参: fmt V4L2 格式
 * 返回值: 图像宽度
 */
static int r528_v4l2_width(const struct v4l2_format *fmt)
{
    return r528_v4l2_is_mplane((enum v4l2_buf_type)fmt->type) ?
           (int)fmt->fmt.pix_mp.width : (int)fmt->fmt.pix.width;
}

/*
 * 函数名: r528_v4l2_height
 * 入参: fmt V4L2 格式
 * 返回值: 图像高度
 */
static int r528_v4l2_height(const struct v4l2_format *fmt)
{
    return r528_v4l2_is_mplane((enum v4l2_buf_type)fmt->type) ?
           (int)fmt->fmt.pix_mp.height : (int)fmt->fmt.pix.height;
}

/*
 * 函数名: r528_v4l2_pixelformat
 * 入参: fmt V4L2 格式
 * 返回值: 像素格式 fourcc
 */
static uint32_t r528_v4l2_pixelformat(const struct v4l2_format *fmt)
{
    return r528_v4l2_is_mplane((enum v4l2_buf_type)fmt->type) ?
           fmt->fmt.pix_mp.pixelformat : fmt->fmt.pix.pixelformat;
}

/*
 * 函数名: r528_v4l2_sizeimage
 * 入参: fmt V4L2 格式
 * 返回值: 单帧缓冲大小
 */
static uint32_t r528_v4l2_sizeimage(const struct v4l2_format *fmt)
{
    if (r528_v4l2_is_mplane((enum v4l2_buf_type)fmt->type))
        return fmt->fmt.pix_mp.plane_fmt[0].sizeimage;

    return fmt->fmt.pix.sizeimage;
}

/*
 * 函数名: fourcc_supported
 * 入参: fd V4L2 设备描述符，type 队列类型，fourcc 需要检查的像素格式
 * 返回值: 1 表示支持，0 表示不支持或查询失败
 */
static int fourcc_supported(int fd, enum v4l2_buf_type type, uint32_t fourcc)
{
    struct v4l2_fmtdesc desc;

    memset(&desc, 0, sizeof(desc));
    desc.type = type;

    while (ioctl(fd, VIDIOC_ENUM_FMT, &desc) == 0) {
        if (desc.pixelformat == fourcc)
            return 1;
        desc.index++;
    }

    return 0;
}

/*
 * 函数名: r528_decoder_query_types
 * 入参: fd V4L2 设备描述符，output_type 输出输入码流队列类型，capture_type 输出解码帧队列类型
 * 返回值: 0 表示设备是可用 M2M 解码设备，负值表示不可用
 */
static int r528_decoder_query_types(int fd,
        enum v4l2_buf_type *output_type, enum v4l2_buf_type *capture_type)
{
    struct v4l2_capability cap;

    memset(&cap, 0, sizeof(cap));
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
        return -errno;

    if (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) {
        *output_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        *capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        return 0;
    }

    if (cap.capabilities & V4L2_CAP_VIDEO_M2M) {
        *output_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        *capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        return 0;
    }

    return -ENODEV;
}

/*
 * 函数名: r528_decoder_probe_fd
 * 入参: fd V4L2 设备描述符
 * 返回值: 0 表示支持 MJPEG/JPEG 到 NV12/YUV420，负值表示不支持
 */
static int r528_decoder_probe_fd(int fd)
{
    enum v4l2_buf_type output_type;
    enum v4l2_buf_type capture_type;
    int ret;
    int compressed_ok;
    int raw_ok;

    ret = r528_decoder_query_types(fd, &output_type, &capture_type);
    if (ret != 0)
        return ret;

    compressed_ok = fourcc_supported(fd, output_type, V4L2_PIX_FMT_MJPEG) ||
                    fourcc_supported(fd, output_type, V4L2_PIX_FMT_JPEG);
    raw_ok = fourcc_supported(fd, capture_type, V4L2_PIX_FMT_NV12) ||
             fourcc_supported(fd, capture_type, V4L2_PIX_FMT_YUV420);

    return compressed_ok && raw_ok ? 0 : -ENOTSUP;
}

/*
 * 函数名: r528_codec_unmap_context
 * 入参: ctx V4L2 buffer 上下文
 * 返回值: 无
 */
static void r528_codec_unmap_context(struct r528_v4l2_context *ctx)
{
    int i;

    if (!ctx)
        return;

    for (i = 0; i < ctx->count; i++) {
        if (ctx->buffers[i].addr && ctx->buffers[i].addr != MAP_FAILED)
            munmap(ctx->buffers[i].addr, ctx->buffers[i].length);
    }
    memset(ctx->buffers, 0, sizeof(ctx->buffers));
    ctx->count = 0;
}

/*
 * 函数名: r528_codec_request_buffers
 * 入参: ctx V4L2 队列上下文
 * 返回值: 0 表示申请并 mmap 成功，负值表示失败
 */
static int r528_codec_request_buffers(struct r528_v4l2_context *ctx)
{
    struct v4l2_requestbuffers req;
    int i;

    memset(&req, 0, sizeof(req));
    req.count = RVT_R528_CODEC_BUFS;
    req.type = ctx->type;
    req.memory = V4L2_MEMORY_MMAP;
    req.mode = V4L2_BUF_MODE_FIFO;

    if (ioctl(decoder_fd, VIDIOC_REQBUFS, &req) < 0)
        return -errno;

    if (req.count <= 0 || req.count > RVT_R528_CODEC_BUFS)
        return -EINVAL;

    ctx->count = req.count;
    for (i = 0; i < ctx->count; i++) {
        struct r528_v4l2_buffer *item = &ctx->buffers[i];

        memset(&item->buf, 0, sizeof(item->buf));
        item->buf.type = ctx->type;
        item->buf.memory = V4L2_MEMORY_MMAP;
        item->buf.index = i;

        if (ioctl(decoder_fd, VIDIOC_QUERYBUF, &item->buf) < 0) {
            r528_codec_unmap_context(ctx);
            return -errno;
        }

        item->length = item->buf.length;
        item->addr = mmap(NULL, item->length, PROT_READ | PROT_WRITE,
                          MAP_SHARED, decoder_fd, item->buf.m.offset);
        if (item->addr == MAP_FAILED) {
            r528_codec_unmap_context(ctx);
            return -errno;
        }

        if (V4L2_TYPE_IS_CAPTURE(ctx->type)) {
            if (ioctl(decoder_fd, VIDIOC_QBUF, &item->buf) < 0) {
                r528_codec_unmap_context(ctx);
                return -errno;
            }
            item->queued = 1;
        }
    }

    return 0;
}

/*
 * 函数名: r528_codec_stream_on
 * 入参: type V4L2 队列类型，stream_flag stream 状态标志
 * 返回值: 0 表示启动成功，负值表示失败
 */
static int r528_codec_stream_on(enum v4l2_buf_type type, int *stream_flag)
{
    if (*stream_flag)
        return 0;

    if (ioctl(decoder_fd, VIDIOC_STREAMON, &type) < 0)
        return -errno;

    *stream_flag = 1;
    return 0;
}

/*
 * 函数名: r528_codec_stream_off
 * 入参: type V4L2 队列类型，stream_flag stream 状态标志
 * 返回值: 无
 */
static void r528_codec_stream_off(enum v4l2_buf_type type, int *stream_flag)
{
    if (*stream_flag) {
        ioctl(decoder_fd, VIDIOC_STREAMOFF, &type);
        *stream_flag = 0;
    }
}

/*
 * 函数名: r528_video_format_from_fourcc
 * 入参: fourcc V4L2 输出格式
 * 返回值: libuapi 显示像素格式
 */
static VideoPixelFormat r528_video_format_from_fourcc(uint32_t fourcc)
{
    if (fourcc == V4L2_PIX_FMT_NV12)
        return VIDEO_PIXEL_FORMAT_NV12;

    return VIDEO_PIXEL_FORMAT_YUV_PLANER_420;
}

/*
 * 函数名: r528_decoder_open
 * 入参: 无
 * 返回值: 0 表示 V4L2 解码器初始化成功，负值表示失败
 */
static int r528_decoder_open(void)
{
    enum v4l2_buf_type output_type;
    enum v4l2_buf_type capture_type;
    int width;
    int height;
    uint32_t output_fourcc;
    int ret;

    if (decoder_ready)
        return 0;

    width = cached_caps.width > 0 ? cached_caps.width : RVT_R528_DEFAULT_WIDTH;
    height = cached_caps.height > 0 ? cached_caps.height : RVT_R528_DEFAULT_HEIGHT;

    decoder_fd = open(CONFIG_RIVOTEK_WIFI_DISPLAY_R528_V4L2_DEV, O_RDWR | O_NONBLOCK);
    if (decoder_fd < 0)
        return -errno;

    ret = r528_decoder_query_types(decoder_fd, &output_type, &capture_type);
    if (ret != 0)
        goto fail;

    output_fourcc = fourcc_supported(decoder_fd, output_type, V4L2_PIX_FMT_MJPEG) ?
                    V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_JPEG;

    output_ctx.type = output_type;
    r528_v4l2_set_pix(&output_ctx.fmt, output_type, output_fourcc,
                      width, height, RVT_R528_MAX_FRAME_SIZE);
    if (ioctl(decoder_fd, VIDIOC_S_FMT, &output_ctx.fmt) < 0) {
        ret = -errno;
        goto fail;
    }

    capture_ctx.type = capture_type;
    r528_v4l2_set_pix(&capture_ctx.fmt, capture_type, V4L2_PIX_FMT_NV12,
                      r528_v4l2_width(&output_ctx.fmt),
                      r528_v4l2_height(&output_ctx.fmt),
                      r528_v4l2_width(&output_ctx.fmt) *
                      r528_v4l2_height(&output_ctx.fmt) * 3 / 2);
    if (ioctl(decoder_fd, VIDIOC_S_FMT, &capture_ctx.fmt) < 0) {
        r528_v4l2_set_pix(&capture_ctx.fmt, capture_type, V4L2_PIX_FMT_YUV420,
                          r528_v4l2_width(&output_ctx.fmt),
                          r528_v4l2_height(&output_ctx.fmt),
                          r528_v4l2_width(&output_ctx.fmt) *
                          r528_v4l2_height(&output_ctx.fmt) * 3 / 2);
        if (ioctl(decoder_fd, VIDIOC_S_FMT, &capture_ctx.fmt) < 0) {
            ret = -errno;
            goto fail;
        }
    }

    ret = r528_codec_request_buffers(&output_ctx);
    if (ret != 0)
        goto fail;

    ret = r528_codec_request_buffers(&capture_ctx);
    if (ret != 0)
        goto fail;

    ret = r528_codec_stream_on(output_ctx.type, &output_stream_on);
    if (ret != 0)
        goto fail;

    ret = r528_codec_stream_on(capture_ctx.type, &capture_stream_on);
    if (ret != 0)
        goto fail;

    memset(&video_param, 0, sizeof(video_param));
    video_param.srcInfo.w = r528_v4l2_width(&capture_ctx.fmt);
    video_param.srcInfo.h = r528_v4l2_height(&capture_ctx.fmt);
    video_param.srcInfo.crop_x = 0;
    video_param.srcInfo.crop_y = 0;
    video_param.srcInfo.crop_w = video_param.srcInfo.w;
    video_param.srcInfo.crop_h = video_param.srcInfo.h;
    video_param.srcInfo.format =
        r528_video_format_from_fourcc(r528_v4l2_pixelformat(&capture_ctx.fmt));
    video_param.srcInfo.color_space =
        video_param.srcInfo.h < 720 ? VIDEO_BT601 : VIDEO_BT709;

    decoder_ready = 1;
    return 0;

fail:
    rvt_display_port_deinit();
    return ret;
}

/*
 * 函数名: r528_decoder_drain_output
 * 入参: 无
 * 返回值: 0 表示已回收可复用输入缓冲，负值表示回收失败
 */
static int r528_decoder_drain_output(void)
{
    struct v4l2_buffer buf;

    while (1) {
        memset(&buf, 0, sizeof(buf));
        buf.type = output_ctx.type;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(decoder_fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN)
                return 0;
            return -errno;
        }

        if (buf.index >= (uint32_t)output_ctx.count)
            return -EINVAL;

        output_ctx.buffers[buf.index].queued = 0;
    }
}

/*
 * 函数名: r528_decoder_get_output_index
 * 入参: 无
 * 返回值: 可提交 JPEG 码流的 V4L2 output buffer 下标，负值表示没有可用缓冲
 */
static int r528_decoder_get_output_index(void)
{
    struct pollfd pfd;
    int i;
    int ret;

    ret = r528_decoder_drain_output();
    if (ret != 0)
        return ret;

    for (i = 0; i < output_ctx.count; i++) {
        if (!output_ctx.buffers[i].queued)
            return i;
    }

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = decoder_fd;
    pfd.events = POLLOUT;
    ret = poll(&pfd, 1, RVT_R528_POLL_TIMEOUT_MS);
    if (ret <= 0)
        return ret == 0 ? -ETIMEDOUT : -errno;

    ret = r528_decoder_drain_output();
    if (ret != 0)
        return ret;

    for (i = 0; i < output_ctx.count; i++) {
        if (!output_ctx.buffers[i].queued)
            return i;
    }

    return -EAGAIN;
}

/*
 * 函数名: r528_decoder_queue_input
 * 入参: data JPEG 数据首地址，len JPEG 数据长度
 * 返回值: 0 表示输入帧入队成功，负值表示失败
 */
static int r528_decoder_queue_input(const uint8_t *data, int len)
{
    struct v4l2_buffer buf;
    int index;

    if (output_ctx.count <= 0)
        return -EINVAL;

    index = r528_decoder_get_output_index();
    if (index < 0)
        return index;

    if (len > (int)output_ctx.buffers[index].length)
        return -EINVAL;

    memcpy(output_ctx.buffers[index].addr, data, len);
    memset(&buf, 0, sizeof(buf));
    buf.type = output_ctx.type;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    buf.bytesused = len;
    buf.length = output_ctx.buffers[index].length;
    if (ioctl(decoder_fd, VIDIOC_QBUF, &buf) < 0)
        return -errno;

    output_ctx.buffers[index].queued = 1;
    return 0;
}

/*
 * 函数名: r528_decoder_render_output
 * 入参: 无
 * 返回值: 0 表示解码帧显示成功，负值表示失败
 */
static int r528_decoder_render_output(void)
{
    struct pollfd pfd;
    struct v4l2_buffer buf;
    struct r528_v4l2_buffer *item;
    int frame_size;
    int ret;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = decoder_fd;
    pfd.events = POLLIN;
    ret = poll(&pfd, 1, RVT_R528_POLL_TIMEOUT_MS);
    if (ret <= 0)
        return ret == 0 ? -ETIMEDOUT : -errno;

    memset(&buf, 0, sizeof(buf));
    buf.type = capture_ctx.type;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(decoder_fd, VIDIOC_DQBUF, &buf) < 0)
        return -errno;

    if (buf.index >= (uint32_t)capture_ctx.count)
        return -EINVAL;

    item = &capture_ctx.buffers[buf.index];
    item->queued = 0;
    frame_size = buf.bytesused > 0 ? (int)buf.bytesused :
                 (int)r528_v4l2_sizeimage(&capture_ctx.fmt);

    ret = video_out->writeData(video_out, item->addr, frame_size, &video_param);

    if (ioctl(decoder_fd, VIDIOC_QBUF, &buf) < 0)
        return -errno;

    item->queued = 1;
    if (ret == 0)
        r528_notify_display_active_once();

    return ret == 0 ? 0 : -1;
}
#endif

#if RVT_R528_HAS_CEDARC
static VideoDecoder *cedarc_decoder;
static struct ScMemOpsS *cedarc_memops;
static int cedarc_ready;
static unsigned int cedarc_decoded_frames;
static unsigned int cedarc_decode_failures;
static unsigned int cedarc_no_picture_frames;
static rvt_thread_t cedarc_decode_tid;
static rvt_mutex_t cedarc_queue_mutex;
static uint8_t *cedarc_queue_buf;
static int cedarc_queue_len;
static int cedarc_queue_pending;
static int cedarc_decode_running;
static int cedarc_decode_busy;
static unsigned int cedarc_queue_frames;
static unsigned int cedarc_queue_replaced;
static int cedarc_stream_width;
static int cedarc_stream_height;

#if RVT_R528_HAS_CEDARC_H264
#define RVT_R528_CEDARC_CODEC_NAME "h264"
#else
#define RVT_R528_CEDARC_CODEC_NAME "mjpeg"
#endif

/*
 * 函数名: r528_h264_first_nal_type
 * 入参: data H264 Annex-B 数据首地址，len 数据长度
 * 返回值: 第一个 NAL type，负值表示未找到起始码
 */
static int r528_h264_first_nal_type(const uint8_t *data, int len)
{
    int i;

    if (!data || len < 5)
        return -EINVAL;

    for (i = 0; i + 4 < len && i < 32; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
            data[i + 2] == 0x01) {
            return data[i + 3] & 0x1f;
        }
        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
            data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            return data[i + 4] & 0x1f;
        }
    }

    return -ENOENT;
}

/*
 * 函数名: r528_jpeg_parse_size
 * 入参: data JPEG 数据首地址，len JPEG 数据长度，width/height 输出宽高
 * 返回值: 0 表示解析成功，负值表示未找到 SOF 尺寸
 */
static int r528_jpeg_parse_size(const uint8_t *data, int len, int *width, int *height)
{
    int pos = 2;

    if (!data || len < 8 || data[0] != 0xff || data[1] != 0xd8)
        return -EINVAL;

    while (pos + 4 < len) {
        int marker;
        int seg_len;

        while (pos < len && data[pos] == 0xff)
            pos++;

        if (pos >= len)
            break;

        marker = data[pos++];
        if (marker == 0xd9 || marker == 0xda)
            break;

        if (pos + 2 > len)
            break;

        seg_len = ((int)data[pos] << 8) | data[pos + 1];
        if (seg_len < 2 || pos + seg_len > len)
            break;

        if ((marker >= 0xc0 && marker <= 0xc3) ||
            (marker >= 0xc5 && marker <= 0xc7) ||
            (marker >= 0xc9 && marker <= 0xcb) ||
            (marker >= 0xcd && marker <= 0xcf)) {
            if (seg_len >= 7) {
                *height = ((int)data[pos + 3] << 8) | data[pos + 4];
                *width = ((int)data[pos + 5] << 8) | data[pos + 6];
                return (*width > 0 && *height > 0) ? 0 : -EINVAL;
            }
            break;
        }

        pos += seg_len;
    }

    return -ENOENT;
}

/*
 * 函数名: r528_cedarc_video_format
 * 入参: pixel_format CedarC 输出像素格式
 * 返回值: libuapi 显示像素格式
 */
static VideoPixelFormat r528_cedarc_video_format(int pixel_format)
{
    switch (pixel_format) {
    case PIXEL_FORMAT_NV12:
        return VIDEO_PIXEL_FORMAT_NV12;
    case PIXEL_FORMAT_NV21:
        return VIDEO_PIXEL_FORMAT_NV21;
    case PIXEL_FORMAT_YV12:
        return VIDEO_PIXEL_FORMAT_YV12;
    case PIXEL_FORMAT_YUV_MB32_420:
        return VIDEO_PIXEL_FORMAT_YUV_MB32_420;
    case PIXEL_FORMAT_YUV_PLANER_420:
    default:
        return VIDEO_PIXEL_FORMAT_YUV_PLANER_420;
    }
}

/*
 * 函数名: r528_cedarc_open
 * 入参: 无
 * 返回值: 0 表示 CedarC 解码器初始化成功，负值表示失败
 */
static int r528_cedarc_open(void)
{
    VConfig vconfig;
    VideoStreamInfo stream_info;
    int width;
    int height;
    int ret;

    if (cedarc_ready)
        return 0;

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: open begin");
    cedarc_memops = MemAdapterGetOpsS();
    if (!cedarc_memops) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: MemAdapterGetOpsS failed");
        return -ENODEV;
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: CdcMemOpen begin");
    ret = CdcMemOpen(cedarc_memops);
    if (ret != 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: CdcMemOpen failed ret=%d", ret);
        cedarc_memops = NULL;
        return -ENODEV;
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: AddVDPlugin begin");
    AddVDPlugin();

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: CreateVideoDecoder begin");
    cedarc_decoder = CreateVideoDecoder();
    if (!cedarc_decoder) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: CreateVideoDecoder failed");
        CdcMemClose(cedarc_memops);
        cedarc_memops = NULL;
        return -ENOMEM;
    }

    width = cedarc_stream_width > 0 ? cedarc_stream_width :
            (cached_caps.width > 0 ? cached_caps.width : RVT_R528_DEFAULT_WIDTH);
    height = cedarc_stream_height > 0 ? cedarc_stream_height :
             (cached_caps.height > 0 ? cached_caps.height : RVT_R528_DEFAULT_HEIGHT);

    memset(&vconfig, 0, sizeof(vconfig));
    vconfig.bDisable3D = 0;
    vconfig.bDispErrorFrame = 0;
    vconfig.bNoBFrames = 0;
    vconfig.bRotationEn = 0;
    vconfig.bScaleDownEn = 0;
    vconfig.nHorizonScaleDownRatio = 0;
    vconfig.nVerticalScaleDownRatio = 0;
    vconfig.eOutputPixelFormat = RVT_R528_HAS_CEDARC_H264 ?
                                  PIXEL_FORMAT_YV12 :
                                  PIXEL_FORMAT_YUV_MB32_420;
    vconfig.nAlignStride = RVT_R528_HAS_CEDARC_H264 ?
                            RVT_R528_CEDARC_STRIDE : 0;
    vconfig.nDeInterlaceHoldingFrameBufferNum = 0;
    vconfig.nDisplayHoldingFrameBufferNum = 0;
    vconfig.nRotateHoldingFrameBufferNum = 0;
    vconfig.nDecodeSmoothFrameBufferNum = RVT_R528_HAS_CEDARC_H264 ?
                                           RVT_R528_CEDARC_SMOOTH : 0;
    vconfig.nVbvBufferSize = RVT_R528_HAS_CEDARC_H264 ?
                              4 * 1024 * 1024 : 2 * 1024 * 1024;
    vconfig.bThumbnailMode = RVT_R528_HAS_CEDARC_H264 ? 0 : 1;
    vconfig.memops = cedarc_memops;

    memset(&stream_info, 0, sizeof(stream_info));
#if RVT_R528_HAS_CEDARC_H264
    stream_info.eCodecFormat = VIDEO_CODEC_FORMAT_H264;
    stream_info.nWidth = width;
    stream_info.nHeight = height;
    stream_info.nFrameRate = RVT_R528_CEDARC_FPS;
    stream_info.bIsFramePackage = 1;
#else
    stream_info.eCodecFormat = VIDEO_CODEC_FORMAT_MJPEG;
#endif

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "r528 cedarc: ve ops normal=%p",
             GetVeOpsS(VE_OPS_TYPE_NORMAL));
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "r528 cedarc: InitializeVideoDecoder begin codec_name=%s codec=%d parsed_wh=%dx%d detected_wh=%dx%d stream_wh=%dx%d fps=%d framepkg=%d pix=%d align=%d smooth=%d thumbnail=%d vbv=%d hold=%d",
             RVT_R528_CEDARC_CODEC_NAME, stream_info.eCodecFormat,
             cedarc_stream_width, cedarc_stream_height, width, height,
             stream_info.nWidth, stream_info.nHeight, stream_info.nFrameRate,
             stream_info.bIsFramePackage, vconfig.eOutputPixelFormat,
             vconfig.nAlignStride, vconfig.nDecodeSmoothFrameBufferNum,
             vconfig.bThumbnailMode, vconfig.nVbvBufferSize,
             vconfig.nDisplayHoldingFrameBufferNum);
    ret = InitializeVideoDecoder(cedarc_decoder, &stream_info, &vconfig);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "r528 cedarc: InitializeVideoDecoder end ret=%d", ret);
    if (ret != 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "r528 cedarc: InitializeVideoDecoder failed ret=%d", ret);
        DestroyVideoDecoder(cedarc_decoder);
        cedarc_decoder = NULL;
        CdcMemClose(cedarc_memops);
        cedarc_memops = NULL;
        return -EIO;
    }

    cedarc_ready = 1;
    cedarc_decoded_frames = 0;
    cedarc_decode_failures = 0;
    cedarc_no_picture_frames = 0;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "r528 cedarc: decoder ready codec=%s output_pix=%d wh=%dx%d",
             RVT_R528_CEDARC_CODEC_NAME, vconfig.eOutputPixelFormat,
             stream_info.nWidth, stream_info.nHeight);
    return 0;
}

/*
 * 函数名: r528_cedarc_close
 * 入参: 无
 * 返回值: 无
 */
static void r528_cedarc_close(void)
{
    if (cedarc_decoder) {
        DestroyVideoDecoder(cedarc_decoder);
        cedarc_decoder = NULL;
    }

    if (cedarc_memops) {
        CdcMemClose(cedarc_memops);
        cedarc_memops = NULL;
    }

    cedarc_ready = 0;
}

/*
 * 函数名: r528_cedarc_render_picture
 * 入参: picture CedarC 解码输出帧
 * 返回值: 0 表示渲染成功，负值表示失败
 */
static int r528_cedarc_render_picture(VideoPicture *picture)
{
    int size;
    int ret;

    if (!picture || !picture->pData0)
        return -EINVAL;

    if (r528_display_open() != 0)
        return -EIO;

    memset(&video_param, 0, sizeof(video_param));
    video_param.srcInfo.w = picture->nWidth;
    video_param.srcInfo.h = picture->nHeight;
    video_param.srcInfo.crop_x = picture->nLeftOffset;
    video_param.srcInfo.crop_y = picture->nTopOffset;
    video_param.srcInfo.crop_w = picture->nRightOffset > picture->nLeftOffset ?
                                 picture->nRightOffset - picture->nLeftOffset :
                                 picture->nWidth;
    video_param.srcInfo.crop_h = picture->nBottomOffset > picture->nTopOffset ?
                                 picture->nBottomOffset - picture->nTopOffset :
                                 picture->nHeight;
    video_param.srcInfo.format = r528_cedarc_video_format(picture->ePixelFormat);
    video_param.srcInfo.color_space = picture->nHeight < 720 ? VIDEO_BT601 : VIDEO_BT709;
    size = picture->nBufSize > 0 ? picture->nBufSize :
           picture->nWidth * picture->nHeight * 3 / 2;

    CdcMemFlushCache(cedarc_memops, picture->pData0, size);
    ret = video_out->writeData(video_out, picture->pData0, size, &video_param);
    if (ret == 0)
        r528_notify_display_active_once();

    if (cedarc_decoded_frames < 5 || (cedarc_decoded_frames % 30) == 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "r528 cedarc: render frame #%u ret=%d pix=%d size=%d wh=%dx%d crop=%ux%u",
                 cedarc_decoded_frames, ret, picture->ePixelFormat, size,
                 picture->nWidth, picture->nHeight,
                 video_param.srcInfo.crop_w, video_param.srcInfo.crop_h);
    }

    return ret == 0 ? 0 : -EIO;
}

/*
 * 函数名: r528_cedarc_decode_render
 * 入参: data 编码帧数据首地址，len 数据长度
 * 返回值: 0 表示解码并渲染成功，负值表示失败
 */
static int r528_cedarc_decode_render(const uint8_t *data, int len)
{
    char *buf = NULL;
    char *ring_buf = NULL;
    int buf_len = 0;
    int ring_buf_len = 0;
    VideoStreamDataInfo data_info;
    VideoPicture *picture;
    int ret;
    int valid_num;

    if (!data || len <= 0)
        return -EINVAL;

    if (!cedarc_ready || !cedarc_decoder) {
#if RVT_R528_HAS_CEDARC_H264
        cedarc_stream_width = cached_caps.width > 0 ?
                              cached_caps.width : RVT_R528_DEFAULT_WIDTH;
        cedarc_stream_height = cached_caps.height > 0 ?
                               cached_caps.height : RVT_R528_DEFAULT_HEIGHT;
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "r528 cedarc: lazy open on first h264 len=%d nal=%d wh=%dx%d first4=%02X%02X%02X%02X",
                 len, r528_h264_first_nal_type(data, len),
                 cedarc_stream_width, cedarc_stream_height,
                 data[0], data[1], data[2], data[3]);
#else
        if (r528_jpeg_parse_size(data, len,
                                 &cedarc_stream_width,
                                 &cedarc_stream_height) != 0) {
            cedarc_stream_width = 0;
            cedarc_stream_height = 0;
        }
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "r528 cedarc: lazy open on first jpeg len=%d wh=%dx%d",
                 len, cedarc_stream_width, cedarc_stream_height);
#endif
        if (r528_cedarc_open() != 0)
            return -EIO;
    }

    ret = RequestVideoStreamBuffer(cedarc_decoder, len, &buf, &buf_len,
                                   &ring_buf, &ring_buf_len, 0);
    if (ret != 0 || buf_len + ring_buf_len < len) {
        cedarc_decode_failures++;
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "r528 cedarc: request bitstream buffer failed ret=%d need=%d got=%d+%d",
                 ret, len, buf_len, ring_buf_len);
        return -EAGAIN;
    }

    if (buf_len >= len) {
        memcpy(buf, data, len);
    } else {
        memcpy(buf, data, buf_len);
        memcpy(ring_buf, data + buf_len, len - buf_len);
    }

    memset(&data_info, 0, sizeof(data_info));
    data_info.pData = buf;
    data_info.nLength = len;
    data_info.bIsFirstPart = 1;
    data_info.bIsLastPart = 1;
    data_info.bValid = 1;

    ret = SubmitVideoStreamData(cedarc_decoder, &data_info, 0);
    if (ret != 0) {
        cedarc_decode_failures++;
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "r528 cedarc: SubmitVideoStreamData failed ret=%d len=%d", ret, len);
        return -EIO;
    }

    ret = DecodeVideoStream(cedarc_decoder, 0, 0, 0, 0);
    if (ret != VDECODE_RESULT_FRAME_DECODED &&
        ret != VDECODE_RESULT_KEYFRAME_DECODED &&
        ret != VDECODE_RESULT_NO_FRAME_BUFFER &&
        ret != VDECODE_RESULT_OK &&
        ret != VDECODE_RESULT_CONTINUE &&
        ret != VDECODE_RESULT_NO_BITSTREAM &&
        ret != VDECODE_RESULT_RESOLUTION_CHANGE) {
        cedarc_decode_failures++;
        if (cedarc_decode_failures <= 5 || (cedarc_decode_failures % 30) == 0) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "r528 cedarc: DecodeVideoStream ret=%d fail=%u len=%d nal=%d",
                     ret, cedarc_decode_failures, len,
                     r528_h264_first_nal_type(data, len));
        }
        return -EIO;
    }

    valid_num = ValidPictureNum(cedarc_decoder, 0);
    if (valid_num <= 0) {
        cedarc_no_picture_frames++;
        if (cedarc_no_picture_frames <= 5 || (cedarc_no_picture_frames % 30) == 0) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "r528 cedarc: no decoded picture #%u ret=%d valid=%d len=%d nal=%d",
                     cedarc_no_picture_frames, ret, valid_num, len,
                     r528_h264_first_nal_type(data, len));
        }
        return -EAGAIN;
    }

    picture = RequestPicture(cedarc_decoder, 0);
    if (!picture) {
        cedarc_decode_failures++;
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: RequestPicture failed");
        return -EAGAIN;
    }

    ret = r528_cedarc_render_picture(picture);
    ReturnPicture(cedarc_decoder, picture);
    if (ret == 0)
        cedarc_decoded_frames++;
    else
        cedarc_decode_failures++;

    return ret;
}

/*
 * 函数名: r528_cedarc_decode_thread
 * 入参: arg 当前未使用
 * 返回值: 无
 */
static void r528_cedarc_decode_thread(void *arg)
{
    uint8_t *local_buf;
    int local_len;
    unsigned int idle_loops = 0;

    (void)arg;

    local_buf = (uint8_t *)rvt_malloc(RVT_R528_MAX_FRAME_SIZE);
    if (!local_buf) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: decode thread no memory");
        cedarc_decode_running = 0;
        cedarc_decode_tid = RVT_NULL;
        return;
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: decode thread started");

    while (cedarc_decode_running) {
        local_len = 0;

        if (cedarc_queue_mutex)
            rvt_mutex_take(cedarc_queue_mutex, RVT_WAIT_FOREVER);

        if (cedarc_queue_pending && cedarc_queue_len > 0) {
            local_len = cedarc_queue_len;
            memcpy(local_buf, cedarc_queue_buf, local_len);
            cedarc_queue_pending = 0;
            idle_loops = 0;
        }

        if (cedarc_queue_mutex)
            rvt_mutex_release(cedarc_queue_mutex);

        if (local_len > 0) {
            cedarc_decode_busy = 1;
            if (cedarc_queue_frames <= 5 || (cedarc_queue_frames % 30) == 0) {
                RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                         "r528 cedarc: decode thread consume frame #%u len=%d",
                         cedarc_queue_frames, local_len);
            }
            r528_cedarc_decode_render(local_buf, local_len);
            cedarc_decode_busy = 0;
            continue;
        }

        idle_loops++;
        if ((idle_loops % 1000) == 0) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "r528 cedarc: decode thread idle pending=%d busy=%d",
                     cedarc_queue_pending, cedarc_decode_busy);
        }
        rvt_thread_mdelay(5);
    }

    rvt_free(local_buf);
    cedarc_decode_tid = RVT_NULL;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: decode thread exit");
}

/*
 * 函数名: r528_cedarc_queue_start
 * 入参: 无
 * 返回值: 0 表示解码线程启动成功，负值表示失败
 */
static int r528_cedarc_queue_start(void)
{
    if (cedarc_decode_tid)
        return 0;

    if (!cedarc_queue_mutex) {
        cedarc_queue_mutex = rvt_mutex_create("r528decq");
        if (!cedarc_queue_mutex) {
            RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: queue mutex create failed");
            return -ENOMEM;
        }
    }

    if (!cedarc_queue_buf) {
        cedarc_queue_buf = (uint8_t *)rvt_malloc(RVT_R528_MAX_FRAME_SIZE);
        if (!cedarc_queue_buf) {
            RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: queue buffer alloc failed");
            return -ENOMEM;
        }
    }

    cedarc_decode_running = 1;
    cedarc_decode_tid = rvt_thread_create("r528dec", r528_cedarc_decode_thread,
                                          RVT_NULL, 16384, 25, 10);
    if (!cedarc_decode_tid || rvt_thread_startup(cedarc_decode_tid) != 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "r528 cedarc: decode thread start failed");
        cedarc_decode_running = 0;
        cedarc_decode_tid = RVT_NULL;
        return -1;
    }

    return 0;
}

/*
 * 函数名: r528_cedarc_queue_frame
 * 入参: data 编码帧数据首地址，len 数据长度
 * 返回值: 0 表示提交成功，负值表示失败
 */
static int r528_cedarc_queue_frame(const uint8_t *data, int len)
{
    if (!data || len <= 0 || len > RVT_R528_MAX_FRAME_SIZE)
        return -EINVAL;

    if (r528_cedarc_queue_start() != 0)
        return -1;

    if (cedarc_queue_mutex)
        rvt_mutex_take(cedarc_queue_mutex, RVT_WAIT_FOREVER);

    if (cedarc_queue_pending)
        cedarc_queue_replaced++;

    memcpy(cedarc_queue_buf, data, len);
    cedarc_queue_len = len;
    cedarc_queue_pending = 1;
    cedarc_queue_frames++;

    if (cedarc_queue_frames <= 5 || (cedarc_queue_frames % 30) == 0 ||
        (cedarc_queue_replaced > 0 && cedarc_queue_replaced <= 5)) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "r528 cedarc: queued %s #%u len=%d nal=%d pending=%d busy=%d replaced=%u",
                 RVT_R528_CEDARC_CODEC_NAME, cedarc_queue_frames, len,
                 r528_h264_first_nal_type(data, len), cedarc_queue_pending,
                 cedarc_decode_busy, cedarc_queue_replaced);
    }

    if (cedarc_queue_mutex)
        rvt_mutex_release(cedarc_queue_mutex);

    return 0;
}
#endif

/*
 * 函数名: probe_v4l2_mjpeg_caps
 * 入参: 无
 * 返回值: 支持的 JPEG/MJPEG 能力掩码，0 表示当前没有可用硬解码设备
 */
static uint32_t probe_v4l2_mjpeg_caps(void)
{
#if RVT_R528_HAS_V4L2_MJPEG
    int fd;
    int ret;

    fd = open(CONFIG_RIVOTEK_WIFI_DISPLAY_R528_V4L2_DEV, O_RDWR | O_NONBLOCK);
    if (fd < 0)
        return 0;

    ret = r528_decoder_probe_fd(fd);
    close(fd);

    return ret == 0 ? RVT_DISPLAY_CODEC_JPEG | RVT_DISPLAY_CODEC_MJPEG : 0;
#else
    return 0;
#endif
}

/*
 * 函数名: probe_cedarc_mjpeg_caps
 * 入参: 无
 * 返回值: 支持的 JPEG/MJPEG 能力掩码，0 表示未编译 CedarC
 */
static uint32_t probe_cedarc_mjpeg_caps(void)
{
#if RVT_R528_HAS_CEDARC
    uint32_t caps = 0;

#if RVT_R528_HAS_CEDARC_MJPEG && !RVT_R528_HAS_CEDARC_H264
    caps |= RVT_DISPLAY_CODEC_JPEG | RVT_DISPLAY_CODEC_MJPEG;
#endif
#if RVT_R528_HAS_CEDARC_H264
    caps |= RVT_DISPLAY_CODEC_H264;
#endif

    return caps;
#else
    return 0;
#endif
}

/*
 * 函数名: probe_fb_caps
 * 入参: caps 用于输出 framebuffer 屏幕尺寸
 * 返回值: 0 表示获取成功，负值表示失败
 */
static int probe_fb_caps(struct rvt_display_caps *caps)
{
#ifdef CONFIG_VIDEO_FB
    struct fb_videoinfo_s vinfo;
    int fd;
    int ret;

    fd = open(RVT_R528_FB_DEV, O_RDWR);
    if (fd < 0)
        return -errno;

    memset(&vinfo, 0, sizeof(vinfo));
    ret = ioctl(fd, FBIOGET_VIDEOINFO, (unsigned long)(uintptr_t)&vinfo);
    close(fd);
    if (ret < 0)
        return -errno;

    caps->width = vinfo.xres;
    caps->height = vinfo.yres;
    return 0;
#else
    (void)caps;
    return -1;
#endif
}

int rvt_display_port_get_caps(struct rvt_display_caps *caps)
{
    if (!caps)
        return -1;

    if (!caps_valid) {
        memset(&cached_caps, 0, sizeof(cached_caps));
        if (probe_fb_caps(&cached_caps) != 0) {
            cached_caps.width = RVT_R528_DEFAULT_WIDTH;
            cached_caps.height = RVT_R528_DEFAULT_HEIGHT;
        }

        cached_caps.codec_mask = probe_v4l2_mjpeg_caps() |
                                 probe_cedarc_mjpeg_caps();
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "r528 display: caps screen=%dx%d codec=0x%lx v4l2=%d cedarc=%d",
                 cached_caps.width, cached_caps.height,
                 (unsigned long)cached_caps.codec_mask,
                 RVT_R528_HAS_V4L2_MJPEG, RVT_R528_HAS_CEDARC);
        caps_valid = 1;
    }

    memcpy(caps, &cached_caps, sizeof(*caps));
    return 0;
}

int rvt_display_port_init(void)
{
    struct rvt_display_caps caps;

    if (display_port_ready)
        return 0;

    if (rvt_display_port_get_caps(&caps) != 0)
        return -1;

    if (caps.codec_mask == 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "r528 display: screen %dx%d, no usable decoder, render disabled",
                 caps.width, caps.height);
        return 0;
    }

#if RVT_R528_HAS_CEDARC
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "r528 display: cedarc enabled codec=%s, decoder init deferred until first frame",
             RVT_R528_CEDARC_CODEC_NAME);
#elif RVT_R528_HAS_V4L2_MJPEG
    if (r528_display_open() != 0 || r528_decoder_open() != 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "r528 display: init decoder/render failed");
        rvt_display_port_deinit();
        return -1;
    }
#endif

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "r528 display: screen %dx%d, codec_mask=0x%lx",
             caps.width, caps.height, (unsigned long)caps.codec_mask);
    display_port_ready = 1;
    return 0;
}

void rvt_display_port_deinit(void)
{
    display_port_ready = 0;
    display_active_notified = 0;
#if RVT_R528_HAS_V4L2_MJPEG
    if (decoder_fd >= 0) {
        r528_codec_stream_off(capture_ctx.type, &capture_stream_on);
        r528_codec_stream_off(output_ctx.type, &output_stream_on);
    }

    r528_codec_unmap_context(&capture_ctx);
    r528_codec_unmap_context(&output_ctx);

    if (decoder_fd >= 0) {
        close(decoder_fd);
        decoder_fd = -1;
    }

    decoder_ready = 0;
#endif
#if RVT_R528_HAS_CEDARC
    cedarc_decode_running = 0;
    cedarc_queue_pending = 0;
    cedarc_queue_len = 0;
    if (cedarc_decode_busy) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "r528 cedarc: deinit while decoder busy, skip close this round");
    } else {
        r528_cedarc_close();
    }
#endif
#if RVT_R528_HAS_V4L2_MJPEG || RVT_R528_HAS_CEDARC
    r528_display_close();
#endif
}

void rvt_display_port_flush(void)
{
}

int rvt_display_port_submit_jpeg(const uint8_t *data, int len)
{
    struct rvt_display_caps caps;

    (void)data;
    (void)len;

    if (rvt_display_port_get_caps(&caps) != 0 || caps.codec_mask == 0) {
        if (!warned_no_decoder) {
            warned_no_decoder = 1;
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "r528 display: frame dropped, decoder capability is unavailable");
        }
        return -1;
    }

#if RVT_R528_HAS_CEDARC
    if (rvt_display_port_init() != 0)
        return -1;

    return r528_cedarc_queue_frame(data, len);
#elif RVT_R528_HAS_V4L2_MJPEG
    if (rvt_display_port_init() != 0)
        return -1;

    if (r528_decoder_queue_input(data, len) != 0)
        return -1;

    return r528_decoder_render_output();
#else
    if (!warned_submit) {
        warned_submit = 1;
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "r528 display: jpeg decoder detected but decode/render queue is not compiled");
    }

    return -1;
#endif
}

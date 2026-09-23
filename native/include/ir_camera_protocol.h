#pragma once
/*
 * Shared-memory frame transport between the IR Camera Bridge app (writer) and the
 * IRCameraBridgeVCam media source (reader), which the Windows Frame Server loads
 * inside its own service process.
 *
 * The READER creates the section: services may create objects in the Global\
 * namespace, while regular user applications may only open them. The media
 * source creates the section with a security descriptor that grants the
 * interactive user read/write access; the app then opens it and publishes
 * frames under a seqlock.
 *
 * The companion publisher mirrors this layout. Bump IRCAM_VERSION on any
 * incompatible change.
 */

#include <stdint.h>

#define IRCAM_FRIENDLY_NAME   L"IR Camera Bridge"
#define IRCAM_CLSID_STRING    L"{19E90705-FD06-4443-9F30-68F97F09FC9E}"
#define IRCAM_SHM_NAME        L"Global\\IRCameraBridgeVCam_v1"
#define IRCAM_STOP_EVENT_NAME L"Local\\IRCameraBridgeVCam_Stop"

#define IRCAM_MAGIC        0x31435657u /* 'WVC1' little-endian */
#define IRCAM_VERSION      1u
#define IRCAM_MAX_WIDTH    1920u
#define IRCAM_MAX_HEIGHT   1080u
#define IRCAM_HEADER_SIZE  64u
#define IRCAM_SHM_SIZE     (IRCAM_HEADER_SIZE + IRCAM_MAX_WIDTH * IRCAM_MAX_HEIGHT * 4u)

enum ir_camera_pixel_format : uint32_t {
    IRCAM_FMT_BGRA = 0,
};

/*
 * Byte offsets inside the shared section (all values little-endian):
 *
 *    0  u32 magic       IRCAM_MAGIC once the writer has published a frame
 *    4  u32 version     IRCAM_VERSION
 *    8  u32 width
 *   12  u32 height
 *   16  u32 fps_num
 *   20  u32 fps_den
 *   24  u32 format      ir_camera_pixel_format
 *   28  u32 seq         seqlock: odd while the writer is mid-update
 *   32  u64 frame_id    increments once per published frame
 *   40  u64 qpc_100ns   writer timestamp in 100 ns units
 *   48  reserved up to offset 64
 *   64  pixel data, width*height*4 bytes (BGRA, top-down rows)
 *
 * Reader algorithm:
 *   1. s1 = seq; if s1 is odd, retry
 *   2. copy header + pixels
 *   3. s2 = seq; accept the copy only if s1 == s2
 */

#pragma pack(push, 4)
typedef struct ir_camera_header {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t format;
    volatile uint32_t seq;
    volatile uint64_t frame_id;
    uint64_t qpc_100ns;
    uint8_t  reserved[16];
} ir_camera_header;
#pragma pack(pop)

static_assert(sizeof(ir_camera_header) == IRCAM_HEADER_SIZE, "header must stay 64 bytes");

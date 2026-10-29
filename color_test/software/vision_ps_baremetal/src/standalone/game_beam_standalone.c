/* Fixed-emitter real-camera application. Standalone main, no menus/levels/KEY. */
#include "game_baremetal_io.h"
#include "game_beam_config.h"
#include "beam_wire_protocol.h"
#include "beam_control_protocol.h"
#include "beam_control_state.h"
#include "beam_uart_log.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xuartps_hw.h"
#include "sleep.h"
#include <stdio.h>
#include <string.h>

static uint8_t cpu_image[1280U*720U*3U] __attribute__((aligned(64)));
/* CPU-only copies, disjoint from every DMA store. Preserve camera pixels and
 * geometry as one frame when the current B observation is still pending. */
static uint8_t committed_image[1280U*720U*3U] __attribute__((aligned(64)));
static uint8_t composed_image[1280U*720U*3U] __attribute__((aligned(64)));
static GameBeam beam;
static GameLinuxDisplay display; /* Existing portable owner; no Linux platform. */
static VisionDetection detections[VISION_MAX_DETECTIONS];
static uint32_t pose_tx;
static BeamUartLog diagnostic_log;
static int defer_logs, log_error;
static BeamControlState control_state;

static int beam_printf(const char *format,...)
{
    int result;
    va_list args;
    va_start(args,format);
    result=defer_logs ? beam_uart_log_vprintf(&diagnostic_log,format,args) : vprintf(format,args);
    va_end(args);
    if (result<0) log_error=1;
    return result;
}
#define printf beam_printf

static int scene_uart_put(unsigned char byte)
{
    if (Xil_In32(STDOUT_BASEADDRESS + XUARTPS_SR_OFFSET) & XUARTPS_SR_TXFULL)
        return 0;
    Xil_Out32(STDOUT_BASEADDRESS + XUARTPS_FIFO_OFFSET, byte);
    return 1;
}
static void service_diagnostics(void)
{
    if (beam_uart_log_poll(&diagnostic_log,scene_uart_put,64U)<0) log_error=1;
}
static int finish_diagnostics(void)
{
    uint64_t started=game_baremetal_now_ms();
    /* Normally drained during capture waits. Finish the last few bytes before
     * v3 packets; never interleave a partial diagnostic and a binary packet. */
    while (diagnostic_log.used) {
        service_diagnostics();
        if (log_error || game_baremetal_now_ms()-started>=200U) return -1;
        if (diagnostic_log.used) usleep(100);
    }
    return log_error ? -1 : 0;
}

/* One complete packet at a time, bounded to 100ms. No pending queue, no logs
 * inserted inside a packet, and no replacement of the capture timestamp.
 * Five objects are 130 bytes (about11.3ms at115200 8N1), including framing. */
static int publish_pose(const GameBeamSnapshot *snapshot)
{
    char line[BEAM_WIRE_MAX_PACKET];
    uint64_t started=game_baremetal_now_ms();
    int offset=0, length=beam_wire_pose_encode(line,sizeof line,snapshot,&control_state.level,pose_tx++,started);
    if (length<0 || finish_diagnostics()) return -1;
    fflush(stdout);
    while (offset<length) {
        if (scene_uart_put((unsigned char)line[offset])) ++offset;
        else {
            if (game_baremetal_now_ms()-started>=100U) return -1;
            usleep(100);
        }
    }
    return 0;
}

static int publish_control_line(const char *line, int length)
{
    char wire[BEAM_WIRE_MAX_PACKET];
    int offset=0;
    uint64_t started=game_baremetal_now_ms();
    if (!line || length<=0 || finish_diagnostics()) return -1;
    length=beam_wire_control_reply(wire,sizeof wire,line,(size_t)length);
    if (length<=0) return -1;
    fflush(stdout);
    while (offset<length) {
        if (scene_uart_put((unsigned char)wire[offset])) ++offset;
        else {
            if (game_baremetal_now_ms()-started>=100U) return -1;
            usleep(100);
        }
    }
    return 0;
}

/* Poll only at frame/packet boundaries. Commands update the emitter through
 * the same beam core; capture identity and DMA ownership remain unchanged. */
static void service_uart_commands(void)
{
    static BeamWireParser parser;
    BeamWirePacket packet;
    unsigned budget=64;
    if (diagnostic_log.used) return; /* respond after the queued line boundary */
    while (budget-- && !(Xil_In32(STDOUT_BASEADDRESS+XUARTPS_SR_OFFSET)&XUARTPS_SR_RXEMPTY)) {
        uint8_t ch=(uint8_t)Xil_In32(STDOUT_BASEADDRESS+XUARTPS_FIFO_OFFSET);
        if (beam_wire_feed(&parser,ch,&packet)) {
            BeamControlMessage message;
            char response[BEAM_CONTROL_MAX_LINE+1U];
            int response_length=-1;
            uint64_t now=game_baremetal_now_ms();
            if (packet.type==BEAM_WIRE_SYNC && packet.length==4U) {
                char wire[BEAM_WIRE_MAX_PACKET];
                int offset=0, n;
                uint64_t sent;
                if (finish_diagnostics()) { log_error=1; return; }
                fflush(stdout);
                sent=game_baremetal_now_ms();
                n=beam_wire_sync_reply(wire,sizeof wire,(uint32_t)beam_wire_le(packet.payload,4),now,sent);
                if (n<0) { log_error=1; return; }
                while (offset<n) {
                    if (scene_uart_put((unsigned char)wire[offset])) ++offset;
                    else if (game_baremetal_now_ms()-sent>=100U) { log_error=1; return; }
                    else usleep(100);
                }
            } else if ((packet.type==BEAM_WIRE_LEVEL || packet.type==BEAM_WIRE_START ||
                        packet.type==BEAM_WIRE_RESET) && beam_wire_control_decode(&packet,&message)) {
                response_length=beam_control_receive(&control_state,&beam,&message,now,
                                                      response,sizeof response);
                if (response_length>0 && publish_control_line(response,response_length)) log_error=1;
            }
        }
    }
}
static struct {
    GameCaptureStamp stamp;
    uint64_t capture_begin, copy_done, detect_done, uart_done, submit_at, submit_done;
    uint64_t render_done, display_begin, display_done;
    uint64_t capture_gap_ms;
    int count, display_status;
    GameBeamStatus status;
} frame_timing;

static long long elapsed(uint64_t end,uint64_t begin)
{
    return begin && end>=begin ? (long long)(end-begin) : -1;
}

static void print_frame_timing(const char *stage)
{
    uint64_t now=game_baremetal_now_ms();
    printf("BEAM_FRAME_DIAGNOSTIC stage=%s status=%s(%d) generation=%llu sequence=%llu captured_ms=%llu submit_at_ms=%llu age_at_detect_ms=%lld age_at_submit_ms=%lld age_now_ms=%lld count=%d\r\n",
        stage,game_beam_status_name(frame_timing.status),(int)frame_timing.status,
        (unsigned long long)frame_timing.stamp.generation,
        (unsigned long long)frame_timing.stamp.sequence,
        (unsigned long long)frame_timing.stamp.captured_ms,
        (unsigned long long)frame_timing.submit_at,
        elapsed(frame_timing.copy_done,frame_timing.stamp.captured_ms),
        elapsed(frame_timing.submit_at,frame_timing.stamp.captured_ms),
        elapsed(now,frame_timing.stamp.captured_ms),frame_timing.count);
    printf("BEAM_FRAME_COST capture_wait_copy_ms=%lld detect_ms=%lld uart_ms=%lld submit_ms=%lld render_ms=%lld display_ms=%lld display_status=%d capture_gap_ms=%llu\r\n",
        elapsed(frame_timing.copy_done,frame_timing.capture_begin),
        elapsed(frame_timing.detect_done,frame_timing.copy_done),
        elapsed(frame_timing.uart_done,frame_timing.submit_done),
        elapsed(frame_timing.submit_done,frame_timing.detect_done),
        elapsed(frame_timing.render_done,frame_timing.uart_done),
        elapsed(frame_timing.display_done,frame_timing.display_begin),frame_timing.display_status,
        (unsigned long long)frame_timing.capture_gap_ms);
}

static void print_detection_diagnostics(const VisionDetectionDiagnostics *d)
{
    printf("BEAM_DETECT sequence=%llu mask_y=%lu mask_b=%lu mask_o=%lu mask_g=%lu mask_mag=%lu mask_black=%lu mask_white=%lu mask_wall=%lu "
           "cand_m=%lu cand_a=%lu cand_b=%lu cand_t=%lu cand_w=%lu "
           "geom_m=%lu geom_a=%lu geom_b=%lu geom_t=%lu geom_w=%lu "
           "mark_m=%lu mark_a=%lu mark_b=%lu mark_t=%lu mark_w=%lu "
           "accept_m=%lu accept_a=%lu accept_b=%lu accept_t=%lu accept_w=%lu "
           "wall_parts=%lu wall_pairs=%lu wall_accept=%lu\r\n",
        (unsigned long long)frame_timing.stamp.sequence,
        (unsigned long)d->mask_pixels[0], (unsigned long)d->mask_pixels[1],
        (unsigned long)d->mask_pixels[2], (unsigned long)d->mask_pixels[3],
        (unsigned long)d->mask_pixels[4], (unsigned long)d->mask_pixels[5],
        (unsigned long)d->mask_pixels[6], (unsigned long)d->mask_pixels[7],
        (unsigned long)d->component_candidates[VISION_MIRROR],
        (unsigned long)d->component_candidates[VISION_PORTAL_A],
        (unsigned long)d->component_candidates[VISION_PORTAL_B],
        (unsigned long)d->component_candidates[VISION_TARGET],
        (unsigned long)d->component_candidates[VISION_WALL],
        (unsigned long)d->geometry_pass[VISION_MIRROR],
        (unsigned long)d->geometry_pass[VISION_PORTAL_A],
        (unsigned long)d->geometry_pass[VISION_PORTAL_B],
        (unsigned long)d->geometry_pass[VISION_TARGET],
        (unsigned long)d->geometry_pass[VISION_WALL],
        (unsigned long)d->marker_pass[VISION_MIRROR],
        (unsigned long)d->marker_pass[VISION_PORTAL_A],
        (unsigned long)d->marker_pass[VISION_PORTAL_B],
        (unsigned long)d->marker_pass[VISION_TARGET],
        (unsigned long)d->marker_pass[VISION_WALL],
        (unsigned long)d->accepted[VISION_MIRROR],
        (unsigned long)d->accepted[VISION_PORTAL_A],
        (unsigned long)d->accepted[VISION_PORTAL_B],
        (unsigned long)d->accepted[VISION_TARGET],
        (unsigned long)d->accepted[VISION_WALL],
        (unsigned long)d->wall_marker_components,
        (unsigned long)d->wall_marker_pairs_tested,
        (unsigned long)d->wall_marker_accepted);
}

int main(void)
{
    GameDisplayPlatform platform;
    GameDisplayMode mode;
    VisionCanvas canvas={composed_image,1280,720,3840,sizeof composed_image};
    const char *failure="prepare";
    unsigned frames=0;
    uint64_t last_log=0;
    uint64_t previous_capture_ms=0;
    GameCaptureStamp committed_stamp={0,0,0};
    int capture_stop,display_stop;
    printf("BEAM_STANDALONE_BEGIN source=(%d,%d) direction=(%d,%d) width=1280 height=720\r\n",
        (int)game_beam_default_config.emitter_point.x,
        (int)game_beam_default_config.emitter_point.y,
        (int)game_beam_default_config.emitter_direction.x,
        (int)game_beam_default_config.emitter_direction.y);
    printf("BEAM_STANDALONE_HARDWARE xsa_sha256=%s\r\n",GAME_BEAM_XSA_SHA256);
    printf("BEAM_DIAGNOSTICS version=14 max_age_ms=%llu age_policy=%s stability=%s support_gap_ms=%u capture_time=START_lower_bound\r\n",
        (unsigned long long)game_beam_default_config.max_age_ms,
        game_beam_default_config.max_age_ms ? "reject_expired" : "disabled_test",
        game_beam_default_config.stability_algorithm==VISION_STABILITY_B ? "B" : "A",
        game_beam_default_config.stability_support_gap_ms);
    printf("BEAM_PROFILE restored_markers no_physical_source no_mirror_portal_strip wall_bars=required portal_b_exit_side=%d display_hold_ms=%llu\r\n",
        game_beam_default_config.portal_b_exit_side,
        (unsigned long long)game_beam_default_config.display_timeout_ms);
    {
        uint32_t control;
        /* Read-only cache evidence. BSP boot.S enables these before main;
         * absence of explicit cache calls in main is not proof of disabled caches. */
        __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(control));
        printf("BEAM_CPU sctlr=%08lx mmu=%u dcache=%u icache=%u\r\n",
            (unsigned long)control,(unsigned)(control&1U),
            (unsigned)((control>>2)&1U),(unsigned)((control>>12)&1U));
    }
    printf("BEAM_POSE protocol=3 confirmed_only=1 uart_baud=115200 clock_sync=SYNC3 object_bytes=12\r\n");
    printf("BEAM_CONTROL protocol=3 commands=LEVEL3,START3,RESET3 ready=READY3 started=STARTED3 pose_gate=START3\r\n");
    printf("BEAM_RELEASE integrated=20261028 portal_profile=low_s_tinted_marks control_protocol=3 wire=COBS_CRC32\r\n");
    printf("BEAM_PIPELINE averaged_shared_HSV=1 hsv_division=exact_reciprocal roi_scan=rowwise display_retirement=overlap_capture two_START=required diagnostics=bounded_queue\r\n");
    printf("BEAM_HOLD frame=last_confirmed_camera_and_ray brightness=constant timeout_ms=%llu missing_commit_ms=%llu timestamps=original\r\n",
        (unsigned long long)game_beam_default_config.display_timeout_ms,
        (unsigned long long)game_beam_default_config.display_dropout_ms);
    printf("BEAM_PORTAL white=local_body_relative_v pose=solid_ring_midpoint_axis entry=finite_long_sides blue_s_min=%u local_white_s_max=%u strict_retry=%u\r\n",
        (unsigned)VISION_RESTORED_BLUE_S_MIN,(unsigned)VISION_RESTORED_PORTAL_WHITE_S_MAX,
        (unsigned)VISION_RESTORED_WHITE_S_MAX);
    game_display_init(&display);
    if (game_baremetal_prepare()) goto failed;
    failure="restored_detector_configuration";
    if (game_beam_default_config.detector_profile != VISION_PROFILE_RESTORED ||
        vision_set_real_scene_profile(game_beam_default_config.detector_profile) ||
        game_beam_init(&beam,&game_beam_default_config,1,game_baremetal_now_ms())!=GAME_BEAM_OK) goto failed;
    game_baremetal_display_platform(&platform);
    game_display_control_720p_mode(&mode);
    failure="display_prepare";
    if (game_display_prepare_continuous(&display,&mode,&platform,&canvas)!=GAME_DISPLAY_OK) goto failed;
    failure="display_start";
    if (game_display_start(&display,500)!=GAME_DISPLAY_OK) goto failed;
    printf("BEAM_HDMI_STARTED buffers=3 stride=5760 span=4147200\r\n");
    failure="capture_start";
    if (game_baremetal_capture_start()) goto failed;
    printf("BEAM_CAPTURE_STARTED\r\n");
    for (;;) {
        GameCaptureStamp stamp;
        GameDetectionFrame frame;
        GameBeamStatus status;
        const GameBeamSnapshot *snapshot;
        uint64_t now;
        GameBeamSnapshot shown;
        VisionDetectionDiagnostics diagnostics;
        const uint8_t *background=cpu_image;
        char age_label[120];
        int count;
        memset(&frame_timing,0,sizeof frame_timing);
        frame_timing.count=-1;
        frame_timing.display_status=-1;
        frame_timing.status=GAME_BEAM_NO_FRAME;
        frame_timing.capture_begin=game_baremetal_now_ms();
        service_uart_commands();
        failure="capture_copy";
        if (game_baremetal_capture_copy(cpu_image,sizeof cpu_image,&stamp,&display,service_diagnostics)) goto failed;
        failure="display_retirement";
        while (display.update_pending) {
            GameDisplayStatus ds=game_display_poll(&display);
            if (ds!=GAME_DISPLAY_OK && ds!=GAME_DISPLAY_BUSY) goto failed;
            if (ds==GAME_DISPLAY_BUSY) { service_diagnostics(); usleep(1000); }
        }
        {
            char response[BEAM_CONTROL_MAX_LINE+1U];
            int n=beam_control_display_retired(&control_state,game_baremetal_now_ms(),
                                               response,sizeof response);
            if (n<0 || (n>0 && publish_control_line(response,n))) goto failed;
        }
        if (log_error) goto failed;
        frame_timing.stamp=stamp;
        frame_timing.capture_gap_ms=previous_capture_ms ? stamp.captured_ms-previous_capture_ms : 0;
        previous_capture_ms=stamp.captured_ms;
        frame_timing.copy_done=game_baremetal_now_ms();
        failure="detect";
        count=vision_detect_bgr888(cpu_image,1280,720,3840,detections,VISION_MAX_DETECTIONS);
        vision_get_detection_diagnostics(&diagnostics);
        frame_timing.detect_done=game_baremetal_now_ms();
        frame_timing.count=count;
        if (count<0) goto failed;
        frame=(GameDetectionFrame){stamp,1,0,detections,count};
        now=game_baremetal_now_ms();
        frame_timing.submit_at=now;
        failure="beam_submit";
        status=game_beam_submit(&beam,&frame,0U,now);
        frame_timing.status=status;
        frame_timing.submit_done=game_baremetal_now_ms();
        if (status==GAME_BEAM_STALE || status==GAME_BEAM_BAD_TIME || status==GAME_BEAM_BAD_ARGUMENT ||
            status==GAME_BEAM_OLD_GENERATION || status==GAME_BEAM_DUPLICATE_FRAME) goto failed;
        failure="beam_render";
        snapshot=game_beam_snapshot(&beam);
        if (!snapshot) goto failed;
        failure="pose_uart";
        if (control_state.pose_enabled && publish_pose(snapshot)) goto failed;
        frame_timing.uart_done=game_baremetal_now_ms();
        if (snapshot->valid) {
            memcpy(committed_image,cpu_image,sizeof cpu_image);
            committed_stamp=stamp;
        }
        now=game_baremetal_now_ms();
        if (game_beam_display_snapshot(&beam,now,&shown)) {
            if (shown.generation!=committed_stamp.generation ||
                shown.sequence!=committed_stamp.sequence ||
                shown.captured_ms!=committed_stamp.captured_ms) goto failed;
            background=committed_image;
        } else {
            shown=*snapshot; /* Current raw frame, no retained geometry. */
            shown.generation=stamp.generation;
            shown.sequence=stamp.sequence;
            shown.captured_ms=stamp.captured_ms;
        }
        memcpy(composed_image,background,sizeof composed_image);
        if (game_beam_render(&shown,&canvas)) goto failed;
        now=game_baremetal_now_ms();
        if (now<stamp.captured_ms) goto failed;
        snprintf(age_label,sizeof age_label,"%s FRAME %llu AGE %llu MS INPUT %s",
            shown.display_stale ? "STALE" : "CAMERA",
            (unsigned long long)shown.sequence,
            (unsigned long long)(now-shown.captured_ms),game_beam_status_name(status));
        vision_canvas_text(&canvas,12,12,age_label,2,0xffb000U);
        frame_timing.render_done=game_baremetal_now_ms();
        failure="pre_display_freshness";
        now=game_baremetal_now_ms();
        if (now<stamp.captured_ms ||
            (beam.config.max_age_ms && now-stamp.captured_ms>beam.config.max_age_ms) ||
            game_baremetal_capture_check()) goto failed;
        failure="display_submit";
        frame_timing.display_begin=game_baremetal_now_ms();
        frame_timing.display_status=(int)game_display_submit(&display,&canvas,500);
        frame_timing.display_done=game_baremetal_now_ms();
        if (frame_timing.display_status!=GAME_DISPLAY_OK) goto failed;
        beam_control_display_submitted(&control_state);
        failure="post_display_freshness";
        now=game_baremetal_now_ms();
        if (now<stamp.captured_ms ||
            (beam.config.max_age_ms && now-stamp.captured_ms>beam.config.max_age_ms) ||
            game_baremetal_capture_check()) goto failed;
        ++frames;
        if (frames==1U) printf("BEAM_FIRST_FRAME sequence=%llu captured_ms=%llu\r\n",
            (unsigned long long)stamp.sequence,(unsigned long long)stamp.captured_ms);
        if (frames==1U || now-last_log>=1000U) {
            fflush(stdout);
            defer_logs=1;
            printf("BEAM_FRAME sequence=%llu age_ms=%llu detections=%d valid=%d beam_status=%d segments=%d raw_mask=%02x supported_mask=%02x pending_mask=%02x restricted_mask=%02x\r\n",
                (unsigned long long)stamp.sequence,(unsigned long long)(now-stamp.captured_ms),
                count,snapshot->valid,(int)status,snapshot->ray.segment_count,
                snapshot->raw_mask,snapshot->supported_mask,snapshot->pending_mask,snapshot->restricted_mask);
            last_log=now;
            printf("BEAM_DISPLAY sequence=%llu captured_ms=%llu stale=%d input_sequence=%llu phase=queued\r\n",
                (unsigned long long)shown.sequence,(unsigned long long)shown.captured_ms,
                shown.display_stale,(unsigned long long)stamp.sequence);
            print_detection_diagnostics(&diagnostics);
            print_frame_timing("submitted");
            defer_logs=0;
        }
    }
failed:
    defer_logs=0;
    game_baremetal_first_fault(failure);
    if (frame_timing.capture_begin) print_frame_timing(failure);
    capture_stop=game_baremetal_capture_stop();
    display_stop=(int)game_display_stop(&display,1000);
    printf("BEAM_SAFE_STOP stage=%s capture_result=%d display_result=%d display_released=%d frames=%u\r\n",
        failure,capture_stop,display_stop,game_display_owner_can_exit(&display),frames);
    /* No exit/reinitialization/retry can reuse uncertain stores. Static RAM,
     * object contexts and clocks needed by an unhalted channel remain alive. */
    for (;;) usleep(1000000);
}

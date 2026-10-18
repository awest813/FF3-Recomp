// Software backend adapter adapted from the repository's MIT VCS backend.
// VCS-specific GPU observers/overlays are disabled for the FF3 software path.
#include "ge_gpu_backend.hpp"
#include "vcs_config.hpp"
#include "vcs_project2dfx.hpp"
#include "vcs_fps_overlay.hpp"
namespace vcs {
const VcsConfiguration &vcs_configuration() { static const VcsConfiguration config{}; return config; }
void fps_overlay_observe_draw(const GeGpuDrawDescriptor &, std::uint32_t) noexcept {}
bool project2dfx_observe_camera_hot(const GeGpuDrawDescriptor &, std::uint32_t, std::uint64_t) noexcept { return true; }
void project2dfx_observe_camera(const std::array<float,12> &, const std::array<float,16> &,
 float,float,float,float,float,float,float,float,float,const GeGpuDrawDescriptor &,std::uint32_t,std::uint64_t) noexcept {}
namespace {
struct Dx12StubState { GeGpuBackendReport report{}; std::uint32_t display_framebuffer{}; };
Dx12StubState &state() { static Dx12StubState s; return s; }
}
bool initialize_ge_gpu_backend(std::string &error) {
    auto &s = state(); s = {};
    s.report.requested = GeGpuBackendKind::Software;
    s.report.active = GeGpuBackendKind::Software;
    s.report.message = "FF3 software GE rasterizer";
    error.clear(); return true;
}
void shutdown_ge_gpu_backend() noexcept { state() = {}; }
bool ge_gpu_backend_active() noexcept { return false; }
bool ge_gpu_backend_transfer_ready() noexcept { return false; }
bool ge_gpu_backend_graphics_ready() noexcept { return false; }
void ge_gpu_backend_record_draw(const GeGpuDrawDescriptor &) noexcept {}
void ge_gpu_backend_observe_camera(const std::array<float, 12> &,
                                   const std::array<float, 16> &,
                                   const std::array<float, 6> &,
                                   const std::array<float, 3> &,
                                   const GeGpuDrawDescriptor &,
                                   std::uint32_t) noexcept {}
bool ge_gpu_backend_stage_vertices(const GeGpuDrawDescriptor &, std::span<const GeGpuVertex>) noexcept { return false; }
bool ge_gpu_backend_texture_needed(const GeGpuDrawDescriptor &) noexcept { return false; }
void ge_gpu_backend_prepare_texture_keys(GeGpuDrawDescriptor &) noexcept {}
bool ge_gpu_backend_texture_signature_needed(const GeGpuDrawDescriptor &) noexcept { return false; }
bool ge_gpu_backend_is_framebuffer_feedback_texture(const GeGpuDrawDescriptor &) noexcept { return false; }
GeGpuWidescreenHud ge_gpu_backend_widescreen_hud(const GeGpuDrawDescriptor &) noexcept { return {}; }
void ge_gpu_backend_note_through_extent(const GeGpuDrawDescriptor &, float, float) noexcept {}
bool ge_gpu_backend_adopt_shared_texture(const GeGpuDrawDescriptor &) noexcept { return false; }
bool ge_gpu_backend_texture_available(const GeGpuDrawDescriptor &) noexcept { return false; }
bool ge_gpu_backend_upload_decoded_texture(const GeGpuDrawDescriptor &, std::uint32_t, std::uint32_t, std::span<const std::byte>) noexcept { return false; }
bool ge_gpu_backend_upload_decoded_texture_chain(const GeGpuDrawDescriptor &, std::span<const GeGpuDecodedMipLevel>) noexcept { return false; }
bool ge_gpu_backend_upload_decoded_texture_chain_packed(const GeGpuDrawDescriptor &, std::uint32_t, std::uint32_t, std::uint32_t, std::vector<std::byte>) noexcept { return false; }
bool ge_gpu_backend_copy_last_texture_rgba(std::span<std::byte>) noexcept { return false; }
void ge_gpu_backend_accumulate_color_triangles(const GeGpuDrawDescriptor &, std::span<const GeGpuVertex>) noexcept {}
void ge_gpu_backend_accumulate_hardware_triangles(const GeGpuDrawDescriptor &, const GeGpuHardwareTransform &, std::span<const GeGpuVertex>, std::span<const std::uint32_t>) noexcept {}
bool ge_gpu_backend_accumulate_hardware_packed_0115(const GeGpuDrawDescriptor &, const GeGpuHardwareTransform &, std::span<const std::byte>, std::uint32_t, std::span<const std::uint32_t>) noexcept { return false; }
void ge_gpu_backend_set_native_window(void *) noexcept {}
void ge_gpu_backend_set_display_framebuffer(std::uint32_t address) noexcept { state().display_framebuffer = address & 0x001FFFF0u; }
bool ge_gpu_backend_finish_color_frame(std::uint64_t) noexcept { return false; }
bool ge_gpu_backend_copy_game_frame_rgba(std::span<std::byte>) noexcept { return false; }
bool ge_gpu_backend_presents_directly() noexcept { return false; }
std::uint32_t ge_gpu_backend_owned_framebuffer() noexcept { return 0u; }
std::uint32_t ge_gpu_backend_display_framebuffer() noexcept { return state().display_framebuffer; }
std::span<const std::byte> ge_gpu_backend_game_frame_rgba() noexcept { return {}; }
bool ge_gpu_backend_copy_offscreen_rgba(std::span<std::byte>) noexcept { return false; }
void ge_gpu_backend_mark_window_presented() noexcept {}
GeGpuBackendReport ge_gpu_backend_report() { return state().report; }

}

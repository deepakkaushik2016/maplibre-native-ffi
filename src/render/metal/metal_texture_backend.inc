#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <mln/gfx/headless_backend.hpp>
#include <mln/gfx/texture2d.hpp>
#include <mln/mtl/headless_backend.hpp>

#include <Metal/MTLCommandBuffer.hpp>
#include <Metal/MTLCommandQueue.hpp>
#include <Metal/MTLDevice.hpp>
#include <Metal/MTLEvent.hpp>
#include <Metal/MTLTexture.hpp>

#include "render/render_session_common.hpp"

namespace mln::mtl {
class Context;
}  // namespace mln::mtl

namespace mln::core {

class MetalTextureBackend final : public mln::mtl::RendererBackend,
                                  public mln::gfx::HeadlessBackend {
 private:
  class MetalTextureRenderableResource;

 public:
  struct CommandBufferFailure {
    // An MTLCommandBufferError, or 0 when Metal reported no error object.
    NS::Integer code = 0;
    std::string description;
  };

  // A null host_queue makes the backend create its own queue on the device.
  // With io_surfaces, each slot renders into a texture over an IOSurface of
  // its own, which the texture's iosurface() names.
  MetalTextureBackend(
    MTL::Device* host_device, MTL::CommandQueue* host_queue, mln::Size size,
    std::size_t ring_depth, MTL::PixelFormat pixel_format,
    bool io_surfaces = false
  );
  MetalTextureBackend(
    std::vector<MTL::Texture*> borrowed_textures, MTL::CommandQueue* host_queue,
    mln::Size size
  );
  MetalTextureBackend(const MetalTextureBackend&) = delete;
  auto operator=(const MetalTextureBackend&) -> MetalTextureBackend& = delete;
  MetalTextureBackend(MetalTextureBackend&&) = delete;
  auto operator=(MetalTextureBackend&&) -> MetalTextureBackend& = delete;
  ~MetalTextureBackend() override;

  auto getDefaultRenderable() -> mln::gfx::Renderable& override;
  auto readStillImage() -> mln::PremultipliedImage override;
  auto getRendererBackend() -> mln::gfx::RendererBackend* override;
  void activate() override;
  void deactivate() override;

  auto metal_texture() -> MTL::Texture*;
  // Selects how each frame is handed over. Any kind but CPU completion makes
  // a frame acquirable once committed rather than once complete.
  void set_producer_sync_kind(mln_gpu_sync_kind kind) {
    producer_sync_kind_ = kind;
  }
  // The synchronization of the last frame committed.
  [[nodiscard]] auto producer_sync() const -> mln_gpu_sync;
  // Makes the next frame that renders into `slot`, whose texture the host
  // reads until `event` reaches `value`, wait for it on the GPU.
  void wait_event(MTL::SharedEvent* event, uint64_t value, std::size_t slot);
  // Waits for every frame still in flight, and records their results.
  void finish_frame();
  // Readies the selected slot for the frame `buffer` encodes: waits for the
  // last frame that rendered into the slot, if the GPU still runs it (and so
  // for the frames before it), and encodes the host's waits for the slot. A
  // frame into another slot does not wait, so the CPU encodes the next frame
  // while the GPU draws the last one.
  void begin_slot_frame(MTL::CommandBuffer& buffer);
  auto select_slot(std::size_t slot) -> bool;
  // Drops what a frame that never committed left in the selected slot's
  // renderable. Runs before each render.
  void discard_unfinished_frame();
  // The depth and stencil attachments every slot renders with, made for
  // `size` when the ring has none of that size. No stencil on the iOS
  // simulator, whose depth format holds it.
  struct DepthStencil {
    std::shared_ptr<mln::gfx::Texture2D> depth;
    std::shared_ptr<mln::gfx::Texture2D> stencil;
    mln::Size size;
  };
  auto depth_stencil(mln::mtl::Context& context, mln::Size size)
    -> const DepthStencil&;
  void set_ring_size(mln::Size size);

  [[nodiscard]] auto has_device(const MTL::Device* other) const -> bool;
  // Whether a replacement texture has the pixel format this session's cached
  // render pipeline states were built against.
  [[nodiscard]] auto has_borrowed_pixel_format(MTL::PixelFormat format) const
    -> bool;
  void set_borrowed_textures(
    std::vector<MTL::Texture*> textures, mln::Size new_size
  );
  void release_borrowed_textures();
  // The failure of the last command buffer a render submitted, if it failed,
  // which the next call no longer returns.
  auto take_command_buffer_failure() -> std::optional<CommandBufferFailure>;

 private:
  void record_command_buffer_result(const MTL::CommandBuffer& buffer);
  void commit(NS::SharedPtr<MTL::CommandBuffer> buffer);
  // Records the results of the frames the GPU has finished, oldest first,
  // without waiting.
  void retire_finished_frames();
  // Waits for the first `count` frames in flight, oldest first.
  void wait_for_frames(std::size_t count);
  // Moves the host's waits for slots onto the next frame, whatever its slot,
  // for a retarget that renumbers the slots.
  void keep_waits_for_next_frame();

  // The caller's texture for each slot, or empty for an offscreen session.
  std::vector<MTL::Texture*> borrowed_textures_;
  // Recorded when this session took borrowed_textures_, which it does not
  // retain and the host may already have released.
  MTL::PixelFormat borrowed_pixel_format_ = MTL::PixelFormatInvalid;
  MTL::PixelFormat owned_pixel_format_ = MTL::PixelFormatInvalid;
  bool io_surfaces_ = false;
  RenderableSlotRing ring_;
  DepthStencil depth_stencil_;
  std::optional<CommandBufferFailure> command_buffer_failure_;
  mln_gpu_sync_kind producer_sync_kind_ = MLN_GPU_SYNC_CPU_COMPLETE;
  // The frames committed and not yet seen finished, oldest first, with the
  // slot each rendered into. A frame of every slot can be in flight at once.
  struct FrameInFlight {
    std::size_t slot = 0;
    NS::SharedPtr<MTL::CommandBuffer> buffer;
  };
  std::vector<FrameInFlight> in_flight_;
  // The producer event, made when the first frame signals it, and the value
  // its last frame signaled.
  NS::SharedPtr<MTL::SharedEvent> event_;
  uint64_t event_value_ = 0;
  // Consumer events that the next frame into `slot` waits for: the host reads
  // that slot's texture until the event signals. A wait for any_slot, left by
  // a retarget, applies to the next frame whatever its slot.
  static constexpr std::size_t any_slot = SIZE_MAX;
  struct EventWait {
    NS::SharedPtr<MTL::SharedEvent> event;
    uint64_t value = 0;
    std::size_t slot = any_slot;
  };
  std::vector<EventWait> event_waits_;
};

}  // namespace mln::core

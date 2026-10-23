#include "TestDevice.h"

#include "VulkanDevice.h"

#include <sonnet/core/Log.h>

#include <catch2/catch_message.hpp>

#include <array>
#include <chrono>

using namespace sonnet::rhi;

// Exercise the Vulkan entry points directly: no render graph, frame-slot reuse or timestamp
// conversion can explain a failure here. Keep this opt-in while the SDK KosmicKrisp driver
// fails it; run it explicitly when qualifying a replacement driver.
TEST_CASE("raw timestamps survive render and compute encoder boundaries", "[.timestamp-diagnostic]") {
  test::TestDevice fixture;
  if (!fixture->info().timestampsSupported) {
    SKIP("device has no timestamp support");
  }
  auto &backend = static_cast<VulkanDevice &>(*fixture);
  const auto &device = backend.device();
  const auto *vk = device.getDispatcher();
  constexpr std::uint32_t queryCount = 44;
  const vk::raii::QueryPool queries{device, {{}, vk::QueryType::eTimestamp, queryCount}};
  const vk::raii::CommandPool pool{device, {{}, backend.graphicsFamily()}};
  const vk::raii::CommandBuffers buffers{device, {*pool, vk::CommandBufferLevel::ePrimary, 1}};
  const auto command = *buffers.front();
  const ImageHandle target = fixture->createImage({.size = {32, 32},
                                                   .format = Format::R8G8B8A8Unorm,
                                                   .usage = ImageUsage::ColorAttachment,
                                                   .debugName = "timestamp boundary"});
  const auto &image = backend.getImage(target);
  buffers.front().begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
  vk->vkCmdResetQueryPool(command, *queries, 0, queryCount);
  const auto stamp = [&](std::uint32_t index) {
    vk->vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, *queries, index);
  };
  for (std::uint32_t i = 0; i < 34; ++i) {
    stamp(i);
  }
  const VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                      nullptr,
                                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                      VK_ACCESS_2_MEMORY_WRITE_BIT,
                                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                      VK_IMAGE_LAYOUT_UNDEFINED,
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      VK_QUEUE_FAMILY_IGNORED,
                                      VK_QUEUE_FAMILY_IGNORED,
                                      image.image,
                                      {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
  const VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &barrier};
  vk->vkCmdPipelineBarrier2(command, &dependency);
  for (std::uint32_t pass = 0; pass < 3; ++pass) {
    const VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                               nullptr,
                                               *image.view,
                                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                               VK_RESOLVE_MODE_NONE,
                                               VK_NULL_HANDLE,
                                               VK_IMAGE_LAYOUT_UNDEFINED,
                                               VK_ATTACHMENT_LOAD_OP_CLEAR,
                                               VK_ATTACHMENT_STORE_OP_STORE,
                                               {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}}};
    const VkRenderingInfo rendering{
        VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, {{0, 0}, {32, 32}}, 1, 0, 1, &attachment, nullptr, nullptr};
    vk->vkCmdBeginRendering(command, &rendering);
    stamp(34 + pass * 3);
    vk->vkCmdEndRendering(command);
    stamp(35 + pass * 3);
    stamp(36 + pass * 3);
    vk->vkCmdPipelineBarrier2(command, &dependency);
  }
  stamp(43);
  buffers.front().end();
  const VkCommandBuffer rawCommand = command;
  const VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &rawCommand, 0, nullptr};
  const auto start = std::chrono::steady_clock::now();
  REQUIRE(vk->vkQueueSubmit(*backend.graphicsQueue(), 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS);
  REQUIRE(vk->vkQueueWaitIdle(*backend.graphicsQueue()) == VK_SUCCESS);
  const double hostNanoseconds =
      std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
  std::array<std::array<std::uint64_t, 2>, queryCount> raw{};
  const VkResult result =
      vk->vkGetQueryPoolResults(*device, *queries, 0, queryCount, sizeof(raw), raw.data(), sizeof(raw.front()),
                                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  const double period = static_cast<double>(backend.physicalDevice().getProperties().limits.timestampPeriod);
  SONNET_LOG_INFO("driver={} {}; period_ns={}; host_ns={}; result={}", fixture->info().driverName,
                  fixture->info().driverInfo, period, hostNanoseconds, static_cast<int>(result));
  for (std::uint32_t i = 0; i < queryCount; ++i) {
    SONNET_LOG_INFO("query={} ticks={} available={}", i, raw[i][0], raw[i][1]);
  }
  CHECK(result == VK_SUCCESS);
  for (std::uint32_t i = 0; i < queryCount; ++i) {
    CAPTURE(i, raw[i][0], raw[i][1]);
    CHECK(raw[i][1] != 0);
    if (i > 0 && raw[i - 1][1] != 0 && raw[i][1] != 0) {
      CHECK(raw[i][0] >= raw[i - 1][0]);
      if (raw[i][0] >= raw[i - 1][0]) {
        CHECK(static_cast<double>(raw[i][0] - raw[i - 1][0]) * period <= hostNanoseconds + period);
      }
    }
  }
  fixture->destroyImage(target);
}

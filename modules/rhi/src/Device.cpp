#include <sonnet/rhi/Device.h>

#if SONNET_RHI_VULKAN
#include "VulkanDevice.h"
#else
#error "SONNET_RHI selected an implementation that does not exist"
#endif

namespace sonnet::rhi {

std::unique_ptr<IDevice> createDevice(const DeviceDesc &desc) {
#if SONNET_RHI_VULKAN
  return std::make_unique<VulkanDevice>(desc);
#endif
}

} // namespace sonnet::rhi

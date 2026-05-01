#pragma once

#include <memory>

namespace sonnet::renderer {

class IRendererBackend;

std::unique_ptr<IRendererBackend> createVulkanBackend();

} // namespace sonnet::renderer

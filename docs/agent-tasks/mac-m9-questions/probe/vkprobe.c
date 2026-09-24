// A throwaway probe for ADR-0018's open questions 4 and 5. It loads Vulkan the way the player
// will on Apple platforms, SDL3 finding a statically linked MoltenVK in the process, and prints
// every feature and limit the engine's device selection requires (modules/rhi/src/VulkanDevice.cpp,
// modules/rhi/include/sonnet/rhi/Types.h). Anything short of the engine's needs is marked MISSING.
// The same text goes to the log and to vkprobe.txt in SDL's pref path.
#define _GNU_SOURCE // dladdr on glibc, for the Linux check of this probe
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static FILE *g_file = NULL;
static int g_missing = 0;

static void out(const char *format, ...) {
  char line[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  SDL_Log("%s", line);
  if (g_file) {
    fprintf(g_file, "%s\n", line);
    fflush(g_file);
  }
}

static void feature(const char *name, VkBool32 value) {
  out("  %-48s %s", name, value ? "yes" : "no   MISSING");
  g_missing += value ? 0 : 1;
}

static void info(const char *name, VkBool32 value) {
  out("  %-48s %s", name, value ? "yes" : "no");
}

static void limit(const char *name, uint64_t value, uint64_t needed) {
  out("  %-48s %llu (needs >= %llu)%s", name, (unsigned long long)value, (unsigned long long)needed,
      value >= needed ? "" : "   MISSING");
  g_missing += value >= needed ? 0 : 1;
}

// The bindless set: sampled images, cubes and depth images are all sampled-image descriptors in set 0.
enum {
  SampledImages = 4096 + 64 + 64,
  StorageBuffers = 4096,
  StorageImages = 512,
  Samplers = 64 + 8,
};

static void probeFormat(VkPhysicalDevice device, const char *name, VkFormat format) {
  VkFormatProperties properties;
  vkGetPhysicalDeviceFormatProperties(device, format, &properties);
  const VkFormatFeatureFlags wanted = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                      VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  out("  %-48s %s", name, (properties.optimalTilingFeatures & wanted) == wanted ? "sampled, linear filter, transfer dst" : "not usable");
}

static void probeDevice(VkPhysicalDevice device) {
  VkPhysicalDeviceDriverProperties driver = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
  VkPhysicalDeviceVulkan12Properties properties12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
                                                     .pNext = &driver};
  VkPhysicalDeviceVulkan14Properties properties14 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES,
                                                     .pNext = &properties12};
  VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &properties14};
  vkGetPhysicalDeviceProperties2(device, &properties);
  const VkPhysicalDeviceLimits *limits = &properties.properties.limits;
  const uint32_t api = properties.properties.apiVersion;
  out("device: %s", properties.properties.deviceName);
  out("  apiVersion %u.%u.%u, driver %s (%s)", VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api), VK_API_VERSION_PATCH(api),
      driver.driverName, driver.driverInfo);
  if (api < VK_API_VERSION_1_4) {
    out("  apiVersion below 1.4   MISSING");
    ++g_missing;
    return; // the 1.4 structures below are not defined for this device
  }

  VkPhysicalDeviceVulkan14Features features14 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
  VkPhysicalDeviceVulkan13Features features13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                                 .pNext = &features14};
  VkPhysicalDeviceVulkan12Features features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                                 .pNext = &features13};
  VkPhysicalDeviceVulkan11Features features11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                                                 .pNext = &features12};
  VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &features11};
  vkGetPhysicalDeviceFeatures2(device, &features);
  const VkPhysicalDeviceFeatures *f = &features.features;

  out(" required features");
  feature("samplerAnisotropy", f->samplerAnisotropy);
  feature("multiDrawIndirect", f->multiDrawIndirect);
  feature("drawIndirectFirstInstance", f->drawIndirectFirstInstance);
  feature("shaderDrawParameters", features11.shaderDrawParameters);
  feature("timelineSemaphore", features12.timelineSemaphore);
  feature("bufferDeviceAddress", features12.bufferDeviceAddress);
  feature("scalarBlockLayout", features12.scalarBlockLayout);
  feature("descriptorIndexing", features12.descriptorIndexing);
  feature("shaderSampledImageArrayNonUniformIndexing", features12.shaderSampledImageArrayNonUniformIndexing);
  feature("shaderStorageImageArrayNonUniformIndexing", features12.shaderStorageImageArrayNonUniformIndexing);
  feature("shaderStorageBufferArrayNonUniformIndexing", features12.shaderStorageBufferArrayNonUniformIndexing);
  feature("descriptorBindingPartiallyBound", features12.descriptorBindingPartiallyBound);
  feature("descriptorBindingSampledImageUpdateAfterBind", features12.descriptorBindingSampledImageUpdateAfterBind);
  feature("descriptorBindingStorageImageUpdateAfterBind", features12.descriptorBindingStorageImageUpdateAfterBind);
  feature("descriptorBindingStorageBufferUpdateAfterBind", features12.descriptorBindingStorageBufferUpdateAfterBind);
  feature("descriptorBindingUpdateUnusedWhilePending", features12.descriptorBindingUpdateUnusedWhilePending);
  feature("runtimeDescriptorArray", features12.runtimeDescriptorArray);
  feature("dynamicRendering", features13.dynamicRendering);
  feature("synchronization2", features13.synchronization2);
  feature("shaderDemoteToHelperInvocation", features13.shaderDemoteToHelperInvocation);
  feature("pushDescriptor", features14.pushDescriptor);
  feature("dynamicRenderingLocalRead", features14.dynamicRenderingLocalRead);
  feature("maintenance5", features14.maintenance5);
  feature("maintenance6", features14.maintenance6);

  out(" optional features");
  info("textureCompressionASTC_LDR", f->textureCompressionASTC_LDR);
  info("textureCompressionBC", f->textureCompressionBC);
  info("textureCompressionETC2", f->textureCompressionETC2);
  info("drawIndirectCount", features12.drawIndirectCount);
  info("hostImageCopy", features14.hostImageCopy);

  out(" limits");
  limit("maxBoundDescriptorSets", limits->maxBoundDescriptorSets, 2);
  limit("maxPushConstantsSize", limits->maxPushConstantsSize, 128);
  limit("maxDescriptorSetUpdateAfterBindSampledImages", properties12.maxDescriptorSetUpdateAfterBindSampledImages, SampledImages);
  limit("maxDescriptorSetUpdateAfterBindStorageBuffers", properties12.maxDescriptorSetUpdateAfterBindStorageBuffers, StorageBuffers);
  limit("maxDescriptorSetUpdateAfterBindStorageImages", properties12.maxDescriptorSetUpdateAfterBindStorageImages, StorageImages);
  limit("maxDescriptorSetUpdateAfterBindSamplers", properties12.maxDescriptorSetUpdateAfterBindSamplers, Samplers);
  limit("maxPerStageDescriptorUpdateAfterBindSampledImages", properties12.maxPerStageDescriptorUpdateAfterBindSampledImages,
        SampledImages);
  limit("maxPerStageDescriptorUpdateAfterBindStorageBuffers", properties12.maxPerStageDescriptorUpdateAfterBindStorageBuffers,
        StorageBuffers);
  limit("maxPerStageDescriptorUpdateAfterBindStorageImages", properties12.maxPerStageDescriptorUpdateAfterBindStorageImages,
        StorageImages);
  limit("maxPerStageDescriptorUpdateAfterBindSamplers", properties12.maxPerStageDescriptorUpdateAfterBindSamplers, Samplers);
  limit("maxPerStageUpdateAfterBindResources", properties12.maxPerStageUpdateAfterBindResources,
        SampledImages + StorageBuffers + StorageImages + Samplers);
  limit("maxPushDescriptors", properties14.maxPushDescriptors, 3);
  out("  %-48s %u", "maxDrawIndirectCount", limits->maxDrawIndirectCount);
  out("  %-48s %u", "maxImageDimension2D", limits->maxImageDimension2D);
  out("  %-48s %u", "maxComputeWorkGroupInvocations", limits->maxComputeWorkGroupInvocations);
  out("  %-48s %.3f ns", "timestampPeriod", limits->timestampPeriod);

  out(" formats (optimal tiling)");
  probeFormat(device, "ASTC_4x4_UNORM_BLOCK", VK_FORMAT_ASTC_4x4_UNORM_BLOCK);
  probeFormat(device, "ASTC_4x4_SRGB_BLOCK", VK_FORMAT_ASTC_4x4_SRGB_BLOCK);
  probeFormat(device, "ASTC_6x6_UNORM_BLOCK", VK_FORMAT_ASTC_6x6_UNORM_BLOCK);
  probeFormat(device, "ASTC_6x6_SRGB_BLOCK", VK_FORMAT_ASTC_6x6_SRGB_BLOCK);
  probeFormat(device, "BC7_SRGB_BLOCK", VK_FORMAT_BC7_SRGB_BLOCK);
  probeFormat(device, "D32_SFLOAT (as depth attachment)", VK_FORMAT_D32_SFLOAT);
}

static bool hasExtension(const VkExtensionProperties *extensions, uint32_t count, const char *name) {
  for (uint32_t i = 0; i < count; ++i) {
    if (strcmp(extensions[i].extensionName, name) == 0) {
      return true;
    }
  }
  return false;
}

static int run(int argc, char **argv) {
  const int version = SDL_GetVersion();
  out("vkprobe: SDL %d.%d.%d on %s", SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
      SDL_VERSIONNUM_MICRO(version), SDL_GetPlatform());
  // Q9: whether a launcher passes arguments through to the app.
  for (int i = 1; i < argc; ++i) {
    out("argument %d: %s", i, argv[i]);
  }

  // Q4: is vkGetInstanceProcAddr in the process before SDL looks for a library?
  void *inProcess = dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr");
  out("dlsym(RTLD_DEFAULT, vkGetInstanceProcAddr) = %p", inProcess);

  SDL_Window *window = SDL_CreateWindow("vkprobe", 640, 480, SDL_WINDOW_VULKAN);
  if (!window) {
    out("SDL_CreateWindow failed: %s   MISSING", SDL_GetError());
    return 1;
  }
  PFN_vkGetInstanceProcAddr getInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
  out("SDL_Vulkan_GetVkGetInstanceProcAddr() = %p (%s the in-process symbol)", (void *)getInstanceProcAddr,
      (void *)getInstanceProcAddr == inProcess ? "is" : "is NOT");

  // Q4: what Platform's loader pinning does (modules/platform/src/Platform.cpp): dladdr, then dlopen.
  Dl_info where;
  if (dladdr((void *)getInstanceProcAddr, &where) && where.dli_fname) {
    void *pinned = dlopen(where.dli_fname, RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    out("dladdr -> %s; dlopen(RTLD_NOLOAD) = %p", where.dli_fname, pinned);
  } else {
    out("dladdr found no image for the entry point");
  }

  uint32_t apiVersion = 0;
  vkEnumerateInstanceVersion(&apiVersion);
  out("instance version %u.%u.%u", VK_API_VERSION_MAJOR(apiVersion), VK_API_VERSION_MINOR(apiVersion),
      VK_API_VERSION_PATCH(apiVersion));

  uint32_t extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
  VkExtensionProperties extensions[256];
  extensionCount = extensionCount > 256 ? 256 : extensionCount;
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, extensions);
  out("instance extensions (%u):", extensionCount);
  for (uint32_t i = 0; i < extensionCount; ++i) {
    out("  %s", extensions[i].extensionName);
  }

  uint32_t sdlCount = 0;
  const char *const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlCount);
  const char *enabled[16];
  uint32_t enabledCount = 0;
  for (uint32_t i = 0; i < sdlCount && enabledCount < 15; ++i) {
    enabled[enabledCount++] = sdlExtensions[i];
  }
  VkInstanceCreateFlags flags = 0;
  if (hasExtension(extensions, extensionCount, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    enabled[enabledCount++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
  out("portability enumeration offered: %s", flags ? "yes" : "no");

  const VkApplicationInfo application = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                         .pApplicationName = "vkprobe",
                                         .apiVersion = VK_API_VERSION_1_4};
  const VkInstanceCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                           .flags = flags,
                                           .pApplicationInfo = &application,
                                           .enabledExtensionCount = enabledCount,
                                           .ppEnabledExtensionNames = enabled};
  VkInstance instance = VK_NULL_HANDLE;
  const VkResult created = vkCreateInstance(&createInfo, NULL, &instance);
  out("vkCreateInstance = %d", (int)created);
  if (created != VK_SUCCESS) {
    ++g_missing;
    return 1;
  }

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  out("SDL_Vulkan_CreateSurface: %s", SDL_Vulkan_CreateSurface(window, instance, NULL, &surface) ? "ok" : SDL_GetError());

  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
  VkPhysicalDevice devices[8];
  deviceCount = deviceCount > 8 ? 8 : deviceCount;
  vkEnumeratePhysicalDevices(instance, &deviceCount, devices);
  out("physical devices: %u", deviceCount);
  for (uint32_t i = 0; i < deviceCount; ++i) {
    probeDevice(devices[i]);
  }

  if (surface) {
    SDL_Vulkan_DestroySurface(instance, surface, NULL);
  }
  vkDestroyInstance(instance, NULL);
  SDL_DestroyWindow(window);
  return 0;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
  (void)appstate;
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  char *pref = SDL_GetPrefPath("sonnet", "vkprobe");
  if (pref) {
    char path[1024];
    snprintf(path, sizeof path, "%svkprobe.txt", pref);
    g_file = fopen(path, "w");
    SDL_Log("writing %s", path);
    SDL_free(pref);
  }
  const int failed = run(argc, argv);
  out("result: %s, %d MISSING", failed || g_missing ? "FAIL" : "PASS", g_missing);
  if (g_file) {
    fclose(g_file);
  }
  return failed || g_missing ? SDL_APP_FAILURE : SDL_APP_SUCCESS;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  (void)appstate;
  return SDL_APP_SUCCESS;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  (void)appstate;
  (void)event;
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  (void)appstate;
  (void)result;
}

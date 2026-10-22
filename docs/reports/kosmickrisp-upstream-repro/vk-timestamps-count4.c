// Minimal Vulkan reproduction: N consecutive vkCmdWriteTimestamp2 calls in one
// command buffer, then read the raw ticks and availability.
// Build: cc vk-timestamps.c -o vk-timestamps -lvulkan   (macOS: link the loader from the SDK)
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define COUNT 4
#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s failed: %d\n", #x, r_); return 2; } } while (0)

int main(void) {
  const char *portability = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
  uint32_t extCount = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &extCount, NULL);
  VkExtensionProperties *exts = calloc(extCount ? extCount : 1, sizeof *exts);
  vkEnumerateInstanceExtensionProperties(NULL, &extCount, exts);
  int hasPortability = 0;
  for (uint32_t i = 0; i < extCount; ++i)
    if (!__builtin_strcmp(exts[i].extensionName, portability)) hasPortability = 1;

  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
  VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
  if (hasPortability) {
    ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    ici.enabledExtensionCount = 1;
    ici.ppEnabledExtensionNames = &portability;
  }
  VkInstance instance;
  CHECK(vkCreateInstance(&ici, NULL, &instance));

  uint32_t gpuCount = 1;
  VkPhysicalDevice gpu;
  VkResult enumerate = vkEnumeratePhysicalDevices(instance, &gpuCount, &gpu);
  if ((enumerate != VK_SUCCESS && enumerate != VK_INCOMPLETE) || gpuCount == 0) { fprintf(stderr, "no device\n"); return 2; }
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(gpu, &props);
  printf("device=%s timestampPeriod=%f\n", props.deviceName, props.limits.timestampPeriod);

  uint32_t qfCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qfCount, NULL);
  VkQueueFamilyProperties *qf = calloc(qfCount, sizeof *qf);
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qfCount, qf);
  uint32_t family = UINT32_MAX;
  for (uint32_t i = 0; i < qfCount; ++i)
    if ((qf[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) && qf[i].timestampValidBits) { family = i; break; }
  if (family == UINT32_MAX) { fprintf(stderr, "no queue with timestamps\n"); return 2; }

  float priority = 1.0f;
  VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
  VkPhysicalDeviceVulkan13Features f13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .synchronization2 = VK_TRUE};
  const char *devExts[] = {"VK_KHR_portability_subset"};
  uint32_t devExtCount = 0;
  vkEnumerateDeviceExtensionProperties(gpu, NULL, &devExtCount, NULL);
  VkExtensionProperties *dprops = calloc(devExtCount ? devExtCount : 1, sizeof *dprops);
  vkEnumerateDeviceExtensionProperties(gpu, NULL, &devExtCount, dprops);
  int needSubset = 0;
  for (uint32_t i = 0; i < devExtCount; ++i)
    if (!__builtin_strcmp(dprops[i].extensionName, devExts[0])) needSubset = 1;
  VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f13, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
                            .enabledExtensionCount = (uint32_t)needSubset, .ppEnabledExtensionNames = devExts};
  VkDevice device;
  CHECK(vkCreateDevice(gpu, &dci, NULL, &device));
  VkQueue queue;
  vkGetDeviceQueue(device, family, 0, &queue);

  VkQueryPoolCreateInfo qpi = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = COUNT};
  VkQueryPool pool;
  CHECK(vkCreateQueryPool(device, &qpi, NULL, &pool));
  VkCommandPoolCreateInfo cpi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
  VkCommandPool cmdPool;
  CHECK(vkCreateCommandPool(device, &cpi, NULL, &cmdPool));
  VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
  VkCommandBuffer cmd;
  CHECK(vkAllocateCommandBuffers(device, &cai, &cmd));

  VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  CHECK(vkBeginCommandBuffer(cmd, &bi));
  vkCmdResetQueryPool(cmd, pool, 0, COUNT);
  for (uint32_t i = 0; i < COUNT; ++i)
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool, i);
  CHECK(vkEndCommandBuffer(cmd));
  VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
  CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
  CHECK(vkQueueWaitIdle(queue));

  uint64_t raw[COUNT][2] = {0};
  CHECK(vkGetQueryPoolResults(device, pool, 0, COUNT, sizeof raw, raw, sizeof raw[0], VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
  int bad = 0;
  for (uint32_t i = 0; i < COUNT; ++i) {
    printf("query=%2u ticks=%llu available=%llu\n", i, (unsigned long long)raw[i][0], (unsigned long long)raw[i][1]);
    if (raw[i][0] == 0 && raw[i][1] != 0) bad = 1;
    if (i && raw[i][0] < raw[i - 1][0]) bad = 1;
  }
  puts(bad ? "FAIL: available query returned zero or went backwards" : "PASS");
  return bad;
}

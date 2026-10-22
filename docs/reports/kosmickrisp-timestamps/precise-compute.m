// Diagnostic-only, process-local repair experiment for issue 374; never ship
// this shim.
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
static void (*originalWrite)(id, SEL, MTL4TimestampGranularity,
                             id<MTL4CounterHeap>, NSUInteger);
static void preciseWrite(id self, SEL selector,
                         MTL4TimestampGranularity granularity,
                         id<MTL4CounterHeap> heap, NSUInteger index) {
  (void)granularity;
  originalWrite(self, selector, MTL4TimestampGranularityPrecise, heap, index);
}
static void (*originalCommit)(id, SEL, const id<MTL4CommandBuffer> *,
                              NSUInteger, MTL4CommitOptions *);
static void measuredCommit(id self, SEL selector,
                           const id<MTL4CommandBuffer> *commands,
                           NSUInteger count, MTL4CommitOptions *options) {
  if (options == nil)
    options = [MTL4CommitOptions new];
  [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
    fprintf(
        stderr,
        "METAL start_s=%.9f end_s=%.9f duration_ms=%.6f buffers=%lu error=%s\n",
        feedback.GPUStartTime, feedback.GPUEndTime,
        (feedback.GPUEndTime - feedback.GPUStartTime) * 1000.0,
        (unsigned long)count,
        feedback.error ? feedback.error.description.UTF8String : "none");
  }];
  originalCommit(self, selector, commands, count, options);
}
__attribute__((constructor)) static void install(void) {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    id<MTL4CommandAllocator> allocator = [device newCommandAllocator];
    id<MTL4CommandBuffer> command = [device newCommandBuffer];
    [command beginCommandBufferWithAllocator:allocator];
    id<MTL4ComputeCommandEncoder> encoder = [command computeCommandEncoder];
    SEL selector = @selector(writeTimestampWithGranularity:intoHeap:atIndex:);
    Method method = class_getInstanceMethod([encoder class], selector);
    if (!method) {
      fprintf(stderr, "No Metal 4 compute timestamp method to intercept\n");
      abort();
    }
    originalWrite = (void *)method_setImplementation(method, (IMP)preciseWrite);
    fprintf(stderr, "DIAGNOSTIC: forcing precise Metal compute timestamps\n");
    id<MTL4CommandQueue> queue = [device newMTL4CommandQueue];
    SEL commitSelector = @selector(commit:count:options:);
    Method commitMethod =
        class_getInstanceMethod([queue class], commitSelector);
    if (!commitMethod) {
      fprintf(stderr, "No Metal 4 commit method to intercept\n");
      abort();
    }
    originalCommit =
        (void *)method_setImplementation(commitMethod, (IMP)measuredCommit);
    [encoder endEncoding];
    [command endCommandBuffer];
  }
}

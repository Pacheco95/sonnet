// Independent Metal 4 reproduction for issue 374. No Vulkan or Sonnet code is
// loaded.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdio.h>

int main(void) {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    id<MTL4CommandQueue> queue = [device newMTL4CommandQueue];
    NSError *error = nil;
    id<MTLLibrary> library =
        [device newLibraryWithSource:
                    @"#include <metal_stdlib>\n"
                     "using namespace metal;\n"
                     "kernel void noop(uint3 id [[thread_position_in_grid]]) {}"
                             options:nil
                               error:&error];
    id<MTLComputePipelineState> pipeline = [device
        newComputePipelineStateWithFunction:[library
                                                newFunctionWithName:@"noop"]
                                      error:&error];
    if (!queue || !pipeline) {
      fprintf(stderr, "Metal initialization failed: %s\n",
              error.description.UTF8String);
      return 1;
    }
    for (int precise = 0; precise < 2; ++precise) {
      for (NSUInteger count = 4; count <= 16; count = count == 4 ? 5 : 16) {
        id<MTL4CommandAllocator> allocator = [device newCommandAllocator];
        id<MTL4CommandBuffer> command = [device newCommandBuffer];
        MTL4CounterHeapDescriptor *descriptor = [MTL4CounterHeapDescriptor new];
        descriptor.type = MTL4CounterHeapTypeTimestamp;
        descriptor.count = count;
        id<MTL4CounterHeap> heap =
            [device newCounterHeapWithDescriptor:descriptor error:&error];
        id<MTLSharedEvent> event = [device newSharedEvent];
        if (!allocator || !command || !heap || !event) {
          fprintf(stderr, "Metal resource creation failed: %s\n",
                  error.description.UTF8String);
          return 1;
        }
        [command beginCommandBufferWithAllocator:allocator];
        id<MTL4ComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        for (NSUInteger i = 0; i < count; ++i) {
          [encoder dispatchThreads:MTLSizeMake(1, 1, 1)
              threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
          [encoder writeTimestampWithGranularity:
                       precise ? MTL4TimestampGranularityPrecise
                               : MTL4TimestampGranularityRelaxed
                                        intoHeap:heap
                                         atIndex:i];
        }
        [encoder endEncoding];
        [command endCommandBuffer];
        dispatch_semaphore_t completed = dispatch_semaphore_create(0);
        __block BOOL failed = NO;
        MTL4CommitOptions *options = [MTL4CommitOptions new];
        [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
          failed = feedback.error != nil;
          printf("precise=%d count=%lu gpu_ms=%.6f error=%s\n", precise,
                 (unsigned long)count,
                 (feedback.GPUEndTime - feedback.GPUStartTime) * 1000.0,
                 feedback.error ? feedback.error.description.UTF8String
                                : "none");
          dispatch_semaphore_signal(completed);
        }];
        [queue commit:&command count:1 options:options];
        [queue signalEvent:event value:1];
        if (![event waitUntilSignaledValue:1 timeoutMS:10000] ||
            dispatch_semaphore_wait(
                completed,
                dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) != 0 ||
            failed) {
          return 2;
        }
        NSData *data = [heap resolveCounterRange:NSMakeRange(0, count)];
        if (data.length != count * sizeof(MTL4TimestampHeapEntry)) {
          return 3;
        }
        const MTL4TimestampHeapEntry *entries = data.bytes;
        for (NSUInteger i = 0; i < count; ++i) {
          printf("precise=%d count=%lu query=%lu ticks=%llu\n", precise,
                 (unsigned long)count, (unsigned long)i, entries[i].timestamp);
        }
        if (count == 16) {
          break;
        }
      }
    }
  }
}

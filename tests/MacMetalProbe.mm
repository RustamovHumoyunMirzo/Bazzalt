// Run in a separate process: some virtual Metal drivers advertise a device but
// abort in argument-encoder creation, which Filament requires for descriptors.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstdio>

int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return 1;
        std::fprintf(stderr, "Metal device: %s; argument buffer tier: %lu\n",
                device.name.UTF8String, (unsigned long)device.argumentBuffersSupport);
        auto* argument = [MTLArgumentDescriptor argumentDescriptor];
        argument.index = 0;
        argument.dataType = MTLDataTypePointer;
        argument.access = MTLArgumentAccessReadOnly;
        id<MTLArgumentEncoder> encoder = [device newArgumentEncoderWithArguments:@[argument]];
        if (!encoder) { [device release]; return 2; }
        id<MTLBuffer> arguments = [device newBufferWithLength:encoder.encodedLength
                                                    options:MTLResourceStorageModeShared];
        id<MTLBuffer> value = [device newBufferWithLength:256
                                                options:MTLResourceStorageModeShared];
        if (!arguments || !value) return 3;
        [encoder setArgumentBuffer:arguments offset:0];
        [encoder setBuffer:value offset:0 atIndex:0];
        [value release];
        [arguments release];
        [encoder release];
        [device release];
        return 0;
    }
}

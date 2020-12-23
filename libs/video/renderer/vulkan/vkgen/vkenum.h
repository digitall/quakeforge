#ifndef __renderer_vulkan_vkgen_vkenum_h
#define __renderer_vulkan_vkgen_vkenum_h

#include "vktype.h"

@class PLItem;

@interface Enum: Type
{
	int          prefix_length;
}
-(void) writeTable;
-(void) writeSymtabInit:(PLItem *) parse;
@end

#endif//__renderer_vulkan_vkgen_vkenum_h
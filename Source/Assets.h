#pragma once
#include <juce_graphics/juce_graphics.h>
#include "BinaryData.h"

namespace rm
{
inline juce::Image loadAsset (const juce::String& filename)
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (filename == BinaryData::originalFilenames[i])
        {
            int size = 0;
            if (auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                return juce::ImageCache::getFromMemory (data, size);
        }
    }
    return {};
}
}

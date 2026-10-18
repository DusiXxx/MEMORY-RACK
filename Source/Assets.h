#pragma once
#include <juce_graphics/juce_graphics.h>
#include "BinaryData.h"
#include "Model.h"

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
    // obrazek ze stazene knihovny hardwaru (jen nazev souboru, zadne cesty)
    auto f = hardwareLibraryDir().getChildFile ("images").getChildFile (juce::File::createLegalFileName (filename));
    if (filename.isNotEmpty() && f.existsAsFile() && f.getSize() < 20 * 1024 * 1024)
        return juce::ImageCache::getFromFile (f);
    return {};
}
}

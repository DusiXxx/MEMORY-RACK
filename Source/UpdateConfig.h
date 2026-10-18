// Jedno misto pro nastaveni aktualizaci (repozitar se nastavuje v CMakeLists.txt: RM_UPDATE_REPO).
#pragma once

#ifndef RM_UPDATE_REPO
 #define RM_UPDATE_REPO ""
#endif

namespace rm::updateconfig
{
constexpr const char* repo            = RM_UPDATE_REPO;           // "ucet/repozitar" na GitHubu (verejny)
constexpr const char* channel         = "stable";                 // vydani oznacene jako "Latest" (ne pre-release)
constexpr const char* pluginManifest  = "latest.json";            // u posledniho vydani pluginu
constexpr const char* libraryTag      = "hardware";               // samostatne vydani s knihovnou hardwaru
constexpr const char* libraryManifest = "hardware.json";
constexpr const char* artifactType    = "AU component (.zip)";    // co plugin umi nainstalovat
constexpr int checkEveryHours         = 6;
constexpr int networkAttempts         = 3;                        // opakovani pri vypadku site
}

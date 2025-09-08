workspace "Game-Engine"
   architecture "x86_64"
   configurations { "Debug", "Release", "Dist" }
   startproject "Game-Engine"

   filter "system:windows"
      buildoptions { "/EHsc", "/Zc:preprocessor", "/Zc:__cplusplus" }

OutputDir = "%{cfg.system}-%{cfg.architecture}/%{cfg.buildcfg}"

group "Core"
	include "Game-Core/Build-Core.lua"
group ""

include "Game-Engine/Build-App.lua"
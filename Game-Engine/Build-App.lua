dofile("../Project-Config.lua")

project "Game-Engine"
   kind "ConsoleApp"
   language "C++"
   cppdialect "C++20"
   targetdir "Binaries/%{cfg.buildcfg}"
   staticruntime "off"

   files { "Source/**.h", "Source/**.cpp", "Source/**.c" }

   includedirs
   {
      "Source",
	  "../Game-Core/Source",
      "%{IncludeDir.GLM}",
      "%{IncludeDir.SDL3}",
      "%{IncludeDir.Vulkan}",
      "%{IncludeDir.GLFW}"
   }

   libdirs
   {
      "%{LibraryDir.Vulkan}",
      "%{LibraryDir.SDL3}",
      "%{LibraryDir.GLFW}"
   }

   links
   {
      "Game-Core",
      "%{Library.Vulkan}",
      "%{Library.SDL3}",
      "%{Library.GLFW}"
   }

   buildoptions { "/VERBOSE" }
   linkoptions { "/VERBOSE" }

   targetdir ("../Binaries/" .. OutputDir .. "/%{prj.name}")
   objdir ("../Binaries/Intermediates/" .. OutputDir .. "/%{prj.name}")

   filter "system:windows"
       systemversion "latest"
       defines { "WINDOWS" }

   filter "configurations:Debug"
       defines { "DEBUG" }
       runtime "Debug"
       symbols "On"

   filter "configurations:Release"
       defines { "RELEASE" }
       runtime "Release"
       optimize "On"
       symbols "On"

   filter "configurations:Dist"
       defines { "DIST" }
       runtime "Release"
       optimize "On"
       symbols "Off"
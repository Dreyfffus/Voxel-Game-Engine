dofile("../Project-Config.lua")

project "Game-Engine"
   kind "ConsoleApp"
   language "C++"
   cppdialect "C++latest"
   targetdir "Binaries/%{cfg.buildcfg}"
   staticruntime "off"
   toolset "msc"


   files { "Source/engine/include/**.h", "Source/engine/include/**.hpp", "Source/engine/src/**.cpp", "Source/engine/src/**.c",
           "Source/vkbootstrap/**.h", "Source/vkbootstrap/**.cpp",
           "Source/imgui/**.h", "Source/imgui/**.cpp" }

   includedirs
   {
      "Source/engine/include",
      "Source/vkbootstrap",
      "Source/imgui",
	   "../Game-Core/Source",
      "%{IncludeDir.GLM}",
      "%{IncludeDir.SDL}",
      "%{IncludeDir.Vulkan}",
      "%{IncludeDir.GLFW}",
      "%{IncludeDir.FMT}",
      "%{IncludeDir.STB}",
      "%{IncludeDir.FastGLTF}",
      "%{IncludeDir.VMA}",
      "%{IncludeDir.Volk}"
   }

   libdirs
   {
      "%{LibraryDir.SDL}",
      "%{LibraryDir.Vulkan}",
      "%{LibraryDir.GLFW}",
      "%{LibraryDir.FMT}",
      "%{LibraryDir.FastGLTF}"
   }

   links
   {
      "Game-Core",
      "%{Library.Vulkan}",
      "%{Library.SDL}",
      "%{Library.SDLmain}",
      "%{Library.GLFW}",
      "%{Library.FMT}",
      "%{Library.FastGLTF}",
      "%{Library.FastGLTF_simdjson}"
    }

   buildoptions { "/VERBOSE" }
   linkoptions { "/VERBOSE" }

   targetdir ("../Binaries/" .. OutputDir .. "/%{prj.name}")
   objdir ("../Binaries/Intermediates/" .. OutputDir .. "/%{prj.name}")


   filter "system:windows"
       systemversion "latest"
       defines { "WINDOWS" }
       postbuildcommands
       {
         '{COPY} "%{Binaries.FastGLTF_simdjson}" "%{cfg.targetdir}"',
         '{COPY} "%{Binaries.FastGLTF}" "%{cfg.targetdir}"',
         '{COPY} "%{Binaries.SDL}" "%{cfg.targetdir}"',
         '{COPY} "%{Binaries.shadercd}" "%{cfg.targetdir}"',
         '{COPY} "%{Binaries.shaderc}" "%{cfg.targetdir}"'
       }

   filter "configurations:Debug"
       defines { "DEBUG" }
       runtime "Debug"
       symbols "On"
       links {
          "%{Library.shadercd}"
       }

   filter "configurations:Release"
       defines { "RELEASE" }
       runtime "Release"
       optimize "On"
       symbols "On"
       links {
          "%{Library.shaderc}"
       }

   filter "configurations:Dist"
       defines { "DIST" }
       runtime "Release"
       optimize "On"
       symbols "Off"
       links {
          "%{Library.shaderc}"
       }
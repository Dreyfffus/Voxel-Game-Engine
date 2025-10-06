Rootdir = os.getcwd()

VULKAN_SDK = os.getenv("VULKAN_SDK") or "External/VulkanSDK/1.4.321.1"

if not os.isdir(VULKAN_SDK) then
   error("Vulkan SDK not found at " .. VULKAN_SDK .. ". Please set the VULKAN_SDK environment variable or place the SDK in the External directory.")
end

IncludeDir= {}
IncludeDir["Vulkan"] = VULKAN_SDK .. "/Include"
IncludeDir["SDL"] = Rootdir .. "/External/SDL/include"
IncludeDir["GLM"] = Rootdir .. "/External/glm"
IncludeDir["GLFW"] = Rootdir .. "/External/glfw/include"
IncludeDir["FMT"] = Rootdir .. "/External/fmt/include"
IncludeDir["STB"] = Rootdir .. "/External/stb_image"
IncludeDir["FastGLTF"] = Rootdir .. "/External/fastgltf/include"
IncludeDir["VMA"] = Rootdir .. "/External/vma"
IncludeDir["Volk"] = Rootdir .. "/External/volk"

LibraryDir = {}
LibraryDir["SDL"] = Rootdir .. "/External/SDL/lib"
LibraryDir["Vulkan"] = VULKAN_SDK .. "/Lib"
LibraryDir["GLFW"] = Rootdir .. "/External/glfw/lib"
LibraryDir["FMT"] = Rootdir .. "/External/fmt/lib"
LibraryDir["FastGLTF"] = Rootdir .. "/External/fastgltf/lib"

Library = {}
Library["FastGLTF"] = "fastgltf"
Library["FastGLTF_simdjson"] = "fastgltf_simdjson"
Library["SDL"] = "SDL2"
Library["SDLmain"] = "SDL2main"
Library["Vulkan"] = "vulkan-1"
Library["shaderc"] = "shaderc_shared"
Library["shadercd"] = "shaderc_sharedd"
Library["GLFW"] = "glfw3"
Library["FMT"] = "fmt"

Binaries = {}
Binaries["SDL"] = Rootdir .. "/External/SDL/bin/SDL2.dll"
Binaries["FastGLTF_simdjson"] = Rootdir .. "/External/fastgltf/bin/fastgltf_simdjson.dll"
Binaries["shaderc"] = VULKAN_SDK .. "/Bin/shaderc_shared.dll"
Binaries["shadercd"] = VULKAN_SDK .. "/Bin/shaderc_sharedd.dll"
Binaries["FastGLTF"] = Rootdir .. "/External/fastgltf/bin/fastgltf.dll"
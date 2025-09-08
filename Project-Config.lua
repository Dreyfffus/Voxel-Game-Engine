Rootdir = os.getcwd()

VULKAN_SDK = os.getenv("VULKAN_SDK") or "External/VulkanSDK/1.4.321.1"

if not os.isdir(VULKAN_SDK) then
   error("Vulkan SDK not found at " .. VULKAN_SDK .. ". Please set the VULKAN_SDK environment variable or place the SDK in the External directory.")
end

IncludeDir= {}
IncludeDir["Vulkan"] = VULKAN_SDK .. "/Include"
IncludeDir["SDL3"] = Rootdir .. "/External/SDL3/include"
IncludeDir["GLM"] = Rootdir .. "/External/glm"
IncludeDir["GLFW"] = Rootdir .. "/External/glfw/include"

LibraryDir = {}
LibraryDir["Vulkan"] = VULKAN_SDK .. "/Lib"
LibraryDir["SDL3"] = Rootdir .. "/External/SDL3/lib"
LibraryDir["GLFW"] = Rootdir .. "/External/glfw/lib"

Library = {}
Library["Vulkan"] = "vulkan-1"
Library["SDL3"] = "SDL3-static"
Library["GLFW"] = "glfw3"
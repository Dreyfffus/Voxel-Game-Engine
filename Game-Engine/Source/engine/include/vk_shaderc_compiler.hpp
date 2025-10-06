#pragma once
#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan.h>
#include <filesystem>
#include <unordered_map>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <fmt/printf.h>

namespace shdc {

    namespace fs = std::filesystem;

    static std::string readFileText(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        if (!in) fmt::printf("[I/O ERROR] could not open file {}\n", p.string());
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    static std::vector<uint32_t> readFileSpirv(const fs::path& p) {
        std::ifstream in(p, std::ios::ate | std::ios::binary);
        if (!in) fmt::printf("[I/O ERROR] could not open file {}\n", p.string());
        return std::vector<uint32_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    class FSIncluder : public shaderc::CompileOptions::IncluderInterface {
    public:
        explicit FSIncluder(fs::path r) : root(std::move(r)) {}
        shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type type, const char* requesting_source, size_t) override {
            if (type == shaderc_include_type_relative && requesting_source && *requested_source) {
                fs::path base = fs::path(requesting_source).parent_path();
                fs::path path = base / requested_source;
                if (fs::exists(path)) return make_result(path.string(), readFileText(path));
            }

            {
                fs::path path = root / requested_source;
                if (fs::exists(path)) return make_result(path.string(), readFileText(path));
            }

            std::string call = fmt::format("[I/O ERROR] could not find shader include path {}\n", requested_source);
            static const char* kMsg = call.c_str();
            return make_result(requested_source, std::string{ kMsg, sizeof(kMsg) - 1 });
        }

        void ReleaseInclude(shaderc_include_result* r) override {
            delete[] r->source_name;
            delete[] r->content;
            delete r;
        }

    private:

        fs::path root;

        static shaderc_include_result* make_result(const std::string& name, const std::string& content) {
            auto* result = new shaderc_include_result{};
            result->source_name_length = name.size();
            result->source_name = new char[name.size() + 1];
            memcpy(const_cast<char*>(result->source_name), name.data(), name.size());
            const_cast<char*>(result->source_name)[name.size()] = '\0';
            result->content_length = content.size();
            if (!content.empty()) {
                char* buffer = new char[content.size()];
                memcpy(buffer, content.data(), content.size());
                result->content = buffer;
            }
            result->user_data = nullptr;
            return result;
        }
    };

    static shaderc_shader_kind kindFromExtension(const fs::path& p) {
        auto ext = p.extension().string();
        if (ext == ".vert") return shaderc_vertex_shader;
        if (ext == ".frag") return shaderc_fragment_shader;
        if (ext == ".comp") return shaderc_compute_shader;
        if (ext == ".geom") return shaderc_geometry_shader;
        if (ext == ".tesc") return shaderc_tess_control_shader;
        if (ext == ".tese") return shaderc_tess_evaluation_shader;
        fmt::print("[I/O ERROR] could not recognize the extension of the shader {}\n", p.string());
		abort();
    }

    static std::vector<uint32_t> compileGLSLtoSPV(const fs::path& file,
        const fs::path& includeRoot,
        const std::vector<std::pair<std::string, std::string>>& defines = {},
#ifdef DEBUG
        bool optimize = false) {
#else
		bool optimize = true) {
#endif
        shaderc::Compiler compiler;
        shaderc::CompileOptions opts;
        if (optimize) opts.SetOptimizationLevel(shaderc_optimization_level_performance);
        opts.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
        opts.SetSourceLanguage(shaderc_source_language_glsl);
        opts.SetTargetSpirv(shaderc_spirv_version_1_5);
        opts.SetIncluder(std::make_unique<FSIncluder>(includeRoot));
        for (auto& d : defines) opts.AddMacroDefinition(d.first, d.second);

        const std::string src = readFileText(includeRoot / file);
        auto kind = kindFromExtension(file);

        const std::string srcName = (includeRoot / file).string();

        shaderc::SpvCompilationResult res = compiler.CompileGlslToSpv(
            src.c_str(), src.size(), kind, srcName.c_str(), "main", opts);

        if (res.GetCompilationStatus() != shaderc_compilation_status_success) {
            fmt::print("shaderc: {}", std::string(res.GetErrorMessage()));
			abort();
        }
        return { res.cbegin(), res.cend() };
    }
}
#include <NifFile.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const unsigned char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                static constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 0x0f];
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

std::vector<fs::path> collectNifs(const fs::path& input) {
    std::vector<fs::path> files;
    if (fs::is_regular_file(input)) {
        files.push_back(input);
    } else if (fs::is_directory(input)) {
        for (const auto& entry : fs::recursive_directory_iterator(input)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (ext == ".nif") files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string cleanTexturePath(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
        return c < 0x20 || c == 0x7f;
    }), value.end());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    std::replace(value.begin(), value.end(), '/', '\\');
    return value;
}

std::string normalizedTexturePath(std::string value) {
    value = cleanTexturePath(std::move(value));
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

void sanitizeTexturePaths(nifly::NifFile& nif) {
    for (auto* shape : nif.GetShapes()) {
        for (std::uint32_t slot = 0; slot < 10; ++slot) {
            std::string texture;
            if (nif.GetTextureSlot(shape, texture, slot) == 0 || texture.empty()) continue;
            auto cleaned = cleanTexturePath(texture);
            if (cleaned != texture) nif.SetTextureSlot(shape, cleaned, slot);
        }
    }
}

void emitInspection(const fs::path& file, const nifly::NifFile& nif) {
    const auto& version = nif.GetHeader().GetVersion();
    const auto shapes = nif.GetShapes();
    std::size_t triShapes = 0;
    std::size_t triStrips = 0;
    std::size_t bsTriShapes = 0;
    std::set<std::string> textures;

    for (auto* shape : shapes) {
        if (dynamic_cast<nifly::NiTriShape*>(shape)) ++triShapes;
        if (dynamic_cast<nifly::NiTriStrips*>(shape)) ++triStrips;
        if (dynamic_cast<nifly::BSTriShape*>(shape)) ++bsTriShapes;
        for (std::uint32_t slot = 0; slot < 10; ++slot) {
            std::string texture;
            if (nif.GetTextureSlot(shape, texture, slot) != 0 && !texture.empty()) {
                textures.insert(texture);
            }
        }
    }

    std::cout << "{\"path\":\"" << jsonEscape(file.generic_string())
              << "\",\"valid\":" << (nif.IsValid() ? "true" : "false")
              << ",\"unknownBlocks\":" << (nif.HasUnknown() ? "true" : "false")
              << ",\"version\":\"" << jsonEscape(version.String())
              << "\",\"userVersion\":" << version.User()
              << ",\"streamVersion\":" << version.Stream()
              << ",\"isLE\":" << (version.IsSK() ? "true" : "false")
              << ",\"isSSE\":" << (version.IsSSE() ? "true" : "false")
              << ",\"sseGeometryCompatible\":" << (nif.IsSSECompatible() ? "true" : "false")
              << ",\"shapes\":" << shapes.size()
              << ",\"niTriShapes\":" << triShapes
              << ",\"niTriStrips\":" << triStrips
              << ",\"bsTriShapes\":" << bsTriShapes
              << ",\"shapeDetails\":[";

    bool firstShape = true;
    for (auto* shape : shapes) {
        if (!firstShape) std::cout << ',';
        firstShape = false;
        std::vector<std::string> bones;
        nif.GetShapeBoneList(shape, bones);
        std::cout << "{\"name\":\"" << jsonEscape(shape->name.get())
                  << "\",\"vertices\":" << shape->GetNumVertices()
                  << ",\"triangles\":" << shape->GetNumTriangles()
                  << ",\"skinned\":" << (shape->IsSkinned() ? "true" : "false")
                  << ",\"bones\":[";
        bool firstBone = true;
        for (const auto& bone : bones) {
            if (!firstBone) std::cout << ',';
            firstBone = false;
            std::cout << '"' << jsonEscape(bone) << '"';
        }
        std::cout << "],\"textures\":[";
        bool firstTexture = true;
        for (std::uint32_t slot = 0; slot < 10; ++slot) {
            std::string texture;
            if (nif.GetTextureSlot(shape, texture, slot) == 0 || texture.empty()) continue;
            if (!firstTexture) std::cout << ',';
            firstTexture = false;
            std::cout << '"' << jsonEscape(texture) << '"';
        }
        std::cout << "]}";
    }
    std::cout << "],\"textures\":[";

    bool first = true;
    for (const auto& texture : textures) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << '"' << jsonEscape(texture) << '"';
    }
    std::cout << "]}\n";
}

int cloneShape(const fs::path& base,
               const fs::path& donor,
               const std::string& shapeName,
               const fs::path& output) {
    if (!fs::is_regular_file(base) || !fs::is_regular_file(donor)) {
        std::cerr << "Base and donor NIFs must both exist\n";
        return 2;
    }
    if (fs::exists(output)) {
        std::cerr << "Refusing to overwrite output: " << output << '\n';
        return 2;
    }
    if (fs::absolute(base).lexically_normal() == fs::absolute(output).lexically_normal()
        || fs::absolute(donor).lexically_normal() == fs::absolute(output).lexically_normal()) {
        std::cerr << "Inputs and output must differ\n";
        return 2;
    }

    nifly::NifFile target(base);
    nifly::NifFile source(donor);
    if (!target.IsValid() || target.HasUnknown() || !source.IsValid() || source.HasUnknown()) {
        std::cerr << "Refusing to clone from invalid or partially understood NIFs\n";
        return 3;
    }
    if (target.GetHeader().GetVersion().String() != source.GetHeader().GetVersion().String()) {
        std::cerr << "Base and donor NIF versions differ\n";
        return 3;
    }

    nifly::NiShape* donorShape = nullptr;
    for (auto* shape : source.GetShapes()) {
        if (shape->name.get() != shapeName) continue;
        if (donorShape != nullptr) {
            std::cerr << "Donor shape name is ambiguous: " << shapeName << '\n';
            return 3;
        }
        donorShape = shape;
    }
    if (donorShape == nullptr) {
        std::cerr << "Donor shape was not found: " << shapeName << '\n';
        return 3;
    }
    for (auto* shape : target.GetShapes()) {
        if (shape->name.get() == shapeName) {
            std::cerr << "Base already contains shape: " << shapeName << '\n';
            return 3;
        }
    }

    const auto originalShapeCount = target.GetShapes().size();
    std::vector<std::string> donorBones;
    source.GetShapeBoneList(donorShape, donorBones);
    std::vector<std::string> donorTextures;
    for (std::uint32_t slot = 0; slot < 10; ++slot) {
        std::string texture;
        if (source.GetTextureSlot(donorShape, texture, slot) != 0 && !texture.empty()) {
            donorTextures.push_back(normalizedTexturePath(texture));
        }
    }
    const auto donorVertices = donorShape->GetNumVertices();
    const auto donorTriangles = donorShape->GetNumTriangles();
    const auto donorSkinned = donorShape->IsSkinned();
    if (target.CloneShape(donorShape, shapeName, &source) == nullptr) {
        std::cerr << "Shape clone failed: " << shapeName << '\n';
        return 4;
    }

    fs::create_directories(output.parent_path());
    if (target.Save(output) != 0) {
        std::cerr << "Save failed: " << output << '\n';
        return 4;
    }

    nifly::NifFile check(output);
    nifly::NiShape* clonedShape = nullptr;
    for (auto* shape : check.GetShapes()) {
        if (shape->name.get() == shapeName) clonedShape = shape;
    }
    std::vector<std::string> clonedBones;
    if (clonedShape != nullptr) check.GetShapeBoneList(clonedShape, clonedBones);
    std::vector<std::string> clonedTextures;
    if (clonedShape != nullptr) {
        for (std::uint32_t slot = 0; slot < 10; ++slot) {
            std::string texture;
            if (check.GetTextureSlot(clonedShape, texture, slot) != 0 && !texture.empty()) {
                clonedTextures.push_back(normalizedTexturePath(texture));
            }
        }
    }
    if (!check.IsValid() || check.HasUnknown() || !check.IsSSECompatible()
        || check.GetShapes().size() != originalShapeCount + 1 || clonedShape == nullptr
        || clonedShape->GetNumVertices() != donorVertices
        || clonedShape->GetNumTriangles() != donorTriangles
        || clonedShape->IsSkinned() != donorSkinned
        || clonedBones != donorBones || clonedTextures != donorTextures) {
        std::cerr << "Post-clone validation failed: " << output << '\n';
        std::error_code ignored;
        fs::remove(output, ignored);
        return 4;
    }

    emitInspection(output, check);
    return 0;
}

int exportObj(const fs::path& input, const fs::path& output) {
    if (!fs::is_regular_file(input)) {
        std::cerr << "Input NIF does not exist: " << input << '\n';
        return 2;
    }
    if (fs::exists(output)) {
        std::cerr << "Refusing to overwrite output: " << output << '\n';
        return 2;
    }
    nifly::NifFile nif(input);
    if (!nif.IsValid() || nif.HasUnknown()) {
        std::cerr << "Refusing to export invalid or partially understood NIF: " << input << '\n';
        return 3;
    }
    fs::create_directories(output.parent_path());
    std::ofstream stream(output, std::ios::binary);
    if (!stream) {
        std::cerr << "Could not create OBJ: " << output << '\n';
        return 4;
    }
    stream << "# nif-port-cli deterministic inspection export\n";
    std::size_t vertexOffset = 1;
    for (auto* shape : nif.GetShapes()) {
        std::vector<nifly::Vector3> vertices;
        std::vector<nifly::Triangle> triangles;
        if (!nif.GetVertsForShape(shape, vertices) || !shape->GetTriangles(triangles)) {
            std::cerr << "Could not read shape geometry: " << shape->name.get() << '\n';
            return 4;
        }
        stream << "o " << shape->name.get() << '\n';
        const auto transform = shape->GetTransformToParent();
        for (const auto& vertex : vertices) {
            const auto point = transform.ApplyTransform(vertex);
            stream << "v " << point.x << ' ' << point.y << ' ' << point.z << '\n';
        }
        for (const auto& triangle : triangles) {
            stream << "f " << vertexOffset + triangle.p1 << ' '
                   << vertexOffset + triangle.p2 << ' '
                   << vertexOffset + triangle.p3 << '\n';
        }
        vertexOffset += vertices.size();
    }
    if (!stream) {
        std::cerr << "OBJ write failed: " << output << '\n';
        return 4;
    }
    std::cout << "{\"input\":\"" << jsonEscape(input.generic_string())
              << "\",\"output\":\"" << jsonEscape(output.generic_string())
              << "\",\"shapes\":" << nif.GetShapes().size() << "}\n";
    return 0;
}

int inspect(const fs::path& input) {
    const auto files = collectNifs(input);
    if (files.empty()) {
        std::cerr << "No NIF files found: " << input << '\n';
        return 2;
    }

    int failures = 0;
    for (const auto& file : files) {
        nifly::NifFile nif(file);
        emitInspection(file, nif);
        if (!nif.IsValid() || nif.HasUnknown()) ++failures;
    }
    return failures == 0 ? 0 : 3;
}

int convertSse(const fs::path& input, const fs::path& output) {
    const auto files = collectNifs(input);
    if (files.empty()) {
        std::cerr << "No NIF files found: " << input << '\n';
        return 2;
    }

    const fs::path inputRoot = fs::is_directory(input) ? input : input.parent_path();
    int failures = 0;
    for (const auto& file : files) {
        nifly::NifFile nif(file);
        if (!nif.IsValid() || nif.HasUnknown()) {
            std::cerr << "Refusing to convert invalid or partially understood NIF: " << file << '\n';
            ++failures;
            continue;
        }

        nifly::OptOptions options;
        options.targetVersion = nifly::NiVersion::getSSE();
        const auto result = nif.OptimizeFor(options);
        sanitizeTexturePaths(nif);
        if (result.versionMismatch) {
            std::cerr << "Unsupported source version: " << file << '\n';
            ++failures;
            continue;
        }

        const auto relative = fs::relative(file, inputRoot);
        const auto destination = output / relative;
        fs::create_directories(destination.parent_path());
        if (nif.Save(destination) != 0) {
            std::cerr << "Save failed: " << destination << '\n';
            ++failures;
            continue;
        }

        nifly::NifFile check(destination);
        if (!check.IsValid() || check.HasUnknown() || !check.GetHeader().GetVersion().IsSSE()
            || !check.IsSSECompatible()) {
            std::cerr << "Post-conversion validation failed: " << destination << '\n';
            ++failures;
            continue;
        }
        emitInspection(destination, check);
    }
    return failures == 0 ? 0 : 4;
}

int remapTextures(const fs::path& input,
                  const fs::path& output,
                  const std::map<std::string, std::string>& replacements) {
    if (!fs::is_regular_file(input)) {
        std::cerr << "Input NIF does not exist: " << input << '\n';
        return 2;
    }
    if (fs::exists(output)) {
        std::cerr << "Refusing to overwrite output: " << output << '\n';
        return 2;
    }
    if (fs::absolute(input).lexically_normal() == fs::absolute(output).lexically_normal()) {
        std::cerr << "Input and output paths must differ\n";
        return 2;
    }

    nifly::NifFile nif(input);
    if (!nif.IsValid() || nif.HasUnknown()) {
        std::cerr << "Refusing to edit invalid or partially understood NIF: " << input << '\n';
        return 3;
    }

    std::map<std::string, std::size_t> hits;
    for (auto* shape : nif.GetShapes()) {
        for (std::uint32_t slot = 0; slot < 10; ++slot) {
            std::string texture;
            if (nif.GetTextureSlot(shape, texture, slot) == 0 || texture.empty()) continue;
            const auto key = normalizedTexturePath(texture);
            const auto replacement = replacements.find(key);
            if (replacement == replacements.end()) continue;
            nif.SetTextureSlot(shape, cleanTexturePath(replacement->second), slot);
            ++hits[key];
        }
    }

    for (const auto& replacement : replacements) {
        const auto& source = replacement.first;
        if (hits[source] == 0) {
            std::cerr << "Requested texture path was not present: " << source << '\n';
            return 3;
        }
    }

    fs::create_directories(output.parent_path());
    if (nif.Save(output) != 0) {
        std::cerr << "Save failed: " << output << '\n';
        return 4;
    }

    nifly::NifFile check(output);
    if (!check.IsValid() || check.HasUnknown()
        || check.GetHeader().GetVersion().String() != nif.GetHeader().GetVersion().String()
        || check.GetShapes().size() != nif.GetShapes().size()) {
        std::cerr << "Post-remap validation failed: " << output << '\n';
        std::error_code ignored;
        fs::remove(output, ignored);
        return 4;
    }
    std::map<std::string, std::size_t> postHits;
    for (auto* shape : check.GetShapes()) {
        for (std::uint32_t slot = 0; slot < 10; ++slot) {
            std::string texture;
            if (check.GetTextureSlot(shape, texture, slot) != 0 && !texture.empty()) {
                ++postHits[normalizedTexturePath(texture)];
            }
        }
    }
    for (const auto& [source, destination] : replacements) {
        if (postHits[source] != 0 || postHits[normalizedTexturePath(destination)] < hits[source]) {
            std::cerr << "Post-remap texture verification failed: " << source << '\n';
            std::error_code ignored;
            fs::remove(output, ignored);
            return 4;
        }
    }

    emitInspection(output, check);
    return 0;
}

void usage() {
    std::cerr << "Usage:\n"
              << "  nif-port-cli inspect <file-or-directory>\n"
              << "  nif-port-cli convert-sse <input-file-or-directory> <output-directory>\n"
              << "  nif-port-cli clone-shape <base-file> <donor-file> <shape-name> <output-file>\n"
              << "  nif-port-cli export-obj <input-file> <output-file>\n"
              << "  nif-port-cli remap-textures <input-file> <output-file>"
                 " <old-path> <new-path> [<old-path> <new-path> ...]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 1;
    }
    const std::string command = argv[1];
    if (command == "inspect" && argc == 3) return inspect(fs::u8path(argv[2]));
    if (command == "convert-sse" && argc == 4) {
        return convertSse(fs::u8path(argv[2]), fs::u8path(argv[3]));
    }
    if (command == "clone-shape" && argc == 6) {
        return cloneShape(fs::u8path(argv[2]), fs::u8path(argv[3]), argv[4], fs::u8path(argv[5]));
    }
    if (command == "export-obj" && argc == 4) {
        return exportObj(fs::u8path(argv[2]), fs::u8path(argv[3]));
    }
    if (command == "remap-textures" && argc >= 6 && ((argc - 4) % 2) == 0) {
        std::map<std::string, std::string> replacements;
        for (int i = 4; i < argc; i += 2) {
            const auto source = normalizedTexturePath(argv[i]);
            const auto destination = cleanTexturePath(argv[i + 1]);
            if (source.empty() || destination.empty()
                || source == normalizedTexturePath(destination)
                || replacements.find(source) != replacements.end()) {
                std::cerr << "Texture source paths must be unique and destinations must differ\n";
                return 1;
            }
            replacements.emplace(source, destination);
        }
        return remapTextures(fs::u8path(argv[2]), fs::u8path(argv[3]), replacements);
    }
    usage();
    return 1;
}

// engine/src/assets/GltfLoader.cpp — the ONLY TU that includes cgltf (ADR-030).
//
// glTF conventions already match ours: metres, +Y up, CCW front faces, and
// UV origin at the TOP-left (so, unlike OBJ, no V flip). Static files: each
// mesh gets its node's world transform baked in; the per-model import
// correction is applied afterwards by loadModel. Animated files (any skin or
// animation): the whole node tree becomes an anim::Skeleton, skinned
// primitives keep bind-space vertices (the skinned mesh node's own transform
// is ignored, per the spec), and every other mesh becomes a RIGID part —
// one joint on its node, weight 1 — so node animation and props held in a
// hand follow the skeleton through the same GPU path.
#include "ImportInternal.hpp"
#include "forge/core/Log.hpp"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4456 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace forge::assets::detail {

namespace {

struct DataDeleter {
    void operator()(cgltf_data* data) const { cgltf_free(data); }
};
using DataPtr = std::unique_ptr<cgltf_data, DataDeleter>;

const char* resultText(cgltf_result result) {
    switch (result) {
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "unknown format";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_invalid_options: return "invalid options";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "glTF 1.0 is not supported";
    default: return "error";
    }
}

// All floats of an accessor (sparse + normalized integers handled by cgltf).
std::vector<float> unpackFloats(const cgltf_accessor& accessor) {
    std::vector<float> out(accessor.count * cgltf_num_components(accessor.type));
    if (!out.empty()) {
        cgltf_accessor_unpack_floats(&accessor, out.data(), out.size());
    }
    return out;
}

glm::mat4 localMatrix(const cgltf_node& node) {
    float m[16];
    cgltf_node_transform_local(&node, m);
    return glm::make_mat4(m);
}

std::string nodeName(const cgltf_data& data, const cgltf_node& node) {
    return node.name != nullptr ? std::string(node.name)
                                : "node" + std::to_string(&node - data.nodes);
}

class Importer {
public:
    Importer(const std::string& path, const ImportOptions& options)
        : m_path(path), m_options(options),
          m_dir(std::filesystem::path(path).parent_path()) {}

    ModelData run() {
        cgltf_options options{};
        cgltf_data* raw = nullptr;
        cgltf_result result = cgltf_parse_file(&options, m_path.c_str(), &raw);
        if (result != cgltf_result_success) {
            throw std::runtime_error("glTF import failed: " + m_path + " (" +
                                     resultText(result) + ")");
        }
        m_data.reset(raw);
        result = cgltf_load_buffers(&options, m_data.get(), m_path.c_str());
        if (result != cgltf_result_success) {
            throw std::runtime_error("glTF buffers failed: " + m_path + " (" +
                                     resultText(result) + ")");
        }
        // Bounds-checks every accessor/view against its buffer: after this,
        // reads cannot run off the end of a malformed file.
        result = cgltf_validate(m_data.get());
        if (result != cgltf_result_success) {
            throw std::runtime_error("glTF validation failed: " + m_path + " (" +
                                     resultText(result) + ")");
        }
        readMaterials();
        m_animated = m_data->skins_count > 0 || m_data->animations_count > 0;
        if (m_animated) {
            buildSkeleton();
        }
        walkScene();
        if (m_animated) {
            readAnimations();
            m_skeleton->root = importCorrection(m_options);
            m_out.skeleton = std::move(m_skeleton);
        }
        return std::move(m_out);
    }

private:
    int32_t imageIndex(const cgltf_image* image) {
        if (image == nullptr) {
            return -1;
        }
        if (const auto it = m_images.find(image); it != m_images.end()) {
            return it->second;
        }
        int32_t index = -1;
        const std::string label = m_path + "#" + (image->name != nullptr ? image->name : "image");
        try {
            if (image->buffer_view != nullptr) { // embedded (GLB bin chunk)
                const uint8_t* bytes = cgltf_buffer_view_data(image->buffer_view);
                if (bytes == nullptr) {
                    throw std::runtime_error("image buffer view has no data");
                }
                m_out.images.push_back(
                    loadImageFromMemory(bytes, image->buffer_view->size, label));
                m_out.imageSources.emplace_back();
            } else if (image->uri != nullptr && std::strncmp(image->uri, "data:", 5) == 0) {
                const char* comma = std::strchr(image->uri, ',');
                if (comma == nullptr || std::strstr(image->uri, ";base64,") == nullptr) {
                    throw std::runtime_error("unsupported data URI");
                }
                const char* base64 = comma + 1;
                size_t length = std::strlen(base64);
                size_t size = length / 4 * 3;
                while (length > 0 && base64[length - 1] == '=') {
                    --length;
                    --size;
                }
                cgltf_options options{};
                void* decoded = nullptr;
                if (cgltf_load_buffer_base64(&options, size, base64, &decoded) !=
                    cgltf_result_success) {
                    throw std::runtime_error("bad base64 image");
                }
                try {
                    m_out.images.push_back(
                        loadImageFromMemory(static_cast<const uint8_t*>(decoded), size, label));
                    m_out.imageSources.emplace_back();
                } catch (...) {
                    std::free(decoded);
                    throw;
                }
                std::free(decoded);
            } else if (image->uri != nullptr) { // external file next to the .gltf
                std::string uri = image->uri;
                uri.resize(cgltf_decode_uri(uri.data()));
                const std::string file = (m_dir / uri).lexically_normal().string();
                m_out.images.push_back(loadImage(file));
                m_out.imageSources.push_back(file);
            } else {
                throw std::runtime_error("image has neither data nor uri");
            }
            index = static_cast<int32_t>(m_out.images.size() - 1);
        } catch (const std::exception& e) {
            FORGE_WARN("model {}: texture unreadable, material stays untextured: {}", m_path,
                       e.what());
        }
        m_images.emplace(image, index);
        return index;
    }

    void readMaterials() {
        const cgltf_data& data = *m_data;
        for (cgltf_size i = 0; i < data.materials_count; ++i) {
            const cgltf_material& source = data.materials[i];
            MaterialData material;
            material.name = source.name != nullptr ? source.name : "material" + std::to_string(i);
            const cgltf_texture* texture = nullptr;
            const float* factor = nullptr;
            if (source.has_pbr_metallic_roughness != 0) {
                factor = source.pbr_metallic_roughness.base_color_factor;
                texture = source.pbr_metallic_roughness.base_color_texture.texture;
                if (texture != nullptr &&
                    source.pbr_metallic_roughness.base_color_texture.texcoord != 0) {
                    FORGE_WARN("model {}: material '{}' samples UV set {}; only set 0 is "
                               "imported",
                               m_path, material.name,
                               source.pbr_metallic_roughness.base_color_texture.texcoord);
                }
            } else if (source.has_pbr_specular_glossiness != 0) {
                factor = source.pbr_specular_glossiness.diffuse_factor;
                texture = source.pbr_specular_glossiness.diffuse_texture.texture;
            }
            if (factor != nullptr) {
                material.baseColor = {factor[0], factor[1], factor[2], factor[3]};
            }
            if (texture != nullptr) {
                material.image = imageIndex(texture->image);
            }
            m_out.materials.push_back(std::move(material));
        }
    }

    // A primitive without a material gets the spec's default (white) — and,
    // like an OBJ without MTL, lets the entity's own texture show.
    uint32_t defaultMaterial() {
        if (m_defaultMaterial < 0) {
            m_out.materials.push_back(MaterialData{"default", glm::vec4(1.0f), -1, false});
            m_defaultMaterial = static_cast<int32_t>(m_out.materials.size() - 1);
        }
        return static_cast<uint32_t>(m_defaultMaterial);
    }

    // Reads one triangle primitive. Returns false (after logging) to skip it.
    bool readPrimitive(const cgltf_primitive& primitive, const std::string& owner,
                       MeshData& out) const {
        if (primitive.type != cgltf_primitive_type_triangles) {
            FORGE_WARN("model {}: '{}' has a non-triangle primitive, skipped", m_path, owner);
            return false;
        }
        if (primitive.has_draco_mesh_compression != 0) {
            FORGE_WARN("model {}: '{}' is Draco-compressed (unsupported), skipped", m_path,
                       owner);
            return false;
        }
        const cgltf_accessor* position = nullptr;
        const cgltf_accessor* normal = nullptr;
        const cgltf_accessor* uv = nullptr;
        for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
            const cgltf_attribute& attribute = primitive.attributes[a];
            if (attribute.index != 0) {
                continue;
            }
            switch (attribute.type) {
            case cgltf_attribute_type_position: position = attribute.data; break;
            case cgltf_attribute_type_normal: normal = attribute.data; break;
            case cgltf_attribute_type_texcoord: uv = attribute.data; break;
            default: break; // COLOR_0 is ignored (ADR-030); tangents arrive with normal maps
            }
        }
        if (position == nullptr || position->count == 0) {
            FORGE_WARN("model {}: '{}' has no positions, skipped", m_path, owner);
            return false;
        }
        const size_t count = position->count;
        out.vertices.assign(count, Vertex{});
        const std::vector<float> p = unpackFloats(*position);
        for (size_t i = 0; i < count; ++i) {
            out.vertices[i].position = {p[i * 3], p[i * 3 + 1], p[i * 3 + 2]};
        }
        const bool hasNormals = normal != nullptr && normal->count == count;
        if (hasNormals) {
            const std::vector<float> n = unpackFloats(*normal);
            for (size_t i = 0; i < count; ++i) {
                out.vertices[i].normal = {n[i * 3], n[i * 3 + 1], n[i * 3 + 2]};
            }
        }
        if (uv != nullptr && uv->count == count) {
            const std::vector<float> t = unpackFloats(*uv);
            for (size_t i = 0; i < count; ++i) {
                out.vertices[i].uv = {t[i * 2], t[i * 2 + 1]};
            }
        }
        if (primitive.indices != nullptr) {
            out.indices.resize(primitive.indices->count);
            for (size_t i = 0; i < out.indices.size(); ++i) {
                const size_t index = cgltf_accessor_read_index(primitive.indices, i);
                if (index >= count) {
                    FORGE_WARN("model {}: '{}' has an out-of-range index, skipped", m_path,
                               owner);
                    return false;
                }
                out.indices[i] = static_cast<uint32_t>(index);
            }
        } else {
            out.indices.resize(count);
            for (size_t i = 0; i < count; ++i) {
                out.indices[i] = static_cast<uint32_t>(i);
            }
        }
        out.indices.resize(out.indices.size() / 3 * 3);
        if (!hasNormals) {
            generateNormals(out); // the spec says flat; smooth is our cheaper stand-in
        }
        return !out.indices.empty();
    }

    // ---- Animated files ----------------------------------------------------

    static void decompose(const glm::mat4& m, glm::vec3& t, glm::quat& r, glm::vec3& s) {
        t = glm::vec3(m[3]);
        s = {glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])),
             glm::length(glm::vec3(m[2]))};
        if (glm::determinant(glm::mat3(m)) < 0.0f) {
            s.x = -s.x;
        }
        const glm::vec3 safe = glm::max(glm::abs(s), glm::vec3(1e-8f)) * glm::sign(s);
        r = glm::normalize(glm::quat_cast(glm::mat3(glm::vec3(m[0]) / safe.x,
                                                    glm::vec3(m[1]) / safe.y,
                                                    glm::vec3(m[2]) / safe.z)));
    }

    // Every node of the file (all roots, depth-first, parents first) — not
    // just the scene's: a skin may name joints outside the active scene.
    void buildSkeleton() {
        const cgltf_data& data = *m_data;
        m_skeleton = std::make_shared<anim::Skeleton>();
        struct Frame {
            const cgltf_node* node;
            int32_t parent;
        };
        std::vector<Frame> stack;
        for (cgltf_size i = data.nodes_count; i-- > 0;) {
            if (data.nodes[i].parent == nullptr) {
                stack.push_back({&data.nodes[i], -1});
            }
        }
        while (!stack.empty()) {
            const Frame frame = stack.back();
            stack.pop_back();
            const cgltf_node& node = *frame.node;
            anim::Skeleton::Node out;
            out.name = nodeName(data, node);
            out.parent = frame.parent;
            if (node.has_matrix != 0) {
                decompose(glm::make_mat4(node.matrix), out.translation, out.rotation, out.scale);
            } else {
                if (node.has_translation != 0) {
                    out.translation = glm::make_vec3(node.translation);
                }
                if (node.has_rotation != 0) {
                    out.rotation.x = node.rotation[0];
                    out.rotation.y = node.rotation[1];
                    out.rotation.z = node.rotation[2];
                    out.rotation.w = node.rotation[3];
                    out.rotation = glm::normalize(out.rotation);
                }
                if (node.has_scale != 0) {
                    out.scale = glm::make_vec3(node.scale);
                }
            }
            const auto index = static_cast<int32_t>(m_skeleton->nodes.size());
            m_nodeIndex.emplace(&node, index);
            m_skeleton->nodes.push_back(std::move(out));
            for (cgltf_size c = node.children_count; c-- > 0;) {
                stack.push_back({node.children[c], index});
            }
        }
        // Joints: every skin's joints, appended skin after skin.
        for (cgltf_size s = 0; s < data.skins_count; ++s) {
            const cgltf_skin& skin = data.skins[s];
            m_skinBase.push_back(static_cast<uint32_t>(m_skeleton->jointNodes.size()));
            std::vector<float> ibm;
            if (skin.inverse_bind_matrices != nullptr) {
                ibm = unpackFloats(*skin.inverse_bind_matrices);
            }
            for (cgltf_size j = 0; j < skin.joints_count; ++j) {
                m_skeleton->jointNodes.push_back(m_nodeIndex.at(skin.joints[j]));
                m_skeleton->inverseBind.push_back(ibm.size() >= (j + 1) * 16
                                                      ? glm::make_mat4(&ibm[j * 16])
                                                      : glm::mat4(1.0f));
            }
        }
    }

    // A non-skinned mesh node in an animated file: one joint on the node.
    uint16_t rigidJoint(const cgltf_node& node) {
        if (const auto it = m_rigidJoints.find(&node); it != m_rigidJoints.end()) {
            return it->second;
        }
        const auto joint = static_cast<uint16_t>(m_skeleton->jointNodes.size());
        m_skeleton->jointNodes.push_back(m_nodeIndex.at(&node));
        m_skeleton->inverseBind.emplace_back(1.0f);
        m_rigidJoints.emplace(&node, joint);
        return joint;
    }

    void addAnimatedMeshNode(const cgltf_node& node) {
        const std::string owner = nodeName(*m_data, node);
        const cgltf_mesh& mesh = *node.mesh;
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& primitive = mesh.primitives[p];
            SubmeshData part;
            part.name = owner;
            if (!readPrimitive(primitive, owner, part.mesh)) {
                continue;
            }
            part.material = primitive.material != nullptr
                                ? static_cast<uint32_t>(primitive.material - m_data->materials)
                                : defaultMaterial();
            const size_t count = part.mesh.vertices.size();
            part.skin.assign(count, SkinVertex{});
            const cgltf_accessor* joints = nullptr;
            const cgltf_accessor* weights = nullptr;
            for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
                const cgltf_attribute& attribute = primitive.attributes[a];
                if (attribute.type == cgltf_attribute_type_joints) {
                    if (attribute.index == 0) {
                        joints = attribute.data;
                    } else if (!m_warnedExtraInfluences) {
                        m_warnedExtraInfluences = true;
                        FORGE_WARN("model {}: more than 4 joint influences per vertex; only "
                                   "JOINTS_0/WEIGHTS_0 are used (renormalized)",
                                   m_path);
                    }
                } else if (attribute.type == cgltf_attribute_type_weights && attribute.index == 0) {
                    weights = attribute.data;
                }
            }
            const bool skinned = node.skin != nullptr && joints != nullptr &&
                                 weights != nullptr && joints->count == count &&
                                 weights->count == count;
            if (skinned) {
                const cgltf_skin& skin = *node.skin;
                const uint32_t base = m_skinBase[static_cast<size_t>(&skin - m_data->skins)];
                const std::vector<float> w = unpackFloats(*weights);
                for (size_t i = 0; i < count; ++i) {
                    cgltf_uint j[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_uint(joints, i, j, 4);
                    glm::vec4 wi(w[i * 4], w[i * 4 + 1], w[i * 4 + 2], w[i * 4 + 3]);
                    SkinVertex& sv = part.skin[i];
                    for (int k = 0; k < 4; ++k) {
                        if (j[k] >= skin.joints_count || wi[k] < 0.0f) {
                            wi[k] = 0.0f;
                            j[k] = 0;
                        }
                        sv.joints[k] = static_cast<uint16_t>(base + j[k]);
                    }
                    const float sum = wi.x + wi.y + wi.z + wi.w;
                    sv.weights = sum > 1e-6f ? wi / sum : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                }
            } else {
                // Rigid: vertices stay in the node's local space and ride
                // the node's (possibly animated) global transform.
                const uint16_t joint = rigidJoint(node);
                for (SkinVertex& sv : part.skin) {
                    sv.joints[0] = joint;
                }
            }
            m_out.submeshes.push_back(std::move(part));
        }
    }

    template <typename T>
    static bool fillTrack(const cgltf_animation_sampler& sampler, anim::Track<T>& track,
                          size_t components) {
        track.times = unpackFloats(*sampler.input);
        const std::vector<float> values = unpackFloats(*sampler.output);
        const bool cubic = sampler.interpolation == cgltf_interpolation_type_cubic_spline;
        const size_t perKey = cubic ? 3 : 1; // in-tangent, VALUE, out-tangent
        const size_t keys = track.times.size();
        if (keys == 0 || values.size() < keys * perKey * components) {
            track.times.clear();
            return false;
        }
        track.interpolation = sampler.interpolation == cgltf_interpolation_type_step
                                  ? anim::Interpolation::Step
                                  : anim::Interpolation::Linear; // CUBICSPLINE -> linear
        track.values.resize(keys);
        for (size_t k = 0; k < keys; ++k) {
            const float* v = &values[(k * perKey + (cubic ? 1 : 0)) * components];
            if constexpr (std::is_same_v<T, glm::quat>) {
                track.values[k].x = v[0];
                track.values[k].y = v[1];
                track.values[k].z = v[2];
                track.values[k].w = v[3];
                track.values[k] = glm::normalize(track.values[k]);
            } else {
                track.values[k] = T(v[0], v[1], v[2]);
            }
        }
        return true;
    }

    void readAnimations() {
        const cgltf_data& data = *m_data;
        bool warnedCubic = false;
        bool warnedMorph = false;
        for (cgltf_size a = 0; a < data.animations_count; ++a) {
            const cgltf_animation& source = data.animations[a];
            anim::SkeletalClip clip;
            clip.name = source.name != nullptr ? source.name : "clip" + std::to_string(a);
            std::unordered_map<int32_t, size_t> channelOf;
            for (cgltf_size c = 0; c < source.channels_count; ++c) {
                const cgltf_animation_channel& channel = source.channels[c];
                if (channel.target_node == nullptr || channel.sampler == nullptr ||
                    channel.sampler->input == nullptr || channel.sampler->output == nullptr) {
                    continue;
                }
                if (channel.target_path == cgltf_animation_path_type_weights) {
                    if (!warnedMorph) {
                        warnedMorph = true;
                        FORGE_WARN("model {}: morph-target (weights) channels ignored", m_path);
                    }
                    continue;
                }
                const auto node = m_nodeIndex.find(channel.target_node);
                if (node == m_nodeIndex.end()) {
                    continue;
                }
                if (channel.sampler->interpolation == cgltf_interpolation_type_cubic_spline &&
                    !warnedCubic) {
                    warnedCubic = true;
                    FORGE_WARN("model {}: CUBICSPLINE keys imported as LINEAR (tangents dropped)",
                               m_path);
                }
                auto [it, inserted] = channelOf.emplace(node->second, clip.channels.size());
                if (inserted) {
                    clip.channels.push_back(anim::NodeChannel{});
                    clip.channels.back().node = node->second;
                }
                anim::NodeChannel& out = clip.channels[it->second];
                bool ok = false;
                switch (channel.target_path) {
                case cgltf_animation_path_type_translation:
                    ok = fillTrack(*channel.sampler, out.translation, 3);
                    if (ok) {
                        clip.duration = std::max(clip.duration, out.translation.times.back());
                    }
                    break;
                case cgltf_animation_path_type_rotation:
                    ok = fillTrack(*channel.sampler, out.rotation, 4);
                    if (ok) {
                        clip.duration = std::max(clip.duration, out.rotation.times.back());
                    }
                    break;
                case cgltf_animation_path_type_scale:
                    ok = fillTrack(*channel.sampler, out.scale, 3);
                    if (ok) {
                        clip.duration = std::max(clip.duration, out.scale.times.back());
                    }
                    break;
                default: break;
                }
                if (!ok) {
                    FORGE_WARN("model {}: clip '{}' has a malformed channel, skipped", m_path,
                               clip.name);
                }
            }
            m_out.clips.push_back(std::move(clip));
        }
    }

    // ---- Static files ------------------------------------------------------

    void addMeshNode(const cgltf_node& node, const glm::mat4& world) {
        const std::string owner = nodeName(*m_data, node);
        const cgltf_mesh& mesh = *node.mesh;
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& primitive = mesh.primitives[p];
            SubmeshData part;
            part.name = owner;
            if (!readPrimitive(primitive, owner, part.mesh)) {
                continue;
            }
            part.material = primitive.material != nullptr
                                ? static_cast<uint32_t>(primitive.material - m_data->materials)
                                : defaultMaterial();
            transformMesh(part.mesh, world);
            m_out.submeshes.push_back(std::move(part));
        }
    }

    void walkScene() {
        const cgltf_data& data = *m_data;
        std::vector<const cgltf_node*> roots;
        const cgltf_scene* scene =
            data.scene != nullptr ? data.scene : (data.scenes_count > 0 ? data.scenes : nullptr);
        if (scene != nullptr) {
            roots.assign(scene->nodes, scene->nodes + scene->nodes_count);
        } else {
            for (cgltf_size i = 0; i < data.nodes_count; ++i) {
                if (data.nodes[i].parent == nullptr) {
                    roots.push_back(&data.nodes[i]);
                }
            }
        }
        struct Frame {
            const cgltf_node* node;
            glm::mat4 parent;
        };
        std::vector<Frame> stack;
        for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
            stack.push_back({*it, glm::mat4(1.0f)});
        }
        while (!stack.empty()) {
            const Frame frame = stack.back();
            stack.pop_back();
            const glm::mat4 world = frame.parent * localMatrix(*frame.node);
            if (frame.node->mesh != nullptr && !isHidden(m_options, nodeName(data, *frame.node))) {
                if (m_animated) {
                    addAnimatedMeshNode(*frame.node);
                } else {
                    addMeshNode(*frame.node, world);
                }
            }
            for (cgltf_size c = frame.node->children_count; c-- > 0;) {
                stack.push_back({frame.node->children[c], world});
            }
        }
    }

    std::string m_path;
    const ImportOptions& m_options;
    std::filesystem::path m_dir;
    DataPtr m_data;
    ModelData m_out;
    std::unordered_map<const cgltf_image*, int32_t> m_images;
    int32_t m_defaultMaterial = -1;
    // Animated files only.
    bool m_animated = false;
    bool m_warnedExtraInfluences = false;
    std::shared_ptr<anim::Skeleton> m_skeleton;
    std::unordered_map<const cgltf_node*, int32_t> m_nodeIndex;
    std::unordered_map<const cgltf_node*, uint16_t> m_rigidJoints;
    std::vector<uint32_t> m_skinBase; // first joint of each skin
};

} // namespace

ModelData loadGltfModel(const std::string& path, const ImportOptions& options) {
    return Importer(path, options).run();
}

} // namespace forge::assets::detail

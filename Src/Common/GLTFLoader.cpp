// Need to be defined only once. See github.com/syoyo/tinygltf#loading-gltf-20-model.
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "GLTFLoader.h"

GLTFLoader::GLTFLoader(string &filename) : mFilename(filename)
{}

unsigned int GLTFLoader::getPrimitiveCount(int nodeIdx) const {
    const tinygltf::Scene &scene = mModel.scenes[mModel.defaultScene];
    auto &node = mModel.nodes[scene.nodes[nodeIdx]];
    auto &mesh = mModel.meshes[node.mesh];
    return mesh.primitives.size();
}

GLTFPrimitiveData GLTFLoader::Load(string &filename) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    string err;
    string warn;

    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
    if (!warn.empty()) {
        printf("Warn: %s\n", warn.c_str());
    }
    if (!err.empty()) {
        printf("Err: %s\n", err.c_str());
    }
    if (!ret) {
        printf("Failed to parse glTF\n");
    }

    GLTFPrimitiveData loadedData;

    const tinygltf::Scene &scene = model.scenes[model.defaultScene];

    for (int j = 0; j < scene.nodes.size(); ++j) {
        auto &node = model.nodes[scene.nodes[j]];
        if ((node.mesh >= 0) && (node.mesh < model.meshes.size())) {
            auto &mesh = model.meshes[node.mesh];

            const size_t index_remap[] = {0, 2, 1};

            for (size_t i = 0; i < mesh.primitives.size(); ++i) {
                tinygltf::Primitive primitive = mesh.primitives[i];

                tinygltf::Accessor indexAccessor = model.accessors[primitive.indices];
                const tinygltf::BufferView &indexBufferView = model.bufferViews[indexAccessor.bufferView];
                const tinygltf::Buffer &indexBuffer = model.buffers[indexBufferView.buffer];
                const uint8_t *indexes = indexBuffer.data.data() + indexBufferView.byteOffset;
                int stride = indexAccessor.ByteStride(indexBufferView);

                loadedData.indices.resize(indexAccessor.count);

                int indexOffset = 0;
                int vertexOffset = 0;

                if (stride == 1) {
                    for (size_t i = 0; i < indexAccessor.count; i += 3) {
                        loadedData.indices[indexOffset + i + 0] = vertexOffset + indexes[i + index_remap[0]];
                        loadedData.indices[indexOffset + i + 1] = vertexOffset + indexes[i + index_remap[1]];
                        loadedData.indices[indexOffset + i + 2] = vertexOffset + indexes[i + index_remap[2]];
                    }
                } else if (stride == 2) {
                    for (size_t i = 0; i < indexAccessor.count; i += 3) {
                        loadedData.indices[indexOffset + i + 0] = vertexOffset + ((uint16_t *)indexes)[i + index_remap[0]];
                        loadedData.indices[indexOffset + i + 1] = vertexOffset + ((uint16_t *)indexes)[i + index_remap[1]];
                        loadedData.indices[indexOffset + i + 2] = vertexOffset + ((uint16_t *)indexes)[i + index_remap[2]];
                    }
                } else if (stride == 4) {
                    for (size_t i = 0; i < indexAccessor.count; i += 3) {
                        loadedData.indices[indexOffset + i + 0] = vertexOffset + ((uint32_t *)indexes)[i + index_remap[0]];
                        loadedData.indices[indexOffset + i + 1] = vertexOffset + ((uint32_t *)indexes)[i + index_remap[1]];
                        loadedData.indices[indexOffset + i + 2] = vertexOffset + ((uint32_t *)indexes)[i + index_remap[2]];
                    }
                } else {
                    assert(0 && "unsupported index stride!");
                }

                const tinygltf::Accessor &accessor = model.accessors[primitive.attributes["POSITION"]];
                const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
                stride = accessor.ByteStride(bufferView);
                const uint8_t *data = buffer.data.data() + accessor.byteOffset + bufferView.byteOffset;

                for (size_t i = 0; i < accessor.count; ++i) {
                    loadedData.vertices.push_back(*(const XMFLOAT3 *)(data + i * stride));
                }
            }
        }
    }

    return loadedData;
}

void GLTFLoader::LoadModel() {
    tinygltf::TinyGLTF loader;

    string err;
    string warn;

    bool ret = loader.LoadASCIIFromFile(&mModel, &err, &warn, mFilename);
    if (!warn.empty()) {
        printf("Warn: %s\n", warn.c_str());
    }
    if (!err.empty()) {
        printf("Err: %s\n", err.c_str());
    }
    if (!ret) {
        printf("Failed to parse glTF\n");
    }
}

// Loads a single primitive from the specified node.
GLTFPrimitiveData GLTFLoader::LoadPrimitive(int nodeIdx, int primitiveIdx) const {
    GLTFPrimitiveData primitiveData;

    const tinygltf::Scene &scene = mModel.scenes[mModel.defaultScene];
    auto &node = mModel.nodes[scene.nodes[nodeIdx]];
    auto &mesh = mModel.meshes[node.mesh];

    // The primitive to load.
    tinygltf::Primitive primitive = mesh.primitives[primitiveIdx];

    // Load indices.

    const size_t index_remap[] = {0, 2, 1};
    tinygltf::Accessor indexAccessor = mModel.accessors[primitive.indices];
    const tinygltf::BufferView &indexBufferView = mModel.bufferViews[indexAccessor.bufferView];
    const tinygltf::Buffer &indexBuffer = mModel.buffers[indexBufferView.buffer];
    const uint8_t *indexes = indexBuffer.data.data() + indexBufferView.byteOffset;
    int stride = indexAccessor.ByteStride(indexBufferView);
    primitiveData.indices.resize(indexAccessor.count);

    if (stride == 1) {
        for (size_t i = 0; i < indexAccessor.count; i += 3) {
            primitiveData.indices[i + 0] = indexes[i + index_remap[0]];
            primitiveData.indices[i + 1] = indexes[i + index_remap[1]];
            primitiveData.indices[i + 2] = indexes[i + index_remap[2]];
        }
    } else if (stride == 2) {
        for (size_t i = 0; i < indexAccessor.count; i += 3) {
            primitiveData.indices[i + 0] = ((uint16_t *)indexes)[i + index_remap[0]];
            primitiveData.indices[i + 1] = ((uint16_t *)indexes)[i + index_remap[1]];
            primitiveData.indices[i + 2] = ((uint16_t *)indexes)[i + index_remap[2]];
        }
    } else if (stride == 4) {
        for (size_t i = 0; i < indexAccessor.count; i += 3) {
            primitiveData.indices[i + 0] = ((uint32_t *)indexes)[i + index_remap[0]];
            primitiveData.indices[i + 1] = ((uint32_t *)indexes)[i + index_remap[1]];
            primitiveData.indices[i + 2] = ((uint32_t *)indexes)[i + index_remap[2]];
        }
    } else {
        assert(0 && "unsupported index stride");
    }

    // Load vertex positions.

    const tinygltf::Accessor &accessor = mModel.accessors[primitive.attributes["POSITION"]];
    const tinygltf::BufferView &bufferView = mModel.bufferViews[accessor.bufferView];
    const tinygltf::Buffer &buffer = mModel.buffers[bufferView.buffer];
    stride = accessor.ByteStride(bufferView);
    const uint8_t *data = buffer.data.data() + accessor.byteOffset + bufferView.byteOffset;

    for (size_t i = 0; i < accessor.count; ++i) {
        primitiveData.vertices.push_back(*(const XMFLOAT3 *)(data + i * stride));
    }

    return primitiveData;
}
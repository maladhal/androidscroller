#ifndef ANDROIDGLINVESTIGATIONS_MODEL_H
#define ANDROIDGLINVESTIGATIONS_MODEL_H

#include <vector>

union Vector3 {
    struct {
        float x, y, z;
    };
    float idx[3];
};

union Vector2 {
    struct {
        float x, y;
    };
    struct {
        float u, v;
    };
    float idx[2];
};

struct Vertex {
    constexpr Vertex(const Vector3 &inPosition, const Vector3 &inColor) : position(inPosition),
                                                                          color(inColor) {}

    Vector3 position;
    Vector3 color;
};;

typedef uint16_t Index;

class Model {
public:
    inline Model(
            std::vector<Vertex> vertices,
            std::vector<Index> indices)
            : vertices_(std::move(vertices)),
              indices_(std::move(indices)) {}

    inline const Vertex *getVertexData() const {
        return vertices_.data();
    }

    inline const size_t getIndexCount() const {
        return indices_.size();
    }

    inline const Index *getIndexData() const {
        return indices_.data();
    }

private:
    std::vector<Vertex> vertices_;
    std::vector<Index> indices_;
};

#endif //ANDROIDGLINVESTIGATIONS_MODEL_H
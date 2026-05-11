#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// VoxelGrid.h  –  Sparse voxel data structure.
//
// Stores occupied grid cells in an unordered_set keyed by Vec3i.
// All operations are O(1) average-case.
//
// The conceptual grid is infinite: there are no fixed bounds.
// Only occupied voxels consume memory (sparse representation).
// ─────────────────────────────────────────────────────────────────────────────

#include "MathUtils.h"
#include <unordered_set>
#include <cstddef>

class VoxelGrid
{
public:
    // ── Mutation ──────────────────────────────────────────────────────────────

    /// Place a voxel at the given integer grid position.
    /// No-op if already occupied.
    void add(const Vec3i& pos)
    {
        voxels.insert(pos);
    }

    /// Remove the voxel at the given position.
    /// No-op if not present.
    void remove(const Vec3i& pos)
    {
        voxels.erase(pos);
    }

    /// Toggle: remove if present, add if absent.
    void toggle(const Vec3i& pos)
    {
        auto it = voxels.find(pos);
        if (it != voxels.end())
            voxels.erase(it);
        else
            voxels.insert(pos);
    }

    /// Remove all voxels.
    void clear()
    {
        voxels.clear();
    }

    // ── Query ─────────────────────────────────────────────────────────────────

    /// Returns true if a voxel occupies the given position.  O(1) average.
    bool contains(const Vec3i& pos) const
    {
        return voxels.count(pos) > 0;
    }

    /// Number of voxels currently stored.
    std::size_t size() const { return voxels.size(); }

    bool empty() const { return voxels.empty(); }

    // ── Iteration ─────────────────────────────────────────────────────────────

    /// Direct read-only access to the underlying set (for mesh building).
    const std::unordered_set<Vec3i, Vec3iHash>& getVoxels() const
    {
        return voxels;
    }

 bool move(const Vec3i& from, const Vec3i& to)
{
    if (from == to)
        return true;

    if (voxels.find(from) == voxels.end())
        return false;

    if (voxels.find(to) != voxels.end())
        return false;

    voxels.erase(from);
    voxels.insert(to);
    return true;
}

private:
    std::unordered_set<Vec3i, Vec3iHash> voxels;
};
